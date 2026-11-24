#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "ocr_engine.h"
#include "pixel_ops.h"
#include "ocr_preprocess.h"
#include "ocr_crop.h"
#include "ppocrv6_det.h"
#include "ppocrv6_ocr.h"
#include "core/env_gate.h"
#include "core/gpu_backend_pref.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace {

struct File {
    HANDLE handle = INVALID_HANDLE_VALUE;
    ~File() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
    File() = default;
    File(const File &) = delete;
    File &operator=(const File &) = delete;
};

struct DetectorDeleter {
    void operator()(ppocrv6_det::context *p) const noexcept {
        try { ppocrv6_det::free(p); } catch (...) {}
    }
};
struct RecognizerDeleter {
    void operator()(ppocrv6_ocr_context *p) const noexcept {
        try { ppocrv6_ocr_free(p); } catch (...) {}
    }
};

struct Context {
    std::unique_ptr<ppocrv6_det::context, DetectorDeleter> detector;
    std::unique_ptr<ppocrv6_ocr_context, RecognizerDeleter> recognizer;
    std::vector<std::string> texts;
    std::vector<SnipliteOcrResult> results;
    std::vector<uint8_t> rgb;
    std::string detector_path, recognizer_path;
    int threads = 1;
    bool vulkan = false;
    OcrResizePlan resize_plan;
};

bool initialize(Context &ctx, bool vulkan) {
    ctx.recognizer.reset();
    ctx.detector.reset();
    ctx.results.clear();
    ctx.texts.clear();
    ctx.vulkan = vulkan;
    if (_putenv_s("CRISPEMBED_PPOCRV6_FORCE_CPU", vulkan ? "0" : "1") ||
        _putenv_s("CRISPEMBED_SHARED_GPU_BACKEND", "0") ||
        _putenv_s("CRISPEMBED_PPOCRV6_NO_GRAPH", "0") ||
        _putenv_s("CRISPEMBED_PPOCRV6_BATCH_GRAPH", "1") ||
        _putenv_s("CRISPEMBED_PPOCRV6_GRAPH_ACCEPT", "1")) return false;
    crispasr_set_gpu_backend_pref(vulkan ? "vulkan" : "cpu");
    try {
        ctx.detector.reset(ppocrv6_det::init(ctx.detector_path.c_str(), ctx.threads));
        if (!ctx.detector) return false;
        ctx.recognizer.reset(ppocrv6_ocr_init(ctx.recognizer_path.c_str(), ctx.threads));
        if (!ctx.recognizer) return false;
        fprintf(stderr, "sniplite OCR: %s models initialized\n", vulkan ? "Vulkan" : "CPU");
        return true;
    } catch (...) {
        return false;
    }
}

uint16_t le16(const uint8_t *p) {
    return uint16_t(p[0]) | (uint16_t(p[1]) << 8);
}
uint32_t le32(const uint8_t *p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

bool read_exact(HANDLE file, void *buffer, DWORD size) {
    auto *p = static_cast<uint8_t *>(buffer);
    while (size) {
        DWORD done = 0;
        if (!ReadFile(file, p, size, &done, nullptr) || done == 0) return false;
        p += done;
        size -= done;
    }
    return true;
}

bool load_bitmap(const char *path, std::vector<uint8_t> &rgb, int &width, int &height) {
    if (!path || !path[0]) return false;
    const int chars = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, nullptr, 0);
    if (chars <= 1) return false;
    std::wstring wide(static_cast<size_t>(chars), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide.data(), chars) != chars) return false;
    File file;
    file.handle = CreateFileW(wide.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file.handle == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER file_size;
    uint8_t header[54];
    if (!GetFileSizeEx(file.handle, &file_size) || file_size.QuadPart < sizeof header ||
        !read_exact(file.handle, header, sizeof header)) return false;
    // This is deliberately the application's BMP contract, not a general BMP decoder.
    if (header[0] != 'B' || header[1] != 'M' || le32(header + 10) != 54 || le32(header + 14) != 40 ||
        le16(header + 26) != 1 || le16(header + 28) != 32 || le32(header + 30) != BI_RGB) return false;
    const uint32_t w = le32(header + 18), h = le32(header + 22);
    // Bounds also protect the vendor crop geometry and signed pixel indexing.
    if (w == 0 || h == 0 || w > 32768 || h > 32768) return false;
    const uint64_t pixels = uint64_t(w) * h;
    const uint64_t bytes = pixels * 4;
    if (pixels > uint64_t(std::numeric_limits<int>::max()) / 3 ||
        bytes > uint64_t(MAXDWORD) - sizeof header || le32(header + 2) != bytes + sizeof header ||
        le32(header + 34) != bytes || uint64_t(file_size.QuadPart) != bytes + sizeof header) return false;
    width = static_cast<int>(w);
    height = static_cast<int>(h);
    rgb.resize(static_cast<size_t>(pixels) * 3);
    std::vector<uint8_t> row(static_cast<size_t>(w) * 4);
    for (int y = height - 1; y >= 0; --y) {
        if (!read_exact(file.handle, row.data(), static_cast<DWORD>(row.size()))) return false;
        uint8_t *dst = rgb.data() + static_cast<size_t>(y) * width * 3;
        pixel_bgra_to_rgb(dst, row.data(), static_cast<std::size_t>(width));
    }
    return true;
}

// Match the original detector's detect_file geometry, while keeping decoding
// inside the bridge and retaining the full-resolution pixels for line crops.
std::vector<ppocrv6_det::box> detect(Context &ctx, const uint8_t *rgb, int w, int h) {
    const float scale = std::min(1.0f, 960.0f / float(std::max(w, h)));
    const int rw = std::max(32, int(std::floor(w * scale / 32.0f + 0.5f)) * 32);
    const int rh = std::max(32, int(std::floor(h * scale / 32.0f + 0.5f)) * 32);
    std::vector<uint8_t> resized;
    const uint8_t *input = rgb;
    if (rw != w || rh != h) {
        resized.resize(static_cast<size_t>(rw) * rh * 3);
        ocr_resize_prepare(ctx.resize_plan, w, rw);
        ocr_resize_rgb(resized.data(), rgb, w, h, rw, rh, ctx.resize_plan.columns.data());
        input = resized.data();
    }
    auto boxes = ppocrv6_det::detect_raw(ctx.detector.get(), input, rw, rh, 3, 0.2f);
    const float sx = float(w) / rw, sy = float(h) / rh;
    for (auto &b : boxes) {
        b.x *= sx; b.w *= sx;
        b.y *= sy; b.h *= sy;
        for (int i = 0; i < 4; ++i) {
            b.qx[i] *= sx;
            b.qy[i] *= sy;
        }
    }
    return boxes;
}

bool detector_succeeded(const Context &ctx) {
    int h = 0, w = 0;
    const float *prob = ppocrv6_det::last_probability(ctx.detector.get(), &h, &w);
    if (!prob || h <= 0 || w <= 0 || h > 1024 || w > 1024) return false;
    const size_t count = static_cast<size_t>(h) * w;
    for (size_t i = 0; i < count; ++i)
        if (!std::isfinite(prob[i])) return false;
    return true;
}


bool run(Context &ctx, const uint8_t *rgb, int width, int height) {
    auto boxes = detect(ctx, rgb, width, height);
    if (!detector_succeeded(ctx)) return false;
    if (boxes.empty()) return true;
    if (boxes.size() > static_cast<size_t>(std::numeric_limits<int>::max())) return false;

    constexpr size_t crop_batch_limit = 8;
    constexpr int text_capacity = 4096;
    const size_t n = boxes.size();
    ctx.texts.resize(n);
    ctx.results.reserve(n);
    const size_t result_count = ctx.results.size();
    auto fail = [&]() {
        ctx.results.resize(result_count);
        return false;
    };

    for (const auto &b : boxes) {
        if (!std::isfinite(b.x) || !std::isfinite(b.y) || !std::isfinite(b.w) || !std::isfinite(b.h) ||
            !std::isfinite(b.score) || b.x < 0 || b.y < 0 || b.w <= 0 || b.h <= 0 ||
            b.x > width || b.y > height || b.w > width || b.h > height) return fail();
        for (int k = 0; k < 4; ++k)
            if (!std::isfinite(b.qx[k]) || !std::isfinite(b.qy[k]) || b.qx[k] < 0 || b.qy[k] < 0 ||
                b.qx[k] > width || b.qy[k] > height) return fail();
    }

    std::vector<size_t> order(n);
    for (size_t i = 0; i < n; ++i) order[i] = i;
    auto aspect_ratio = [&](size_t i) {
        const auto &b = boxes[i];
        if (std::hypot(b.qx[1] - b.qx[0], b.qy[1] - b.qy[0]) <= 1.0f)
            return (b.w + 4.0f) / (b.h + 4.0f);
        std::array<float, 4> edges{};
        for (int k = 0; k < 4; ++k) {
            const int next = (k + 1) & 3;
            edges[k] = std::hypot(b.qx[next] - b.qx[k], b.qy[next] - b.qy[k]);
        }
        std::sort(edges.begin(), edges.end(), [](float a, float b) { return a > b; });
        return (edges[0] + edges[1] + 4.0f) / (edges[2] + edges[3] + 4.0f);
    };
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        return aspect_ratio(a) < aspect_ratio(b);
    });

    std::vector<std::vector<uint8_t>> crops;
    std::vector<int> widths, heights, channels, capacities, lengths;
    std::vector<ocr_crop::orientation_info> orientations(n);
    std::vector<const uint8_t *> pixels;
    std::vector<char *> outputs;
    std::vector<char> output_storage(crop_batch_limit * text_capacity);
    crops.reserve(crop_batch_limit);
    widths.reserve(crop_batch_limit);
    heights.reserve(crop_batch_limit);

    for (size_t start = 0; start < n; start += crop_batch_limit) {
        const size_t count = std::min(crop_batch_limit, n - start);
        crops.clear();
        widths.clear();
        heights.clear();
        for (size_t position = start; position < start + count; ++position) {
            const size_t i = order[position];
            const auto &b = boxes[i];
            int cw = 0, ch = 0;
            const bool quad = std::hypot(b.qx[1] - b.qx[0], b.qy[1] - b.qy[0]) > 1.0f;
            auto crop = quad ? ocr_crop::extract_quad(rgb, width, height, 3, b.qx, b.qy, 2, &cw, &ch)
                             : ocr_crop::extract(rgb, width, height, 3, int(b.x), int(b.y), int(b.w), int(b.h),
                                                 2, &cw, &ch);
            if (crop.empty() || cw <= 0 || ch <= 0) return fail();
            orientations[i] = ocr_crop::orient_180_rgb_info(crop, cw, ch);
            widths.push_back(cw);
            heights.push_back(ch);
            crops.push_back(std::move(crop));
        }

        pixels.resize(count);
        outputs.resize(count);
        channels.assign(count, 3);
        capacities.assign(count, text_capacity);
        lengths.assign(count, 0);
        for (size_t j = 0; j < count; ++j) {
            pixels[j] = crops[j].data();
            outputs[j] = output_storage.data() + j * text_capacity;
        }
        if (ppocrv6_ocr_recognize_raw_batch(ctx.recognizer.get(), pixels.data(), widths.data(), heights.data(),
                                            channels.data(), static_cast<int>(count), outputs.data(), capacities.data(),
                                            lengths.data()) != static_cast<int>(count)) return fail();

        for (size_t j = 0; j < count; ++j) {
            const size_t i = order[start + j];
            int length = lengths[j];
            if (length < 0 || length >= text_capacity) return fail();
            if (length == text_capacity - 1) {
                // The vendor batch API truncates without reporting the full size.
                // Recover a saturated line through its unbounded single-line API.
                const char *text = ppocrv6_ocr_recognize_raw(ctx.recognizer.get(), pixels[j], widths[j], heights[j],
                                                            3, &length);
                if (!text || length < 0) return fail();
                ctx.texts[i].assign(text, static_cast<size_t>(length));
            } else {
                ctx.texts[i].assign(outputs[j], static_cast<size_t>(length));
            }
        }
    }

    for (size_t i = 0; i < n; ++i) {
        if (ctx.texts[i].empty()) continue;
        const auto &b = boxes[i];
        const auto &o = orientations[i];
        const int length = static_cast<int>(ctx.texts[i].size());
        // This vendor API exposes detector confidence, as did the orchestrator.
        ctx.results.push_back({b.x, b.y, b.w, b.h, b.score, ctx.texts[i].c_str(), length,
                               o.corrected ? 1 : 0, o.angle, o.confidence});
    }
    return true;
}

bool load_bgra(Context &ctx, const uint8_t *pixels, int width, int height,
               size_t stride_bytes, size_t data_bytes) {
    if (!pixels || width <= 0 || height <= 0 || width > 32768 || height > 32768) return false;
    const size_t row_bytes = static_cast<size_t>(width) * 4;
    const size_t rows = static_cast<size_t>(height - 1);
    const uint64_t pixel_count = uint64_t(width) * height;
    if (pixel_count > uint64_t(std::numeric_limits<int>::max()) / 3 || stride_bytes < row_bytes ||
        rows > (std::numeric_limits<size_t>::max() - row_bytes) / stride_bytes ||
        data_bytes < rows * stride_bytes + row_bytes) return false;
    ctx.rgb.resize(static_cast<size_t>(pixel_count) * 3);
    for (int y = 0; y < height; ++y)
        pixel_bgra_to_rgb(ctx.rgb.data() + static_cast<size_t>(y) * width * 3,
                          pixels + static_cast<size_t>(y) * stride_bytes, static_cast<size_t>(width));
    return true;
}

template <typename Job>
const SnipliteOcrResult *run_request(void *context, int *count, const Job &job) {
    if (!count) return nullptr;
    *count = -1;
    if (!context) return nullptr;
    auto &ctx = *static_cast<Context *>(context);
    ctx.results.clear();
    bool ok = false;
    try { ok = job(ctx); } catch (...) {}
    if (!ok && ctx.vulkan) {
        fprintf(stderr, "sniplite OCR: Vulkan inference failed; retrying on CPU\n");
        try { ok = initialize(ctx, false) && job(ctx); } catch (...) {}
    }
    if (!ok) {
        ctx.results.clear();
        return nullptr;
    }
    *count = static_cast<int>(ctx.results.size());
    return ctx.results.empty() ? nullptr : ctx.results.data();
}

} // namespace

extern "C" void *sniplite_ocr_init(const char *det, const char *rec, int threads) {
    if (!det || !det[0] || !rec || !rec[0]) return nullptr;
    try {
        auto ctx = std::make_unique<Context>();
        ctx->detector_path = det;
        ctx->recognizer_path = rec;
        ctx->threads = std::max(1, threads);
        bool force_cpu;
#if defined(SNIPLITE_CPU_ONLY)
        force_cpu = true;
#elif defined(SNIPLITE_VULKAN_PREFERRED)
        force_cpu = false;
#else
        WCHAR backend[16] = {0};
        const DWORD backend_len = GetEnvironmentVariableW(L"SNIPLITE_OCR_BACKEND", backend, 16);
        force_cpu = backend_len == 3 && _wcsicmp(backend, L"cpu") == 0;
#endif
        bool vulkan = false;
        if (!force_cpu) {
            try { vulkan = crispasr_gpu_device_kind_present("Vulkan"); }
            catch (...) { fprintf(stderr, "sniplite OCR: Vulkan device discovery failed; using CPU\n"); }
        }
        if (!initialize(*ctx, vulkan)) {
            if (!vulkan) return nullptr;
            fprintf(stderr, "sniplite OCR: Vulkan initialization failed; retrying on CPU\n");
            if (!initialize(*ctx, false)) return nullptr;
        }
        return ctx.release();
    } catch (...) {
        return nullptr;
    }
}

extern "C" void sniplite_ocr_free(void *context) {
    try { delete static_cast<Context *>(context); } catch (...) {}
}

extern "C" const SnipliteOcrResult *sniplite_ocr_run(void *context, const char *bitmap_path, int *count) {
    return run_request(context, count, [bitmap_path](Context &ctx) {
        int width = 0, height = 0;
        return load_bitmap(bitmap_path, ctx.rgb, width, height) && run(ctx, ctx.rgb.data(), width, height);
    });
}

extern "C" const SnipliteOcrResult *sniplite_ocr_run_bgra(void *context, const unsigned char *pixels,
    int width, int height, size_t stride_bytes, size_t data_bytes, int *count) {
    return run_request(context, count, [=](Context &ctx) {
        return load_bgra(ctx, pixels, width, height, stride_bytes, data_bytes) &&
               run(ctx, ctx.rgb.data(), width, height);
    });
}

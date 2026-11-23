#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "ocr_engine.h"
#include "ocr_crop.h"
#include "ppocrv6_det.h"
#include "ppocrv6_ocr.h"
#include "core/env_gate.h"
#include "core/gpu_backend_pref.h"

#include <algorithm>
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
    std::string detector_path, recognizer_path;
    int threads = 1;
    bool vulkan = false;
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
        for (int x = 0; x < width; ++x) {
            dst[3 * x] = row[4 * x + 2];
            dst[3 * x + 1] = row[4 * x + 1];
            dst[3 * x + 2] = row[4 * x];
        }
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
        for (int y = 0; y < rh; ++y) {
            const float fy = std::max(0.0f, (y + 0.5f) * h / rh - 0.5f);
            const int y0 = std::min(h - 1, int(fy)), y1 = std::min(h - 1, y0 + 1);
            const float wy = fy - y0;
            for (int x = 0; x < rw; ++x) {
                const float fx = std::max(0.0f, (x + 0.5f) * w / rw - 0.5f);
                const int x0 = std::min(w - 1, int(fx)), x1 = std::min(w - 1, x0 + 1);
                const float wx = fx - x0;
                for (int c = 0; c < 3; ++c) {
                    const float a = rgb[(static_cast<size_t>(y0) * w + x0) * 3 + c] * (1 - wx) +
                                    rgb[(static_cast<size_t>(y0) * w + x1) * 3 + c] * wx;
                    const float b = rgb[(static_cast<size_t>(y1) * w + x0) * 3 + c] * (1 - wx) +
                                    rgb[(static_cast<size_t>(y1) * w + x1) * 3 + c] * wx;
                    resized[(static_cast<size_t>(y) * rw + x) * 3 + c] =
                        static_cast<uint8_t>(std::clamp(a * (1 - wy) + b * wy, 0.0f, 255.0f));
                }
            }
        }
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

void set_graph_acceptance(Context &ctx, size_t regions) {
    // The original region budget protects Metal planning. CPU batch graphs do
    // not incur that dispatch cost and must remain available on full pages.
    if (core_env::on("CRISPEMBED_PPOCRV6_FORCE_CPU") && core_env::on("CRISPEMBED_PPOCRV6_BATCH_GRAPH")) {
        ppocrv6_ocr_set_graph_accept(ctx.recognizer.get(), 1);
    } else if (core_env::on("CRISPEMBED_PPOCRV6_GRAPH_ACCEPT")) {
        long limit = 8;
        if (const char *value = std::getenv("CRISPEMBED_PPOCRV6_GRAPH_MAX_REGIONS")) {
            char *end = nullptr;
            const long parsed = std::strtol(value, &end, 10);
            if (end != value && *end == '\0' && parsed >= 0) limit = parsed;
        }
        ppocrv6_ocr_set_graph_accept(ctx.recognizer.get(), regions <= static_cast<size_t>(limit) ? 1 : 0);
    } else {
        ppocrv6_ocr_set_graph_accept(ctx.recognizer.get(), -1);
    }
}

bool run(Context &ctx, const char *path) {
    std::vector<uint8_t> rgb;
    int width = 0, height = 0;
    if (!load_bitmap(path, rgb, width, height)) return false;
    auto boxes = detect(ctx, rgb.data(), width, height);
    if (!detector_succeeded(ctx)) return false;
    if (boxes.empty()) return true;
    if (boxes.size() > static_cast<size_t>(std::numeric_limits<int>::max())) return false;
    set_graph_acceptance(ctx, boxes.size());

    std::vector<std::vector<uint8_t>> crops;
    std::vector<int> widths, heights;
    std::vector<ocr_crop::orientation_info> orientations;
    crops.reserve(boxes.size());
    widths.reserve(boxes.size());
    heights.reserve(boxes.size());
    orientations.reserve(boxes.size());
    for (size_t i = 0; i < boxes.size(); ++i) {
        const auto &b = boxes[i];
        if (!std::isfinite(b.x) || !std::isfinite(b.y) || !std::isfinite(b.w) || !std::isfinite(b.h) ||
            !std::isfinite(b.score) || b.x < 0 || b.y < 0 || b.w <= 0 || b.h <= 0 ||
            b.x > width || b.y > height || b.w > width || b.h > height) return false;
        for (int k = 0; k < 4; ++k)
            if (!std::isfinite(b.qx[k]) || !std::isfinite(b.qy[k]) || b.qx[k] < 0 || b.qy[k] < 0 ||
                b.qx[k] > width || b.qy[k] > height) return false;
        int cw = 0, ch = 0;
        const bool quad = std::hypot(b.qx[1] - b.qx[0], b.qy[1] - b.qy[0]) > 1.0f;
        auto crop = quad ? ocr_crop::extract_quad(rgb.data(), width, height, 3, b.qx, b.qy, 2, &cw, &ch)
                         : ocr_crop::extract(rgb.data(), width, height, 3, int(b.x), int(b.y), int(b.w), int(b.h),
                                             2, &cw, &ch);
        if (crop.empty() || cw <= 0 || ch <= 0) return false;
        auto orientation = ocr_crop::orient_180_rgb_info(crop, cw, ch);
        widths.push_back(cw);
        heights.push_back(ch);
        orientations.push_back(orientation);
        crops.push_back(std::move(crop));
    }

    const size_t n = crops.size();
    std::vector<const uint8_t *> pixels(n);
    std::vector<char *> outputs(n);
    constexpr int text_capacity = 4096;
    std::vector<int> channels(n, 3), capacities(n, text_capacity), lengths(n, 0);
    ctx.texts.resize(n);
    for (size_t i = 0; i < n; ++i) {
        ctx.texts[i].resize(text_capacity);
        pixels[i] = crops[i].data();
        outputs[i] = ctx.texts[i].data();
    }
    if (ppocrv6_ocr_recognize_raw_batch(ctx.recognizer.get(), pixels.data(), widths.data(), heights.data(),
                                       channels.data(), static_cast<int>(n), outputs.data(), capacities.data(),
                                       lengths.data()) != static_cast<int>(n)) return false;
    ctx.results.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        int length = lengths[i];
        if (length < 0 || length >= text_capacity) return false;
        if (length == text_capacity - 1) {
            // The vendor batch API truncates without reporting the full size.
            // Recover a saturated line through its unbounded single-line API.
            const char *text = ppocrv6_ocr_recognize_raw(ctx.recognizer.get(), pixels[i], widths[i], heights[i],
                                                       3, &length);
            if (!text || length < 0) return false;
            ctx.texts[i].assign(text, static_cast<size_t>(length));
        } else {
            ctx.texts[i].resize(static_cast<size_t>(length));
        }
        if (length == 0) continue;
        const auto &b = boxes[i];
        const auto &o = orientations[i];
        // This vendor API exposes detector confidence, as did the orchestrator.
        ctx.results.push_back({b.x, b.y, b.w, b.h, b.score, ctx.texts[i].c_str(), length,
                               o.corrected ? 1 : 0, o.angle, o.confidence});
    }
    return true;
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
    if (!count) return nullptr;
    *count = -1;
    if (!context) return nullptr;
    auto &ctx = *static_cast<Context *>(context);
    ctx.results.clear();
    bool ok = false;
    try { ok = run(ctx, bitmap_path); } catch (...) {}
    if (!ok && ctx.vulkan) {
        fprintf(stderr, "sniplite OCR: Vulkan inference failed; retrying on CPU\n");
        try { ok = initialize(ctx, false) && run(ctx, bitmap_path); } catch (...) {}
    }
    if (!ok) {
        ctx.results.clear();
        return nullptr;
    }
    *count = static_cast<int>(ctx.results.size());
    return ctx.results.empty() ? nullptr : ctx.results.data();
}

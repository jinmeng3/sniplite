#include "microtex.h"
#include <wtypes.h>
#include <gdiplus.h>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <string>
#undef TRANSPARENT
#include "latex.h"
#include "platform/gdi_win/graphic_win32.h"

namespace {
ULONG_PTR gdiplus_token;
bool initialized;

void detail(WCHAR *out, int cap, const char *message) noexcept {
    if (!out || cap <= 0) return;
    out[0] = 0;
    if (!message) return;
    int length = MultiByteToWideChar(CP_UTF8, 0, message, -1, nullptr, 0);
    if (length > 0 && length <= cap) {
        MultiByteToWideChar(CP_UTF8, 0, message, -1, out, cap);
    } else {
        lstrcpynW(out, L"MicroTeX could not render this formula.", cap);
    }
}

std::string utf8(const WCHAR *text) {
    int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1, nullptr, 0, nullptr, nullptr);
    if (!bytes) throw std::runtime_error("Invalid Unicode resource directory.");
    std::string result(static_cast<size_t>(bytes), '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1, result.data(), bytes, nullptr, nullptr))
        throw std::runtime_error("Could not encode the resource directory.");
    result.pop_back();
    return result;
}
}

extern "C" MT_API int microtex_initialize(const WCHAR *resource_dir, WCHAR *error, int error_cap) {
    detail(error, error_cap, nullptr);
    if (initialized) return 1;
    try {
        if (!resource_dir || !*resource_dir) throw std::runtime_error("Missing MicroTeX resources.");
        std::wstring marker = std::wstring(resource_dir) + L"\\.clatexmath-res_root";
        if (GetFileAttributesW(marker.c_str()) == INVALID_FILE_ATTRIBUTES)
            throw std::runtime_error("MicroTeX resources are incomplete.");
        if (!gdiplus_token) {
            Gdiplus::GdiplusStartupInput input;
            if (Gdiplus::GdiplusStartup(&gdiplus_token, &input, nullptr) != Gdiplus::Ok)
                throw std::runtime_error("Windows GDI+ could not start.");
        }
        tex::LaTeX::init(utf8(resource_dir));
        initialized = true;
        return 1;
    } catch (const std::exception &e) {
        detail(error, error_cap, e.what());
    } catch (...) {
        detail(error, error_cap, "MicroTeX initialization failed.");
    }
    return 0;
}

extern "C" MT_API MicrotexRender *microtex_create(const WCHAR *latex, int max_width, float font_size,
                                                 uint32_t foreground, int *width, int *height,
                                                 WCHAR *error, int error_cap) {
    detail(error, error_cap, nullptr);
    if (width) *width = 0;
    if (height) *height = 0;
    try {
        if (!initialized) throw std::runtime_error("MicroTeX is not initialized.");
        if (!latex || !*latex || max_width <= 0 || !std::isfinite(font_size) || font_size <= 0)
            throw std::runtime_error("Invalid formula or render dimensions.");
        std::unique_ptr<tex::TeXRender> render(tex::LaTeX::parse(latex, max_width, font_size, font_size / 3, foreground));
        if (!render) throw std::runtime_error("MicroTeX could not lay out this formula.");
        if (width) *width = render->getWidth();
        if (height) *height = render->getHeight();
        return reinterpret_cast<MicrotexRender *>(render.release());
    } catch (const std::exception &e) {
        detail(error, error_cap, e.what());
    } catch (...) {
        detail(error, error_cap, "MicroTeX could not parse this formula.");
    }
    return nullptr;
}

extern "C" MT_API int microtex_draw(MicrotexRender *render, HDC dc, float x, float y) {
    if (!render || !dc || !std::isfinite(x) || !std::isfinite(y)) return 0;
    try {
        Gdiplus::Graphics graphics(dc);
        tex::Graphics2D_win32 target(&graphics);
        reinterpret_cast<tex::TeXRender *>(render)->draw(target, static_cast<int>(x), static_cast<int>(y));
        return graphics.GetLastStatus() == Gdiplus::Ok;
    } catch (...) {
        return 0;
    }
}

extern "C" MT_API void microtex_destroy(MicrotexRender *render) {
    delete reinterpret_cast<tex::TeXRender *>(render);
}

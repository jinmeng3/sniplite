#pragma once
#include <windows.h>
#include <stdint.h>

#ifdef MICROTEX_BUILD
#define MT_API __declspec(dllexport)
#else
#define MT_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Calls are serialized on the preview UI thread. Resources and renderer state
 * stay loaded for the process lifetime; each formula owns one render handle.
 * All strings are UTF-16. Colors use 0xAARRGGBB. Failure details are caller-owned. */
typedef struct MicrotexRender MicrotexRender;
typedef int (*MicrotexInitialize)(const WCHAR *, WCHAR *, int);
typedef MicrotexRender *(*MicrotexCreate)(const WCHAR *, int, float, uint32_t, int *, int *, WCHAR *, int);
typedef int (*MicrotexDraw)(MicrotexRender *, HDC, float, float);
typedef void (*MicrotexDestroy)(MicrotexRender *);

MT_API int microtex_initialize(const WCHAR *resource_dir, WCHAR *error, int error_cap);
MT_API MicrotexRender *microtex_create(const WCHAR *latex, int max_width, float font_size,
                                      uint32_t foreground, int *width, int *height,
                                      WCHAR *error, int error_cap);
MT_API int microtex_draw(MicrotexRender *render, HDC dc, float x, float y);
MT_API void microtex_destroy(MicrotexRender *render);

#ifdef __cplusplus
}
#endif

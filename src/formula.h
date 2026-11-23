#pragma once
#include "common.h"

enum { FORMULA_GET_OK, FORMULA_GET_CHECK, FORMULA_GET_WRITE, FORMULA_GET_CANCEL, FORMULA_GET_SOURCE };
#define FORMULA_SERVICE_IDLE_MS 60000u
typedef struct { uint32_t w, h; } FormulaRequest;
typedef struct { uint32_t code, bytes; } FormulaReply;

bool formula_installed(void);
bool formula_remove(void);
int formula_get(volatile LONG *cancel, void (*progress)(int percent, void *ctx), void *ctx);
int formula_size_mb(void);
bool formula_run(const uint32_t *bgra, int w, int h, Str *latex);
const WCHAR *formula_error(void);
int formula_helper(int argc, WCHAR **argv);
bool formula_preview_host(HWND owner, const WCHAR *latex, HMONITOR monitor, bool refresh, WCHAR *detail, int cap);

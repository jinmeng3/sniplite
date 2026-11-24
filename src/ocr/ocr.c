/* PP-OCRv6 OCR: prefer Vulkan, retry on CPU when the accelerator fails. */
#include "ocr.h"
#include "config.h"
#include "ocr_engine.h"
#include "resource.h"

#define OCR_DET_MODEL L"PP-OCRv6_tiny_det-f16.gguf"
#define OCR_REC_MODEL L"PP-OCRv6_tiny_rec-f16.gguf"
#define OCR_MAX_LINES 10000
#define BAND_AREA 1500000
#define BAND_LAP 160

static struct {
    HMODULE dll;
    HMODULE runtime[4];
    void *ctx;
    SnipliteOcrInit init;
    SnipliteOcrFree free;
    SnipliteOcrRunBgra run;
    WCHAR err[200];
    int backend;
} G;

const WCHAR *ocr_error(void) { return G.err; }

static bool fail(const WCHAR *message)
{
    lstrcpynW(G.err, message, COUNTOF(G.err));
    return false;
}

void ocr_dir(WCHAR *dir, int cap)
{
    DWORD n = GetModuleFileNameW(NULL, dir, cap - 16);
    if (!n || n >= (DWORD)(cap - 16)) {
        if (cap) dir[0] = 0;
        return;
    }
    while (n && dir[n - 1] != L'\\' && dir[n - 1] != L'/') n--;
    lstrcpynW(dir + n, L"ocr", cap - (int)n);
}
static bool sibling_dir(const WCHAR *name, WCHAR *dir, int cap)
{
    DWORD n = GetModuleFileNameW(NULL, dir, cap - 16);
    if (!n || n >= (DWORD)(cap - 16)) return false;
    while (n && dir[n - 1] != L'\\' && dir[n - 1] != L'/') n--;
    lstrcpynW(dir + n, name, cap - (int)n);
    return true;
}
static bool app_dir(WCHAR *dir, int cap)
{
    DWORD n = GetModuleFileNameW(NULL, dir, cap);
    if (!n || n >= (DWORD)cap) return false;
    while (n && dir[n - 1] != L'\\' && dir[n - 1] != L'/') n--;
    if (n && (dir[n - 1] == L'\\' || dir[n - 1] == L'/')) n--;
    dir[n] = 0;
    return true;
}


static bool path_in(const WCHAR *dir, const WCHAR *name, WCHAR *path, int cap)
{
    if (!dir || !*dir || lstrlenW(dir) + lstrlenW(name) + 2 >= cap)
        return false;
    wsprintfW(path, L"%s\\%s", dir, name);
    WIN32_FILE_ATTRIBUTE_DATA data;
    return GetFileAttributesExW(path, GetFileExInfoStandard, &data) &&
           !(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
}

static bool shared_runtime_dir(const WCHAR *module, const WCHAR *backend, WCHAR *dir, int cap)
{
    if (!module || !*module || lstrlenW(module) + lstrlenW(backend) + 16 >= cap) return false;
    wsprintfW(dir, L"%s\\shared-runtime\\%s", module, backend);
    DWORD attr = GetFileAttributesW(dir);
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY);
}

static bool runtime_core(const WCHAR *dir, bool vulkan)
{
    WCHAR path[MAX_PATH * 2];
    return path_in(dir, L"ggml.dll", path, COUNTOF(path)) &&
           path_in(dir, L"ggml-base.dll", path, COUNTOF(path)) &&
           path_in(dir, L"ggml-cpu.dll", path, COUNTOF(path)) &&
           (!vulkan || path_in(dir, L"ggml-vulkan.dll", path, COUNTOF(path)));
}

static bool runtime_aux(const WCHAR *dir)
{
    static const WCHAR *const names[] = {
        L"crispembed.dll", L"vcruntime140.dll", L"vcruntime140_1.dll", L"msvcp140_1.dll", L"msvcp140.dll"
    };
    WCHAR path[MAX_PATH * 2];
    for (int i = 0; i < COUNTOF(names); ++i)
        if (!path_in(dir, names[i], path, COUNTOF(path))) return false;
    return true;
}

bool ocr_installed(void)
{
    static const WCHAR *const models[] = {OCR_DET_MODEL, OCR_REC_MODEL};
    WCHAR dir[MAX_PATH * 2], package[MAX_PATH * 2], formula[MAX_PATH * 2], path[MAX_PATH * 2];
    WCHAR package_generic[MAX_PATH * 2], package_vulkan[MAX_PATH * 2];
    WCHAR own_generic[MAX_PATH * 2], own_vulkan[MAX_PATH * 2];
    WCHAR formula_generic[MAX_PATH * 2], formula_vulkan[MAX_PATH * 2];
    ocr_dir(dir, COUNTOF(dir));
    if (!dir[0]) return false;
    bool vulkan_engine = path_in(dir, L"sniplite_ocr_vulkan.dll", path, COUNTOF(path));
    for (int i = 0; i < COUNTOF(models); ++i)
        if (!path_in(dir, models[i], path, COUNTOF(path))) return false;
    bool have_package = app_dir(package, COUNTOF(package));
    bool have_formula = sibling_dir(L"formula", formula, COUNTOF(formula));
    bool package_gen = have_package && shared_runtime_dir(package, L"cpu-generic", package_generic, COUNTOF(package_generic));
    bool package_vk = have_package && shared_runtime_dir(package, L"vulkan", package_vulkan, COUNTOF(package_vulkan));
    bool own_gen = shared_runtime_dir(dir, L"cpu-generic", own_generic, COUNTOF(own_generic));
    bool own_vk = shared_runtime_dir(dir, L"vulkan", own_vulkan, COUNTOF(own_vulkan));
    bool formula_gen = have_formula && shared_runtime_dir(formula, L"cpu-generic", formula_generic, COUNTOF(formula_generic));
    bool formula_vk = have_formula && shared_runtime_dir(formula, L"vulkan", formula_vulkan, COUNTOF(formula_vulkan));
    bool generic_core = (package_gen && runtime_core(package_generic, false)) ||
                        (own_gen && runtime_core(own_generic, false)) ||
                        (formula_gen && runtime_core(formula_generic, false)) || runtime_core(dir, false) ||
                        (have_formula && runtime_core(formula, false));
    bool vulkan_core = (package_vk && runtime_core(package_vulkan, true)) ||
                       (own_vk && runtime_core(own_vulkan, true)) ||
                       (formula_vk && runtime_core(formula_vulkan, true)) || runtime_core(dir, true) ||
                       (have_formula && runtime_core(formula, true));
    bool auxiliary = (package_gen && runtime_aux(package_generic)) || (package_vk && runtime_aux(package_vulkan)) ||
                     (own_gen && runtime_aux(own_generic)) || (formula_gen && runtime_aux(formula_generic)) ||
                     (own_vk && runtime_aux(own_vulkan)) || (formula_vk && runtime_aux(formula_vulkan)) ||
                     runtime_aux(dir) || (have_formula && runtime_aux(formula));
    return auxiliary && (vulkan_engine ? vulkan_core : generic_core || vulkan_core);
}

static bool path_utf8(const WCHAR *path, char **out)
{
    int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path, -1, NULL, 0, NULL, NULL);
    char *p = n > 0 ? mem_alloc((size_t)n) : NULL;
    if (!p || !WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path, -1, p, n, NULL, NULL)) {
        mem_free(p);
        return fail(L"Could not encode an OCR path as UTF-8");
    }
    *out = p;
    return true;
}

static void unload(void)
{
    if (G.ctx && G.free) G.free(G.ctx);
    if (G.dll) FreeLibrary(G.dll);
    for (int i = COUNTOF(G.runtime) - 1; i >= 0; --i) {
        if (G.runtime[i]) FreeLibrary(G.runtime[i]);
        G.runtime[i] = NULL;
    }
    G.ctx = NULL;
    G.dll = NULL;
    G.init = NULL;
    G.free = NULL;
    G.run = NULL;
}
void ocr_set_backend(int backend)
{
    if ((unsigned)backend >= INFER_COUNT) backend = INFER_AUTO;
    if (G.backend == backend) return;
    unload();
    G.backend = backend;
}

/* Existing model installations receive the current embedded engine on first use.
 * Compare bytes before publishing so read-only, up-to-date portable folders work. */
static bool extract_engine_file(const WCHAR *dir, const WCHAR *name, int id)
{
    HRSRC resource = FindResourceW(NULL, MAKEINTRESOURCEW(id), RT_RCDATA);
    HGLOBAL loaded = resource ? LoadResource(NULL, resource) : NULL;
    const void *data = loaded ? LockResource(loaded) : NULL;
    DWORD bytes = resource ? SizeofResource(NULL, resource) : 0;
    if (!data || !bytes) return fail(L"The embedded OCR engine is missing");
    WCHAR path[MAX_PATH * 2], temporary[MAX_PATH * 2];
    if (lstrlenW(dir) + lstrlenW(name) + 7 >= COUNTOF(path))
        return fail(L"The OCR module path is too long");
    wsprintfW(path, L"%s\\%s", dir, name);
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    bool matches = false;
    if (file != INVALID_HANDLE_VALUE) {
        LARGE_INTEGER size;
        if (GetFileSizeEx(file, &size) && size.QuadPart == bytes) {
            HANDLE mapping = CreateFileMappingW(file, NULL, PAGE_READONLY, 0, 0, NULL);
            const void *view = mapping ? MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, bytes) : NULL;
            matches = view && !memcmp(view, data, bytes);
            if (view) UnmapViewOfFile(view);
            if (mapping) CloseHandle(mapping);
        }
        CloseHandle(file);
    }
    if (matches) return true;
    wsprintfW(temporary, L"%s.part", path);
    file = CreateFileW(temporary, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return fail(L"Could not write the current OCR engine");
    DWORD written = 0;
    bool ok = WriteFile(file, data, bytes, &written, NULL) && written == bytes;
    if (!CloseHandle(file)) ok = false;
    if (ok) ok = MoveFileExW(temporary, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    if (!ok) DeleteFileW(temporary);
    return ok || fail(L"Could not install the current OCR engine");
}

static bool load_backend(const WCHAR *engine, const WCHAR *runtime_dir, const WCHAR *fallback_dir,
                         bool shared, bool vulkan_engine)
{
    static const WCHAR *const runtime[] = {
        L"ggml.dll", L"ggml-cpu.dll", L"ggml-vulkan.dll", L"crispembed.dll"
    };
    int count = vulkan_engine ? 4 : 3;
    unload();
    for (int i = 0; i < count; ++i) {
        const WCHAR *name = !vulkan_engine && i == 2 ? runtime[3] : runtime[i];
        WCHAR path[MAX_PATH * 2];
        if (!path_in(runtime_dir, name, path, COUNTOF(path)) &&
            !(shared && path_in(fallback_dir, name, path, COUNTOF(path)))) {
            unload();
            return false;
        }
        G.runtime[i] = LoadLibraryExW(path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!G.runtime[i]) {
            unload();
            return false;
        }
    }
    G.dll = LoadLibraryExW(engine, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!G.dll) {
        unload();
        return false;
    }
    G.init = (SnipliteOcrInit)(void *)GetProcAddress(G.dll, "sniplite_ocr_init");
    G.free = (SnipliteOcrFree)(void *)GetProcAddress(G.dll, "sniplite_ocr_free");
    G.run = (SnipliteOcrRunBgra)(void *)GetProcAddress(G.dll, "sniplite_ocr_run_bgra");
    if (!G.init || !G.free || !G.run) {
        unload();
        return false;
    }
    return true;
}

bool ocr_load(const WCHAR *dir, int threads)
{
    if (!dir && !ocr_installed() && ocr_packed() && !ocr_unpack())
        return fail(L"The embedded OCR module could not be extracted");
    if (G.ctx) return true;
    bool default_dir = !dir;
    WCHAR own[MAX_PATH * 2], package[MAX_PATH * 2], formula[MAX_PATH * 2], path[MAX_PATH * 2], engine_path[MAX_PATH * 2];
    WCHAR avx2_dir[MAX_PATH * 2], avx2_engine[MAX_PATH * 2];
    WCHAR package_generic[MAX_PATH * 2], package_vulkan[MAX_PATH * 2], package_avx2[MAX_PATH * 2];
    WCHAR own_generic[MAX_PATH * 2], own_vulkan[MAX_PATH * 2], own_avx2[MAX_PATH * 2];
    WCHAR formula_generic[MAX_PATH * 2], formula_vulkan[MAX_PATH * 2], formula_avx2[MAX_PATH * 2];
    WCHAR det[MAX_PATH * 2], rec[MAX_PATH * 2];
    const WCHAR *generic_runtime = NULL, *generic_fallback = NULL;
    const WCHAR *vulkan_runtime = NULL, *vulkan_fallback = NULL;
    const WCHAR *avx2_runtime = NULL, *avx2_fallback = NULL;
    bool have_package = default_dir && app_dir(package, COUNTOF(package));
    bool have_formula = default_dir && sibling_dir(L"formula", formula, COUNTOF(formula));
    if (!have_formula) formula[0] = 0;
    if (default_dir) {
        ocr_dir(own, COUNTOF(own));
        dir = own;
    }
    if (!path_in(dir, OCR_DET_MODEL, det, COUNTOF(det)) || !path_in(dir, OCR_REC_MODEL, rec, COUNTOF(rec)))
        return fail(L"A PP-OCRv6 GGUF model is missing");
    bool vulkan_engine = path_in(dir, L"sniplite_ocr_vulkan.dll", engine_path, COUNTOF(engine_path));
    if ((!vulkan_engine &&
         (!extract_engine_file(dir, L"sniplite_ocr.dll", IDR_OCR_ENGINE) ||
          !path_in(dir, L"sniplite_ocr.dll", engine_path, COUNTOF(engine_path)))) ||
        !extract_engine_file(dir, L"LICENSE-OCREngine.txt", IDR_OCR_ENGINE_LICENSE)) return false;
    if (default_dir) {
        bool package_gen = have_package && shared_runtime_dir(package, L"cpu-generic", package_generic, COUNTOF(package_generic));
        bool package_vk = have_package && shared_runtime_dir(package, L"vulkan", package_vulkan, COUNTOF(package_vulkan));
        bool package_avx = have_package && shared_runtime_dir(package, L"cpu-avx2", package_avx2, COUNTOF(package_avx2));
        bool own_gen = shared_runtime_dir(dir, L"cpu-generic", own_generic, COUNTOF(own_generic));
        bool own_vk = shared_runtime_dir(dir, L"vulkan", own_vulkan, COUNTOF(own_vulkan));
        bool own_avx = shared_runtime_dir(dir, L"cpu-avx2", own_avx2, COUNTOF(own_avx2));
        bool formula_gen = have_formula && shared_runtime_dir(formula, L"cpu-generic", formula_generic, COUNTOF(formula_generic));
        bool formula_vk = have_formula && shared_runtime_dir(formula, L"vulkan", formula_vulkan, COUNTOF(formula_vulkan));
        bool formula_avx = have_formula && shared_runtime_dir(formula, L"cpu-avx2", formula_avx2, COUNTOF(formula_avx2));
        if (package_gen && runtime_core(package_generic, false)) generic_runtime = package_generic;
        else if (formula_gen && runtime_core(formula_generic, false)) generic_runtime = formula_generic;
        else if (own_gen && runtime_core(own_generic, false)) generic_runtime = own_generic;
        else if (package_vk && runtime_core(package_vulkan, true)) generic_runtime = package_vulkan;
        else if (formula_vk && runtime_core(formula_vulkan, true)) generic_runtime = formula_vulkan;
        else if (own_vk && runtime_core(own_vulkan, true)) generic_runtime = own_vulkan;
        else generic_runtime = dir;
        generic_fallback = package_gen && generic_runtime != package_generic ? package_generic :
                           formula_gen && generic_runtime != formula_generic ? formula_generic :
                           own_gen && generic_runtime != own_generic ? own_generic :
                           have_formula ? formula : dir;
        if (package_vk && runtime_core(package_vulkan, true)) vulkan_runtime = package_vulkan;
        else if (formula_vk && runtime_core(formula_vulkan, true)) vulkan_runtime = formula_vulkan;
        else if (own_vk && runtime_core(own_vulkan, true)) vulkan_runtime = own_vulkan;
        else vulkan_runtime = dir;
        vulkan_fallback = generic_runtime;
        if (package_avx && path_in(package_avx2, L"ggml-cpu.dll", path, COUNTOF(path))) avx2_runtime = package_avx2;
        else if (formula_avx && path_in(formula_avx2, L"ggml-cpu.dll", path, COUNTOF(path))) avx2_runtime = formula_avx2;
        else if (own_avx && path_in(own_avx2, L"ggml-cpu.dll", path, COUNTOF(path))) avx2_runtime = own_avx2;
        else if (have_formula && lstrlenW(formula) + 10 < COUNTOF(avx2_dir)) {
            wsprintfW(avx2_dir, L"%s\\cpu-avx2", formula);
            if (path_in(avx2_dir, L"ggml-cpu.dll", path, COUNTOF(path))) avx2_runtime = avx2_dir;
        }
        if (!avx2_runtime && lstrlenW(dir) + 10 < COUNTOF(avx2_dir)) {
            wsprintfW(avx2_dir, L"%s\\cpu-avx2", dir);
            if (path_in(avx2_dir, L"ggml-cpu.dll", path, COUNTOF(path))) avx2_runtime = avx2_dir;
        }
        avx2_fallback = generic_runtime;
    } else {
        generic_runtime = dir;
        generic_fallback = NULL;
        vulkan_runtime = dir;
        vulkan_fallback = NULL;
    }
    bool try_vulkan = G.backend == INFER_AUTO || G.backend == INFER_VULKAN;
    bool try_avx2 = G.backend == INFER_AUTO || G.backend == INFER_AVX2;
    bool avx2 = try_avx2 && default_dir && IsProcessorFeaturePresent(PF_AVX2_INSTRUCTIONS_AVAILABLE) &&
                avx2_runtime && lstrlenW(dir) + 10 < COUNTOF(avx2_dir);
    if (avx2) {
        wsprintfW(avx2_dir, L"%s\\cpu-avx2", dir);
        avx2 = path_in(avx2_dir, L"sniplite_ocr.dll", avx2_engine, COUNTOF(avx2_engine));
    }
    int selected = -1;
    if (try_vulkan && vulkan_engine && load_backend(engine_path, vulkan_runtime, vulkan_fallback, default_dir, true))
        selected = 2;
    if (selected < 0 && avx2 && load_backend(avx2_engine, avx2_runtime, avx2_fallback, true, false)) selected = 1;
    if (selected < 0) {
        if (vulkan_engine &&
            (!extract_engine_file(dir, L"sniplite_ocr.dll", IDR_OCR_ENGINE) ||
             !path_in(dir, L"sniplite_ocr.dll", engine_path, COUNTOF(engine_path))))
            return false;
        if (load_backend(engine_path, generic_runtime, generic_fallback, default_dir, false)) selected = 0;
    }
    if (selected < 0) return fail(L"No OCR engine or GGML runtime could be loaded");
    char *det8 = NULL, *rec8 = NULL;
    if (!path_utf8(det, &det8) || !path_utf8(rec, &rec8)) {
        mem_free(det8);
        mem_free(rec8);
        return false;
    }
    DWORD ncpu = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    int nthreads = threads > 0 ? threads : (ncpu > 4 ? 4 : ncpu ? (int)ncpu : 1);
    G.ctx = G.init(det8, rec8, nthreads);
    if (!G.ctx && selected == 2 && avx2 && load_backend(avx2_engine, avx2_runtime, avx2_fallback, true, false)) {
        selected = 1;
        G.ctx = G.init(det8, rec8, nthreads);
    }
    if (!G.ctx && selected > 0) {
        if (extract_engine_file(dir, L"sniplite_ocr.dll", IDR_OCR_ENGINE) &&
            path_in(dir, L"sniplite_ocr.dll", engine_path, COUNTOF(engine_path)) &&
            load_backend(engine_path, generic_runtime, generic_fallback, default_dir, false)) {
            selected = 0;
            G.ctx = G.init(det8, rec8, nthreads);
        }
    }
    mem_free(det8);
    mem_free(rec8);
    if (!G.ctx) {
        unload();
        return fail(L"CrispEmbed could not initialize the PP-OCRv6 models");
    }
    G.err[0] = 0;
    return true;
}


static bool append_result(OcrOut *out, const SnipliteOcrResult *r, int w, int h, WCHAR *text, int cap)
{
    if (!r->text || r->text_len <= 0 || r->text_len > 32768) return true;
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, r->text, r->text_len, NULL, 0);
    if (n <= 0 || n > cap || out->n >= OCR_MAX_LINES) return true;
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, r->text, r->text_len, text, cap) != n) return true;
    if (!(r->x >= -w && r->x <= 2.0f * w && r->y >= -h && r->y <= 2.0f * h &&
          r->w >= 0 && r->w <= 2.0f * w && r->h >= 0 && r->h <= 2.0f * h)) return true;
    float right_x = r->x + r->w, bottom_y = r->y + r->h;
    int left = r->x <= 0 ? 0 : r->x >= w ? w : (int)(r->x + 0.5f);
    int top = r->y <= 0 ? 0 : r->y >= h ? h : (int)(r->y + 0.5f);
    int right = right_x <= 0 ? 0 : right_x >= w ? w : (int)(right_x + 0.5f);
    int bottom = bottom_y <= 0 ? 0 : bottom_y >= h ? h : (int)(bottom_y + 0.5f);
    right = MAXI(right, left);
    bottom = MAXI(bottom, top);
    if (right <= left || bottom <= top) return true;
    OcrLine *line = &out->line[out->n];
    line->rc = (RECT){left, top, right, bottom};
    float score = r->confidence;
    if (!(score >= 0)) score = 0;
    if (score > 1) score = 1;
    line->score = score;
    line->text = out->chars.n;
    line->len = n;
    line->row = 0;
    str_addn(&out->chars, text, n);
    if (out->chars.n != line->text + n) return fail(L"Out of memory");
    out->n++;
    return true;
}

static bool run_band(const uint32_t *px, int stride, int w, int h, OcrOut *out)
{
    memset(out, 0, sizeof *out);
    if (w < 1 || h < 1) return true;
    if (!G.ctx && !ocr_load(NULL, 0)) return false;
    if (!px || stride < w) return fail(L"The OCR image dimensions are invalid");
    const size_t stride_bytes = (size_t)stride * sizeof *px;
    const size_t data_bytes = ((size_t)(h - 1) * stride + w) * sizeof *px;
    double start = now_ms();
    int count = 0;
    const SnipliteOcrResult *results = G.run(G.ctx, (const unsigned char *)px, w, h,
                                           stride_bytes, data_bytes, &count);
    if (count < 0 || count > OCR_MAX_LINES || (count && !results)) {
        return fail(L"The native engine returned an invalid OCR result");
    }
    int max_chars = 1;
    for (int i = 0; i < count; i++) {
        if (!results[i].text || results[i].text_len <= 0 || results[i].text_len > 32768) continue;
        int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, results[i].text, results[i].text_len, NULL, 0);
        if (n > max_chars && n <= 32768) max_chars = n;
    }
    out->line = count ? mem_alloc((size_t)count * sizeof(OcrLine)) : NULL;
    WCHAR *text = mem_alloc((size_t)max_chars * sizeof(WCHAR));
    if ((count && !out->line) || !text) {
        mem_free(text);
        ocr_free(out);
        return fail(L"Out of memory");
    }
    bool good = true;
    for (int i = 0; good && i < count; i++) good = append_result(out, &results[i], w, h, text, max_chars);
    mem_free(text);
    out->ms_find = now_ms() - start;
    if (!good) ocr_free(out);
    return good;
}

static void sort_by(int *idx, int n, const float *key)
{
    for (int i = 1; i < n; i++) {
        int v = idx[i], j = i;
        for (; j > 0 && key[idx[j - 1]] > key[v]; j--) idx[j] = idx[j - 1];
        idx[j] = v;
    }
}

static bool arrange(OcrOut *o)
{
    int n = o->n;
    if (n < 2) return true;
    OcrLine *sorted = mem_alloc((size_t)n * (sizeof(OcrLine) + sizeof(int) + sizeof(float)));
    if (!sorted) {
        ocr_free(o);
        return fail(L"Out of memory");
    }
    int *idx = (int *)(sorted + n);
    float *key = (float *)(idx + n);
    for (int i = 0; i < n; i++) {
        idx[i] = i;
        key[i] = (float)(o->line[i].rc.top + o->line[i].rc.bottom);
    }
    sort_by(idx, n, key);
    int row = 0;
    for (int i = 0; i < n;) {
        const RECT *a = &o->line[idx[i]].rc;
        int top = a->top, bottom = a->bottom, j = i + 1;
        for (; j < n; j++) {
            const RECT *b = &o->line[idx[j]].rc;
            int overlap = MINI(bottom, b->bottom) - MAXI(top, b->top);
            if (overlap * 2 < MINI(bottom - top, b->bottom - b->top)) break;
        }
        for (int k = i; k < j; k++) key[idx[k]] = (float)o->line[idx[k]].rc.left;
        sort_by(idx + i, j - i, key);
        for (int k = i; k < j; k++) {
            sorted[k] = o->line[idx[k]];
            sorted[k].row = row;
        }
        row++;
        i = j;
    }
    memcpy(o->line, sorted, (size_t)n * sizeof(OcrLine));
    mem_free(sorted);
    return true;
}

bool ocr_run(const uint32_t *px, int stride, int w, int h, float screen, OcrOut *out)
{
    (void)screen;
    if (!out) return false;
    memset(out, 0, sizeof *out);
    if (!px || w <= 0 || h <= 0 || stride < w) return fail(L"The OCR image dimensions are invalid");
    int band = CLAMPI(BAND_AREA / MAXI(w, 1), 512, 2400);
    if (h <= band + BAND_LAP) return run_band(px, stride, w, h, out) && arrange(out);
    for (int y0 = 0;; y0 += band - BAND_LAP) {
        int y1 = MINI(y0 + band, h);
        bool last = y1 == h;
        OcrOut part;
        if (!run_band(px + (size_t)y0 * stride, stride, w, y1 - y0, &part)) {
            ocr_free(out);
            return false;
        }
        int from = y0 ? y0 + BAND_LAP / 2 : 0;
        int to = last ? h : y1 - BAND_LAP / 2;
        for (int i = 0; i < part.n; i++) {
            OcrLine line = part.line[i];
            int mid = y0 + (line.rc.top + line.rc.bottom) / 2;
            if (mid < from || mid >= to) continue;
            line.rc.top += y0;
            line.rc.bottom += y0;
            line.text = out->chars.n;
            str_addn(&out->chars, part.chars.p + part.line[i].text, line.len);
            if (out->chars.n != line.text + line.len) {
                ocr_free(&part);
                ocr_free(out);
                return fail(L"Out of memory");
            }
            OcrLine *more = mem_grow(out->line, (size_t)(out->n + 1) * sizeof(OcrLine));
            if (!more) {
                ocr_free(&part);
                ocr_free(out);
                return fail(L"Out of memory");
            }
            out->line = more;
            out->line[out->n++] = line;
        }
        out->ms_find += part.ms_find;
        ocr_free(&part);
        if (last) break;
    }
    return arrange(out);
}

static bool cjk_char(WCHAR c)
{
    return c >= 0x2E80 && c <= 0x9FFF;
}

static bool word_char(WCHAR c)
{
    return (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z') || (c >= L'0' && c <= L'9');
}

void ocr_text(const OcrOut *o, WCHAR between, Str *s)
{
    for (int i = 0; i < o->n; i++) {
        const OcrLine *line = &o->line[i];
        if (i) str_addn(s, line->row == o->line[i - 1].row ? &between : L"\r\n", line->row == o->line[i - 1].row ? 1 : 2);
        const WCHAR *text = o->chars.p + line->text;
        for (int j = 0; j < line->len; j++) {
            if (j && ((cjk_char(text[j - 1]) && word_char(text[j])) ||
                      (word_char(text[j - 1]) && cjk_char(text[j]))))
                str_add(s, L" ");
            str_addn(s, text + j, 1);
        }
    }
}

void ocr_free(OcrOut *o)
{
    if (!o) return;
    mem_free(o->line);
    str_free(&o->chars);
    o->line = NULL;
    o->n = 0;
}

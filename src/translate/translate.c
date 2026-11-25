#include "translate.h"
#include "../app/config.h"
#include "../ocr/ocr.h"

#define TRANSLATE_DLL L"sniplite_translate.dll"
#define TRANSLATE_MODEL L"opus-mt-en-zh-q8_0.gguf"
typedef void *(*TranslateInit)(const char *, const char *, int);
typedef int (*TranslateRun)(void *, const char *, char *, int);
typedef void (*TranslateFree)(void *);

static struct {
    HMODULE dll;
    void *ctx;
    TranslateRun run;
    TranslateFree free;
    int backend;
} T;

static bool module_path(const WCHAR *name, WCHAR *path, int cap)
{
    DWORD n = GetModuleFileNameW(NULL, path, cap - 32);
    if (!n || n >= (DWORD)(cap - 32))
        return false;
    while (n && path[n - 1] != L'\\' && path[n - 1] != L'/') n--;
    if (lstrlenW(path + n) + lstrlenW(name) + 1 >= cap)
        return false;
    lstrcatW(path, name);
    return true;
}

static bool path_exists(const WCHAR *path)
{
    DWORD attr = GetFileAttributesW(path);
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

static bool translation_paths(int backend, WCHAR *dll, int dll_cap, WCHAR *model, int model_cap)
{
    const WCHAR *variant = backend == INFER_VULKAN ? L"vulkan" :
                           backend == INFER_AVX2 ? L"cpu-avx2" : L"";
    WCHAR rel[MAX_PATH];
    lstrcpyW(rel, L"translation\\");
    if (variant[0]) {
        if (lstrlenW(rel) + lstrlenW(variant) + 2 >= COUNTOF(rel)) return false;
        lstrcatW(rel, variant);
        lstrcatW(rel, L"\\");
    }
    if (lstrlenW(rel) + lstrlenW(TRANSLATE_DLL) + 1 >= COUNTOF(rel)) return false;
    lstrcatW(rel, TRANSLATE_DLL);
    if (!module_path(rel, dll, dll_cap)) return false;

    lstrcpyW(rel, L"translation\\");
    if (lstrlenW(rel) + lstrlenW(TRANSLATE_MODEL) + 1 >= COUNTOF(rel)) return false;
    lstrcatW(rel, TRANSLATE_MODEL);
    return module_path(rel, model, model_cap);
}

bool translate_installed(void)
{
    int backend = ocr_backend_selected();
    WCHAR dll[MAX_PATH * 2], model[MAX_PATH * 2], runtime[MAX_PATH * 2];
    if (backend == INFER_AUTO || !ocr_runtime_dir(runtime, COUNTOF(runtime)) ||
        !translation_paths(backend, dll, COUNTOF(dll), model, COUNTOF(model)) ||
        !path_exists(dll) || !path_exists(model))
        return false;
    WCHAR path[MAX_PATH * 2];
    static const WCHAR *const cpu_files[] = {L"ggml.dll", L"ggml-base.dll", L"ggml-cpu.dll"};
    static const WCHAR *const vulkan_files[] = {L"ggml.dll", L"ggml-base.dll", L"ggml-cpu.dll", L"ggml-vulkan.dll"};
    const WCHAR *const *runtime_files = backend == INFER_VULKAN ? vulkan_files : cpu_files;
    int runtime_count = backend == INFER_VULKAN ? COUNTOF(vulkan_files) : COUNTOF(cpu_files);
    for (int i = 0; i < runtime_count; i++) {
        lstrcpynW(path, runtime, COUNTOF(path));
        if (lstrlenW(path) + lstrlenW(runtime_files[i]) + 2 >= COUNTOF(path)) return false;
        lstrcatW(path, L"\\");
        lstrcatW(path, runtime_files[i]);
        if (!path_exists(path)) return false;
    }
    return true;
}

static bool translate_load(void)
{
    int backend = ocr_backend_selected();
    if (T.ctx && T.backend == backend) return true;
    if (T.ctx) translate_unload();
    WCHAR dll[MAX_PATH * 2], model[MAX_PATH * 2], runtime[MAX_PATH * 2];
    if (backend == INFER_AUTO || !ocr_runtime_dir(runtime, COUNTOF(runtime)) ||
        !translation_paths(backend, dll, COUNTOF(dll), model, COUNTOF(model)) || !translate_installed())
        return false;

    int model_bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, model, -1, NULL, 0, NULL, NULL);
    int runtime_bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, runtime, -1, NULL, 0, NULL, NULL);
    char *model_utf8 = model_bytes > 0 ? mem_alloc((size_t)model_bytes) : NULL;
    char *runtime_utf8 = runtime_bytes > 0 ? mem_alloc((size_t)runtime_bytes) : NULL;
    if (!model_utf8 || !runtime_utf8 ||
        !WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, model, -1, model_utf8, model_bytes, NULL, NULL) ||
        !WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, runtime, -1, runtime_utf8, runtime_bytes, NULL, NULL)) {
        mem_free(model_utf8);
        mem_free(runtime_utf8);
        return false;
    }

    WCHAR old_dir[MAX_PATH * 2];
    DWORD old_len = GetDllDirectoryW(COUNTOF(old_dir), old_dir);
    bool have_old = old_len > 0 && old_len < COUNTOF(old_dir);
    if (!SetDllDirectoryW(runtime)) {
        mem_free(model_utf8);
        mem_free(runtime_utf8);
        return false;
    }
    HMODULE dll_module = LoadLibraryExW(dll, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    SetDllDirectoryW(have_old ? old_dir : NULL);
    if (!dll_module) {
        mem_free(model_utf8);
        mem_free(runtime_utf8);
        return false;
    }

    TranslateInit init = (TranslateInit)(void *)GetProcAddress(dll_module, "sniplite_opusmt_init");
    TranslateRun run = (TranslateRun)(void *)GetProcAddress(dll_module, "sniplite_opusmt_translate");
    TranslateFree free_ctx = (TranslateFree)(void *)GetProcAddress(dll_module, "sniplite_opusmt_free");
    if (!init || !run || !free_ctx) {
        mem_free(model_utf8);
        mem_free(runtime_utf8);
        FreeLibrary(dll_module);
        return false;
    }
    DWORD processors = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    int threads = processors > 4 ? 4 : processors ? (int)processors : 1;
    void *ctx = init(model_utf8, runtime_utf8, threads);
    mem_free(model_utf8);
    mem_free(runtime_utf8);
    if (!ctx) {
        FreeLibrary(dll_module);
        return false;
    }

    T.dll = dll_module;
    T.ctx = ctx;
    T.run = run;
    T.free = free_ctx;
    T.backend = backend;
    return true;
}

bool translate_text(const WCHAR *text, Str *translated)
{
    if (!text || !*text || !translated || !translate_load()) return false;
    int input_bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1, NULL, 0, NULL, NULL);
    if (input_bytes <= 1) return false;
    char *input = mem_alloc((size_t)input_bytes);
    if (!input || !WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1, input,
                                       input_bytes, NULL, NULL)) {
        mem_free(input);
        return false;
    }

    int capacity = 65536;
    char *output = mem_alloc((size_t)capacity);
    int bytes = output ? T.run(T.ctx, input, output, capacity) : 0;
    mem_free(input);
    if (bytes <= 0 || bytes >= capacity) {
        mem_free(output);
        return false;
    }
    int chars = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, output, bytes, NULL, 0);
    WCHAR *wide = chars > 0 ? mem_alloc(((size_t)chars + 1) * sizeof(WCHAR)) : NULL;
    if (!wide || MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, output, bytes, wide, chars) != chars) {
        mem_free(wide);
        mem_free(output);
        return false;
    }
    wide[chars] = 0;
    str_free(translated);
    str_addn(translated, wide, chars);
    mem_free(wide);
    mem_free(output);
    return translated->n > 0;
}

void translate_unload(void)
{
    if (T.ctx && T.free) T.free(T.ctx);
    if (T.dll) FreeLibrary(T.dll);
    T.ctx = NULL;
    T.dll = NULL;
    T.run = NULL;
    T.free = NULL;
    T.backend = INFER_AUTO;
}

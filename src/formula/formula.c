#pragma GCC optimize("O2")
#include "formula.h"
#include "config.h"

#define FORMULA_MAX_PIXELS 16000000u
#define FORMULA_OUTPUT_CAP 65536

typedef void *(*CrispModelInit)(const char *model_path, int threads);
typedef void (*CrispModelFree)(void *ctx);
typedef const char *(*CrispRecognize)(void *ctx, const uint8_t *pixels, int width,
                                      int height, int channels, int *length);

static struct {
    HMODULE runtime[5];
    void *ctx;
    CrispModelInit model_init;
    CrispModelFree model_free;
    CrispRecognize recognize;
    bool ready, bad;
    WCHAR err[200];
    int backend;
} F;

const WCHAR *formula_error(void) { return F.err; }
static void error(const WCHAR *s) { lstrcpynW(F.err, s, COUNTOF(F.err)); }


static bool find_path(const WCHAR *name, WCHAR *path, int cap)
{
    DWORD n = GetModuleFileNameW(NULL, path, cap - 16);
    if (!n || n >= (DWORD)(cap - 16)) return false;
    while (n && path[n - 1] != L'\\' && path[n - 1] != L'/') n--;
    lstrcpyW(path + n, L"formula\\");
    if (lstrlenW(path) + lstrlenW(name) + 1 >= cap) return false;
    lstrcatW(path, name);
    return true;
}
static bool runtime_path(const WCHAR *name, WCHAR *path, int cap)
{
    return find_path(name, path, cap) && GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

static bool shared_runtime_dir(const WCHAR *backend, WCHAR *dir, int cap)
{
    WCHAR relative[64];
    if (lstrlenW(backend) + 16 >= COUNTOF(relative)) return false;
    wsprintfW(relative, L"shared-runtime\\%s", backend);
    if (!find_path(relative, dir, cap)) return false;
    DWORD attr = GetFileAttributesW(dir);
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY);
}

static bool runtime_file(const WCHAR *dir, const WCHAR *name, WCHAR *path, int cap)
{
    if (!dir || !*dir || lstrlenW(dir) + lstrlenW(name) + 2 >= cap) return false;
    wsprintfW(path, L"%s\\%s", dir, name);
    DWORD attr = GetFileAttributesW(path);
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

static bool runtime_complete(const WCHAR *dir, bool vulkan)
{
    WCHAR path[MAX_PATH * 2];
    return runtime_file(dir, L"ggml.dll", path, COUNTOF(path)) &&
           runtime_file(dir, L"ggml-base.dll", path, COUNTOF(path)) &&
           runtime_file(dir, L"ggml-cpu.dll", path, COUNTOF(path)) &&
           (!vulkan || runtime_file(dir, L"ggml-vulkan.dll", path, COUNTOF(path)));
}

static bool load_runtime(const WCHAR *dir, const WCHAR *fallback, bool vulkan)
{
    static const WCHAR *const names[] = {
        L"ggml-base.dll", L"ggml.dll", L"ggml-cpu.dll", L"ggml-vulkan.dll", L"crispembed.dll"
    };
    int count = vulkan ? 5 : 4;
    for (int i = 0; i < count; ++i) {
        const WCHAR *name = !vulkan && i == 3 ? names[4] : names[i];
        WCHAR path[MAX_PATH * 2];
        bool found = runtime_file(dir, name, path, COUNTOF(path));
        if (!found && fallback) found = runtime_file(fallback, name, path, COUNTOF(path));
        if (!found || !(F.runtime[i] = LoadLibraryExW(path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH)))
            return false;
    }
    return true;
}

static void unload(void)
{
    if (F.ctx && F.model_free) F.model_free(F.ctx);
    if (F.dll) FreeLibrary(F.dll);
    for (int i = COUNTOF(F.runtime) - 1; i >= 0; --i) {
        if (F.runtime[i]) FreeLibrary(F.runtime[i]);
        F.runtime[i] = NULL;
    }
    F.ctx = NULL;
    F.dll = NULL;
    F.model_init = NULL;
    F.model_free = NULL;
    F.recognize = NULL;
    F.ready = false;
}
void formula_set_backend(int backend)
{
    if ((unsigned)backend >= INFER_COUNT) backend = INFER_AUTO;
    if (F.backend == backend) return;
    unload();
    F.backend = backend;
}

static bool load(void)
{
    if (F.ready) return true;
    WCHAR model_path[MAX_PATH * 2], vk_engine[MAX_PATH * 2], avx_engine[MAX_PATH * 2];
    WCHAR generic_engine[MAX_PATH * 2], root_engine[MAX_PATH * 2];
    WCHAR legacy_root[MAX_PATH * 2], legacy_avx[MAX_PATH * 2], legacy_generic[MAX_PATH * 2];
    struct Candidate { const WCHAR *engine, *runtime, *fallback; bool vulkan; } candidate[3];
    int count = 0;
    bool have_vk_engine = runtime_path(L"sniplite_formula_vulkan.dll", vk_engine, COUNTOF(vk_engine));
    bool have_avx_engine = IsProcessorFeaturePresent(PF_AVX2_INSTRUCTIONS_AVAILABLE) &&
                           runtime_path(L"cpu-avx2\\sniplite_formula.dll", avx_engine, COUNTOF(avx_engine));
    bool have_generic_engine = runtime_path(L"cpu-generic\\sniplite_formula.dll", generic_engine, COUNTOF(generic_engine));
    bool have_root_engine = runtime_path(L"sniplite_formula.dll", root_engine, COUNTOF(root_engine));
    bool have_vk_runtime = shared_runtime_dir(L"vulkan", vk_runtime, COUNTOF(vk_runtime)) && runtime_complete(vk_runtime, true);
    bool have_avx_runtime = shared_runtime_dir(L"cpu-avx2", avx_runtime, COUNTOF(avx_runtime)) && runtime_complete(avx_runtime, false);
    bool have_generic_runtime = shared_runtime_dir(L"cpu-generic", generic_runtime, COUNTOF(generic_runtime)) && runtime_complete(generic_runtime, false);
    find_path(L"", legacy_root, COUNTOF(legacy_root));
    find_path(L"cpu-avx2", legacy_avx, COUNTOF(legacy_avx));
    find_path(L"cpu-generic", legacy_generic, COUNTOF(legacy_generic));
    const WCHAR *fallback_runtime = have_generic_runtime ? generic_runtime : have_vk_runtime ? vk_runtime : legacy_root;
    if ((F.backend == INFER_AUTO || F.backend == INFER_VULKAN) && have_vk_engine) {
        const WCHAR *runtime = have_vk_runtime ? vk_runtime : legacy_root;
        if (have_vk_runtime || runtime_complete(runtime, true))
            candidate[count++] = (struct Candidate){vk_engine, runtime, fallback_runtime, true};
    }
    if ((F.backend == INFER_AUTO || F.backend == INFER_AVX2) && have_avx_engine) {
        const WCHAR *runtime = have_avx_runtime ? avx_runtime : legacy_avx;
        if (have_avx_runtime || runtime_complete(runtime, false))
            candidate[count++] = (struct Candidate){avx_engine, runtime, fallback_runtime, false};
    }
    if (have_generic_engine || have_root_engine) {
        const WCHAR *runtime = have_generic_runtime ? generic_runtime : have_vk_runtime ? vk_runtime :
                               have_generic_engine ? legacy_generic : legacy_root;
        if (have_generic_runtime || have_vk_runtime || runtime_complete(runtime, false))
            candidate[count++] = (struct Candidate){have_generic_engine ? generic_engine : root_engine,
                                                     runtime, fallback_runtime, false};
    }
    if (!count) {
        error(L"Formula runtime is not installed");
        goto failed;
    }
    if (!find_path(L"texo-distill-q8_0.gguf", model_path, COUNTOF(model_path))) {
        error(L"Texo-Distill model is not installed");
        goto failed;
    }
    int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, model_path, -1, NULL, 0, NULL, NULL);
    char *model = bytes > 0 ? mem_alloc((size_t)bytes) : NULL;
    if (!model || !WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, model_path, -1, model, bytes, NULL, NULL)) {
        mem_free(model);
        error(L"Could not encode the model path as UTF-8");
        goto failed;
    }
    DWORD processors = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    int threads = processors > 4 ? 4 : (processors ? (int)processors : 1);
    for (int i = 0; i < count; ++i) {
        unload();
        if (!load_runtime(candidate[i].runtime, candidate[i].fallback, candidate[i].vulkan)) continue;
        F.dll = LoadLibraryExW(candidate[i].engine, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!F.dll) continue;
        F.model_init = (CrispModelInit)(void *)GetProcAddress(F.dll, "ppformulanet_ocr_init");
        F.model_free = (CrispModelFree)(void *)GetProcAddress(F.dll, "ppformulanet_ocr_free");
        F.recognize = (CrispRecognize)(void *)GetProcAddress(F.dll, "ppformulanet_ocr_recognize_raw");
        if (!F.model_init || !F.model_free || !F.recognize) continue;
        F.ctx = F.model_init(model, threads);
        if (F.ctx) {
            mem_free(model);
            F.ready = true;
            return true;
        }
    }
    mem_free(model);
    error(L"Texo-Distill model could not be loaded by any available backend");
failed:
    unload();
    return false;
}

static bool run(const uint8_t *rgb, int w, int h, Str *latex)
{
    if (!load()) return false;
    F.bad = true;
    int n = 0;
    const char *text = F.recognize(F.ctx, rgb, w, h, 3, &n);
    if (!text) {
        error(L"Formula recognition failed");
        return false;
    }
    if (n < 0 || n >= FORMULA_OUTPUT_CAP) {
        error(L"CrispEmbed returned an oversized formula");
        return false;
    }
    if (!n) {
        F.bad = false;
        return false;
    }
    int chars = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, n, NULL, 0);
    WCHAR *wide = chars > 0 ? mem_alloc((size_t)chars * sizeof(WCHAR)) : NULL;
    if (!wide || MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, n, wide, chars) != chars) {
        mem_free(wide);
        error(L"CrispEmbed returned invalid UTF-8");
        return false;
    }
    int before = latex->n;
    str_addn(latex, wide, chars);
    mem_free(wide);
    if (latex->n != before + chars) {
        error(L"out of memory");
        return false;
    }
    F.bad = false;
    return true;
}

bool formula_run(const uint32_t *bgra, int w, int h, Str *latex)
{
    if (!latex) return false;
    F.bad = true;
    latex->n = 0;
    if (latex->p) latex->p[0] = 0;
    if (!bgra || w <= 0 || h <= 0 || (uint64_t)w * h > FORMULA_MAX_PIXELS) {
        error(L"Invalid formula image dimensions");
        return false;
    }
    size_t pixels = (size_t)w * h;
    uint8_t *rgb = mem_alloc(pixels * 3);
    if (!rgb) {
        error(L"out of memory");
        return false;
    }
    for (size_t i = 0; i < pixels; i++) {
        uint32_t p = bgra[i];
        rgb[i * 3] = (uint8_t)(p >> 16);
        rgb[i * 3 + 1] = (uint8_t)(p >> 8);
        rgb[i * 3 + 2] = (uint8_t)p;
    }
    bool ok = run(rgb, w, h, latex);
    mem_free(rgb);
    return ok;
}

static bool read_exact(HANDLE h,void *p,uint32_t n)
{uint8_t *b=p;while(n){DWORD got=0;if(!ReadFile(h,b,n,&got,NULL)||!got)return false;b+=got;n-=got;}return true;}
static bool write_all(HANDLE h,const void *p,uint32_t n)
{const uint8_t *b=p;while(n){DWORD wrote=0;if(!WriteFile(h,b,n,&wrote,NULL)||!wrote)return false;b+=wrote;n-=wrote;}return true;}

/* Keep the isolated helper and its initialized CrispEmbed model warm briefly so a
 * second recognition does not reload the model and backend. */
static int formula_service(void)
{
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE), out = GetStdHandle(STD_OUTPUT_HANDLE);
    uint32_t *pixels = NULL;
    size_t cap = 0;
    DWORD idle = GetTickCount();
    for (;;) {
        DWORD available = 0;
        if (!PeekNamedPipe(in, NULL, 0, NULL, &available, NULL))
            break;
        if (!available) {
            if (GetTickCount() - idle >= FORMULA_SERVICE_IDLE_MS)
                break;
            Sleep(50);
            continue;
        }
        FormulaRequest req;
        if (!read_exact(in, &req, sizeof req) || !req.w || !req.h || (uint64_t)req.w * req.h > 16000000)
            break;
        if (req.backend >= INFER_COUNT) req.backend = INFER_AUTO;
        size_t bytes = (size_t)req.w * req.h * sizeof(uint32_t);
        if (bytes > cap) {
            uint32_t *next = mem_alloc(bytes);
            if (!next)
                break;
            mem_free(pixels);
            pixels = next;
            cap = bytes;
        }
        if (!read_exact(in, pixels, (uint32_t)bytes))
            break;
        formula_set_backend((int)req.backend);
        Str latex = {0};
        bool ok = formula_run(pixels, (int)req.w, (int)req.h, &latex);
        int n = ok ? WideCharToMultiByte(CP_UTF8, 0, latex.p, latex.n, NULL, 0, NULL, NULL) : 0;
        char *text = n > 0 ? mem_alloc((size_t)n) : NULL;
        bool encoded = ok && n > 0 && text &&
                       WideCharToMultiByte(CP_UTF8, 0, latex.p, latex.n, text, n, NULL, NULL) == n;
        FormulaReply reply = {encoded ? 0u : ok ? 1u : F.bad ? 1u : 4u, encoded ? (uint32_t)n : 0u};
        bool wrote = write_all(out, &reply, sizeof reply) && (!reply.bytes || write_all(out, text, reply.bytes));
        mem_free(text);
        str_free(&latex);
        if (!wrote)
            break;
        idle = GetTickCount();
    }
    mem_free(pixels);
    return 0;
}

static bool parse_dimension(const WCHAR *s,int *value)
{
    int n=0;if(!*s)return false;
    while(*s){if(*s<L'0'||*s>L'9')return false;int d=*s++-L'0';if(n>(16000000-d)/10)return false;n=n*10+d;}
    if (!n) return false;
    *value=n; return true;
}
int formula_helper(int argc,WCHAR **argv)
{
    Config config;
    config_load(&config);
    formula_set_backend(config.formula_backend);
    if (argc >= 2 && arg_eq(argv[1], L"--formula-worker")) return formula_service();
    if(argc<4||!arg_eq(argv[1],L"--formula-raw"))return 1;
    int w,h;if(!parse_dimension(argv[2],&w)||!parse_dimension(argv[3],&h)||(uint64_t)w*h>16000000)return 1;
    size_t bytes=(size_t)w*h*4;uint32_t *px=mem_alloc(bytes);if(!px)return 1;
    HANDLE in=GetStdHandle(STD_INPUT_HANDLE);if(!read_exact(in,px,(uint32_t)bytes)){mem_free(px);return 4;}
    Str out={0};bool ok=formula_run(px,(int)w,(int)h,&out);mem_free(px);if(!ok){str_free(&out);return F.bad?1:4;}
    int n=WideCharToMultiByte(CP_UTF8,0,out.p,out.n,NULL,0,NULL,NULL);char *u=n>0?mem_alloc(n):NULL;bool wrote=u&&WideCharToMultiByte(CP_UTF8,0,out.p,out.n,u,n,NULL,NULL)==n&&write_all(GetStdHandle(STD_OUTPUT_HANDLE),u,(uint32_t)n);
    mem_free(u);str_free(&out);return wrote?0:1;
}

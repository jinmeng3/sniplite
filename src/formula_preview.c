/* In-process KaTeX preview hosted by the installed WebView2 Runtime. */
#define CINTERFACE
#define COBJMACROS
#include "common.h"
#include "overlay.h"
#include "formula.h"
#include "lang.h"
#include "resource.h"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#include <WebView2.h>
#pragma GCC diagnostic pop

#define PREVIEW_CLASS L"sniplite.formula.preview"
#define PREVIEW_URL_CAP 32768
#define PREVIEW_WIDTH 800
#define PREVIEW_HEIGHT 600

typedef HRESULT (STDAPICALLTYPE *CreateEnvironmentFn)(PCWSTR, PCWSTR, ICoreWebView2EnvironmentOptions *, ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *);

typedef struct {
    HWND window;
    ICoreWebView2Environment *environment;
    ICoreWebView2Controller *controller;
    ICoreWebView2 *view;
    HMODULE loader;
    WCHAR url[PREVIEW_URL_CAP];
    bool creating, com_initialized;
    unsigned controller_requests;
} Preview;

static Preview P;

static void preview_release_if_idle(void)
{
    if (P.window || P.creating || P.controller_requests) return;
    if (P.environment) {
        ICoreWebView2Environment_Release(P.environment);
        P.environment = NULL;
    }
    if (P.com_initialized) {
        CoUninitialize();
        P.com_initialized = false;
    }
    if (P.loader) {
        FreeLibrary(P.loader);
        P.loader = NULL;
    }
}

static bool resource_file(int id, const WCHAR *path)
{
    HRSRC r = FindResourceW(NULL, MAKEINTRESOURCEW(id), RT_RCDATA);
    HGLOBAL g = r ? LoadResource(NULL, r) : NULL;
    const void *data = g ? LockResource(g) : NULL;
    DWORD size = r ? SizeofResource(NULL, r) : 0, written = 0;
    HANDLE f = data && size ? CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL) : INVALID_HANDLE_VALUE;
    if (f == INVALID_HANDLE_VALUE) return false;
    bool ok = WriteFile(f, data, size, &written, NULL) && written == size;
    CloseHandle(f);
    return ok;
}

typedef struct {
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler iface;
    LONG refs;
    HWND window;
} EnvHandler;
typedef struct {
    ICoreWebView2CreateCoreWebView2ControllerCompletedHandler iface;
    LONG refs;
    HWND window;
} ControllerHandler;

static HRESULT STDMETHODCALLTYPE env_query(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *self, REFIID iid, void **out);
static ULONG STDMETHODCALLTYPE env_addref(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *self);
static ULONG STDMETHODCALLTYPE env_release(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *self);
static HRESULT STDMETHODCALLTYPE env_invoke(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *self, HRESULT error, ICoreWebView2Environment *environment);
static HRESULT STDMETHODCALLTYPE controller_query(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler *self, REFIID iid, void **out);
static ULONG STDMETHODCALLTYPE controller_addref(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler *self);
static ULONG STDMETHODCALLTYPE controller_release(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler *self);
static HRESULT STDMETHODCALLTYPE controller_invoke(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler *self, HRESULT error, ICoreWebView2Controller *controller);

static ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandlerVtbl env_vtbl = {
    env_query, env_addref, env_release, env_invoke
};
static ICoreWebView2CreateCoreWebView2ControllerCompletedHandlerVtbl controller_vtbl = {
    controller_query, controller_addref, controller_release, controller_invoke
};

static HRESULT STDMETHODCALLTYPE env_query(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *self, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (!IsEqualIID(iid, &IID_IUnknown) && !IsEqualIID(iid, &IID_ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler)) return E_NOINTERFACE;
    *out = self;
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler_AddRef(self);
    return S_OK;
}
static ULONG STDMETHODCALLTYPE env_addref(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *self)
{
    return (ULONG)InterlockedIncrement(&((EnvHandler *)self)->refs);
}
static ULONG STDMETHODCALLTYPE env_release(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *self)
{
    EnvHandler *h = (EnvHandler *)self;
    LONG n = InterlockedDecrement(&h->refs);
    if (!n) mem_free(h);
    return (ULONG)n;
}

static HRESULT STDMETHODCALLTYPE controller_query(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler *self, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (!IsEqualIID(iid, &IID_IUnknown) && !IsEqualIID(iid, &IID_ICoreWebView2CreateCoreWebView2ControllerCompletedHandler)) return E_NOINTERFACE;
    *out = self;
    ICoreWebView2CreateCoreWebView2ControllerCompletedHandler_AddRef(self);
    return S_OK;
}
static ULONG STDMETHODCALLTYPE controller_addref(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler *self)
{
    ControllerHandler *h = (ControllerHandler *)self;
    return (ULONG)InterlockedIncrement(&h->refs);
}
static ULONG STDMETHODCALLTYPE controller_release(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler *self)
{
    ControllerHandler *h = (ControllerHandler *)self;
    LONG n = InterlockedDecrement(&h->refs);
    if (!n) mem_free(h);
    return (ULONG)n;
}

static void preview_bounds(void)
{
    if (!P.controller || !P.window) return;
    RECT r;
    GetClientRect(P.window, &r);
    ICoreWebView2Controller_put_Bounds(P.controller, r);
}

static bool preview_center(HMONITOR monitor)
{
    MONITORINFO mi = {sizeof mi};
    if (!monitor || !GetMonitorInfoW(monitor, &mi)) return false;
    int w = MINI(PREVIEW_WIDTH, mi.rcWork.right - mi.rcWork.left - 32);
    int h = MINI(PREVIEW_HEIGHT, mi.rcWork.bottom - mi.rcWork.top - 32);
    SetWindowPos(P.window, HWND_TOPMOST, mi.rcWork.left + (mi.rcWork.right - mi.rcWork.left - w) / 2,
                 mi.rcWork.top + (mi.rcWork.bottom - mi.rcWork.top - h) / 2, w, h, SWP_SHOWWINDOW);
    return true;
}

static LRESULT CALLBACK preview_proc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m) {
    case WM_SIZE:
        preview_bounds();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_CLOSE:
        DestroyWindow(h);
        if (ov_active()) ov_leave();
        return 0;
    case WM_DESTROY:
        if (P.window == h) {
            P.window = NULL;
            if (P.controller) {
                ICoreWebView2Controller_Close(P.controller);
                ICoreWebView2Controller_Release(P.controller);
                P.controller = NULL;
            }
            if (P.view) {
                ICoreWebView2_Release(P.view);
                P.view = NULL;
            }
            preview_release_if_idle();
        }
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

static HRESULT STDMETHODCALLTYPE controller_invoke(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler *self, HRESULT error, ICoreWebView2Controller *controller)
{
    ControllerHandler *handler = (ControllerHandler *)self;
    HWND h = handler->window;
    if (P.controller_requests) --P.controller_requests;
    if (FAILED(error) || !controller || !IsWindow(h) || P.window != h) {
        if (controller) ICoreWebView2Controller_Close(controller);
        if (FAILED(error) && IsWindow(h) && P.window == h) {
            MessageBoxW(h, T(F_PREVIEW_ERROR), T(F_TITLE), MB_OK | MB_ICONERROR);
            DestroyWindow(h);
        }
        preview_release_if_idle();
        return S_OK;
    }
    ICoreWebView2Controller_AddRef(controller); /* callback result is borrowed; retain it until WM_DESTROY */
    P.controller = controller;
    if (FAILED(ICoreWebView2Controller_get_CoreWebView2(controller, &P.view))) {
        MessageBoxW(h, T(F_PREVIEW_ERROR), T(F_TITLE), MB_OK | MB_ICONERROR);
        DestroyWindow(h);
        return S_OK;
    }
    preview_bounds();
    ICoreWebView2_Navigate(P.view, P.url);
    ICoreWebView2Controller_put_IsVisible(controller, TRUE);
    SetFocus(h);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE env_invoke(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *self, HRESULT error, ICoreWebView2Environment *environment)
{
    EnvHandler *handler = (EnvHandler *)self;
    P.creating = false;
    if (FAILED(error) || !environment) {
        HWND h = P.window && IsWindow(P.window) ? P.window : handler->window;
        if (IsWindow(h)) {
            MessageBoxW(h, T(F_PREVIEW_ERROR), T(F_TITLE), MB_OK | MB_ICONERROR);
            DestroyWindow(h);
        }
        preview_release_if_idle();
        return S_OK;
    }
    if (P.environment) ICoreWebView2Environment_Release(P.environment);
    P.environment = environment;
    ICoreWebView2Environment_AddRef(environment);
    if (IsWindow(P.window)) {
        ControllerHandler *c = mem_alloc(sizeof *c);
        if (!c) {
            MessageBoxW(P.window, T(F_PREVIEW_ERROR), T(F_TITLE), MB_OK | MB_ICONERROR);
            DestroyWindow(P.window);
            preview_release_if_idle();
            return E_OUTOFMEMORY;
        }
        c->iface.lpVtbl = &controller_vtbl;
        c->refs = 1;
        c->window = P.window;
        ++P.controller_requests;
        HRESULT hr = ICoreWebView2Environment_CreateCoreWebView2Controller(environment, P.window, &c->iface);
        ICoreWebView2CreateCoreWebView2ControllerCompletedHandler_Release(&c->iface);
        if (FAILED(hr)) {
            --P.controller_requests;
            MessageBoxW(P.window, T(F_PREVIEW_ERROR), T(F_TITLE), MB_OK | MB_ICONERROR);
            DestroyWindow(P.window);
        }
    }
    preview_release_if_idle();
    return S_OK;
}

static bool preview_environment(HWND h)
{
    WCHAR local[MAX_PATH], appdir[MAX_PATH], userdir[MAX_PATH], loaderpath[MAX_PATH];
    WCHAR runtime[MAX_PATH * 2], runtime_exe[MAX_PATH * 2 + 32];
    DWORD exe_n = GetModuleFileNameW(NULL, runtime, COUNTOF(runtime));
    if (!exe_n || exe_n >= COUNTOF(runtime)) {
        runtime[0] = 0;
    } else {
        while (exe_n && runtime[exe_n - 1] != L'\\' && runtime[exe_n - 1] != L'/') exe_n--;
        if (!exe_n || exe_n + COUNTOF(L"webview2-runtime") > COUNTOF(runtime)) {
            runtime[0] = 0;
        } else {
            lstrcpyW(runtime + exe_n, L"webview2-runtime");
            wsprintfW(runtime_exe, L"%s\\msedgewebview2.exe", runtime);
            DWORD attr = GetFileAttributesW(runtime_exe);
            if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY)) runtime[0] = 0;
        }
    }
    /* Use the package's Fixed Version runtime when present; otherwise use Evergreen. */
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", local, COUNTOF(local));
    if (!n || n >= COUNTOF(local)) return false;
    wsprintfW(appdir, L"%s\\sniplite", local);
    if (!CreateDirectoryW(appdir, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) return false;
    wsprintfW(userdir, L"%s\\FormulaPreview", appdir);
    if (!CreateDirectoryW(userdir, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) return false;
    wsprintfW(loaderpath, L"%s\\WebView2Loader.dll", appdir);
    if (GetFileAttributesW(loaderpath) == INVALID_FILE_ATTRIBUTES && !resource_file(IDR_WEBVIEW2_LOADER, loaderpath)) return false;
    if (!P.loader) P.loader = LoadLibraryW(loaderpath);
    if (!P.loader) return false;
    CreateEnvironmentFn create = (CreateEnvironmentFn)GetProcAddress(P.loader, "CreateCoreWebView2EnvironmentWithOptions");
    if (!create) return false;
    if (!P.com_initialized) {
        HRESULT com = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
        if (FAILED(com)) return false;
        P.com_initialized = true;
    }
    EnvHandler *c = mem_alloc(sizeof *c);
    if (!c) return false;
    c->iface.lpVtbl = &env_vtbl;
    c->refs = 1;
    c->window = h;
    P.creating = true;
    HRESULT hr = create(runtime[0] ? runtime : NULL, userdir, NULL, &c->iface);
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler_Release(&c->iface);
    if (FAILED(hr)) {
        P.creating = false;
        return false;
    }
    return true;
}

bool formula_preview_host(HWND owner, const WCHAR *url, HMONITOR monitor, bool refresh)
{
    if (lstrlenW(url) >= PREVIEW_URL_CAP) return false;
    if (!monitor) monitor = MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST);
    if (P.window && IsWindow(P.window)) {
        bool changed = refresh || lstrcmpW(P.url, url) != 0;
        if (changed) {
            lstrcpyW(P.url, url);
            if (P.view) ICoreWebView2_Navigate(P.view, P.url);
        }
        if (monitor && MonitorFromWindow(P.window, MONITOR_DEFAULTTONEAREST) != monitor)
            preview_center(monitor);
        SetForegroundWindow(P.window);
        return true;
    }
    lstrcpyW(P.url, url);
    WNDCLASSW wc = {0};
    wc.lpfnWndProc = preview_proc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = PREVIEW_CLASS;
    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    P.window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, PREVIEW_CLASS, L"sniplite · 公式预览",
                               WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, PREVIEW_WIDTH, PREVIEW_HEIGHT,
                               owner, NULL, wc.hInstance, NULL);
    if (!P.window) return false;
    if (!preview_center(monitor)) ShowWindow(P.window, SW_SHOWNORMAL);
    UpdateWindow(P.window);
    SetForegroundWindow(P.window);
    if (P.environment) {
        ControllerHandler *c = mem_alloc(sizeof *c);
        if (!c) { DestroyWindow(P.window); return false; }
        c->iface.lpVtbl = &controller_vtbl;
        c->refs = 1;
        c->window = P.window;
        ++P.controller_requests;
        HRESULT hr = ICoreWebView2Environment_CreateCoreWebView2Controller(P.environment, P.window, &c->iface);
        ICoreWebView2CreateCoreWebView2ControllerCompletedHandler_Release(&c->iface);
        if (FAILED(hr)) {
            --P.controller_requests;
            DestroyWindow(P.window);
            return false;
        }
        return true;
    }
    if (P.creating) return true;
    if (!preview_environment(P.window)) { DestroyWindow(P.window); return false; }
    return true;
}

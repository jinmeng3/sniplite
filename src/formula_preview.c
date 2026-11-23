/* Native formula editor. The renderer and its global font state live until exit. */
#include "common.h"
#include "overlay.h"
#include "formula.h"
#include "image.h"
#include "resource.h"
#include "microtex/microtex.h"

#define PREVIEW_CLASS L"sniplite.formula.preview"
#define CANVAS_CLASS L"sniplite.formula.canvas"
#define PREVIEW_TIMER 1
#define PREVIEW_COPY 100
#define PREVIEW_EDIT 101
#define PREVIEW_TEXT_LIMIT 1048575

typedef struct {
    HWND window, canvas, edit, copy, label, status;
    WNDPROC edit_proc, copy_proc;
    HFONT ui_font, edit_font;
    HBRUSH background;
    WCHAR *source;
    UINT dpi;
    int width, height, scroll_x, scroll_y, layout_width;
    MicrotexRender *render;
    HMODULE module;
    MicrotexInitialize initialize;
    MicrotexCreate create;
    MicrotexDraw draw;
    MicrotexDestroy destroy;
    WCHAR resource_dir[MAX_PATH];
    bool initialized;
} Preview;

static Preview P;

static bool preview_fail(WCHAR *detail, int cap, const WCHAR *stage, DWORD error)
{
    WCHAR text[512], code[32];
    lstrcpynW(text, stage, COUNTOF(text) - 32);
    wsprintfW(code, L" (0x%08lx)", (unsigned long)error);
    lstrcatW(text, code);
    if (detail && cap > 0) lstrcpynW(detail, text, cap);
    return false;
}

static bool preview_directory(const WCHAR *path)
{
    if (CreateDirectoryW(path, NULL)) return true;
    if (GetLastError() != ERROR_ALREADY_EXISTS) return false;
    DWORD a = GetFileAttributesW(path);
    if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY)) return true;
    SetLastError(ERROR_DIRECTORY);
    return false;
}

static const void *preview_resource(int id, DWORD *size)
{
    HRSRC r = FindResourceW(NULL, MAKEINTRESOURCEW(id), RT_RCDATA);
    HGLOBAL g = r ? LoadResource(NULL, r) : NULL;
    const void *data = g ? LockResource(g) : NULL;
    *size = r ? SizeofResource(NULL, r) : 0;
    if (!data || !*size) SetLastError(ERROR_RESOURCE_DATA_NOT_FOUND);
    return data && *size ? data : NULL;
}

static uint64_t preview_hash(uint64_t hash, const void *data, DWORD size)
{
    const unsigned char *p = data;
    hash = (hash ^ size) * UINT64_C(1099511628211);
    for (DWORD i = 0; i < size; ++i) hash = (hash ^ p[i]) * UINT64_C(1099511628211);
    return hash;
}

/* Publish a complete file once; never truncate a DLL another process has loaded. */
static bool preview_cache_file(const WCHAR *path, const void *data, DWORD size)
{
    DWORD attributes = GetFileAttributesW(path);
    if (attributes != INVALID_FILE_ATTRIBUTES) {
        if (!(attributes & FILE_ATTRIBUTE_DIRECTORY)) return true;
        SetLastError(ERROR_DIRECTORY);
        return false;
    }
    WCHAR temporary[MAX_PATH];
    wsprintfW(temporary, L"%s.%lu.tmp", path, (unsigned long)GetCurrentProcessId());
    HANDLE file = CreateFileW(temporary, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = WriteFile(file, data, size, &written, NULL) && written == size;
    DWORD error = ok ? ERROR_SUCCESS : GetLastError();
    if (!ok && !error) error = ERROR_WRITE_FAULT;
    CloseHandle(file);
    if (ok && !MoveFileExW(temporary, path, MOVEFILE_WRITE_THROUGH)) {
        error = GetLastError();
        ok = error == ERROR_ALREADY_EXISTS || error == ERROR_FILE_EXISTS;
    }
    DeleteFileW(temporary);
    if (!ok) SetLastError(error);
    return ok;
}

static bool preview_load(WCHAR *detail, int cap)
{
    if (P.initialized) return true;
    if (!P.module) {
        DWORD dll_size, zip_size;
        const void *dll = preview_resource(IDR_MICROTEX_DLL, &dll_size);
        const void *zip = preview_resource(IDR_MICROTEX_ASSETS, &zip_size);
        if (!dll || !zip) return preview_fail(detail, cap, L"Read embedded MicroTeX", ERROR_RESOURCE_DATA_NOT_FOUND);
        uint64_t hash = preview_hash(UINT64_C(14695981039346656037), dll, dll_size);
        hash = preview_hash(hash, zip, zip_size);
        WCHAR dir[MAX_PATH], path[MAX_PATH], archive[MAX_PATH], marker[MAX_PATH];
        DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", dir, COUNTOF(dir));
        if (!n || n > MAX_PATH - 120)
            return preview_fail(detail, cap, L"Get LOCALAPPDATA", n ? ERROR_INSUFFICIENT_BUFFER : GetLastError());
        lstrcatW(dir, L"\\sniplite");
        if (!preview_directory(dir)) return preview_fail(detail, cap, L"Create sniplite cache", GetLastError());
        lstrcatW(dir, L"\\MicroTeX");
        if (!preview_directory(dir)) return preview_fail(detail, cap, L"Create MicroTeX cache", GetLastError());
        wsprintfW(path, L"\\%08lx%08lx", (unsigned long)(hash >> 32), (unsigned long)(hash & UINT64_C(0xffffffff)));
        lstrcatW(dir, path);
        if (!preview_directory(dir)) return preview_fail(detail, cap, L"Create versioned MicroTeX cache", GetLastError());
        wsprintfW(path, L"%s\\sniplite_microtex.dll", dir);
        wsprintfW(archive, L"%s\\microtex-assets.zip", dir);
        wsprintfW(marker, L"%s\\assets.ready", dir);
        wsprintfW(P.resource_dir, L"%s\\res", dir);
        DWORD assets = GetFileAttributesW(P.resource_dir);
        if (GetFileAttributesW(marker) == INVALID_FILE_ATTRIBUTES || assets == INVALID_FILE_ATTRIBUTES || !(assets & FILE_ATTRIBUTE_DIRECTORY)) {
            if (!preview_cache_file(archive, zip, zip_size))
                return preview_fail(detail, cap, L"Extract embedded MicroTeX archive", GetLastError());
            WCHAR tar[MAX_PATH], command[MAX_PATH * 3 + 32];
            UINT sys = GetSystemDirectoryW(tar, COUNTOF(tar));
            if (!sys || sys > MAX_PATH - 9)
                return preview_fail(detail, cap, L"Find System32 tar.exe", sys ? ERROR_INSUFFICIENT_BUFFER : GetLastError());
            lstrcatW(tar, L"\\tar.exe");
            wsprintfW(command, L"\"%s\" -xf \"%s\" -C \"%s\"", tar, archive, dir);
            STARTUPINFOW si = {sizeof si};
            PROCESS_INFORMATION pi = {0};
            if (!CreateProcessW(tar, command, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
                return preview_fail(detail, cap, L"Start System32 tar.exe", GetLastError());
            CloseHandle(pi.hThread);
            DWORD waited = WaitForSingleObject(pi.hProcess, INFINITE), exit_code = 1;
            bool exited = waited == WAIT_OBJECT_0 && GetExitCodeProcess(pi.hProcess, &exit_code);
            DWORD error = exited ? exit_code : GetLastError();
            CloseHandle(pi.hProcess);
            if (!exited || exit_code) return preview_fail(detail, cap, L"Unpack MicroTeX resources", error);
            assets = GetFileAttributesW(P.resource_dir);
            if (assets == INVALID_FILE_ATTRIBUTES || !(assets & FILE_ATTRIBUTE_DIRECTORY))
                return preview_fail(detail, cap, L"Find extracted MicroTeX resources", ERROR_PATH_NOT_FOUND);
            static const char ready[] = "MicroTeX assets extracted\n";
            if (!preview_cache_file(marker, ready, sizeof ready - 1))
                return preview_fail(detail, cap, L"Mark extracted MicroTeX resources", GetLastError());
        }
        if (!preview_cache_file(path, dll, dll_size))
            return preview_fail(detail, cap, L"Extract embedded MicroTeX DLL", GetLastError());
        P.module = LoadLibraryExW(path, NULL, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!P.module) return preview_fail(detail, cap, L"Load MicroTeX DLL", GetLastError());
        /* FARPROC and the ABI entry points have different function signatures. */
        union { FARPROC proc; MicrotexInitialize initialize; MicrotexCreate create; MicrotexDraw draw; MicrotexDestroy destroy; } api;
        api.proc = GetProcAddress(P.module, "microtex_initialize"); P.initialize = api.initialize;
        api.proc = GetProcAddress(P.module, "microtex_create"); P.create = api.create;
        api.proc = GetProcAddress(P.module, "microtex_draw"); P.draw = api.draw;
        api.proc = GetProcAddress(P.module, "microtex_destroy"); P.destroy = api.destroy;
    }
    if (!P.initialize || !P.create || !P.draw || !P.destroy)
        return preview_fail(detail, cap, L"Find MicroTeX C ABI", ERROR_PROC_NOT_FOUND);
    WCHAR error[512] = {0};
    if (!P.initialize(P.resource_dir, error, COUNTOF(error))) {
        error[COUNTOF(error) - 1] = 0;
        if (error[0]) {
            if (detail && cap > 0) lstrcpynW(detail, error, cap);
            return false;
        }
        return preview_fail(detail, cap, L"Initialize MicroTeX", ERROR_INVALID_DATA);
    }
    P.initialized = true;
    return true;
}

static int preview_px(int value)
{
    return MulDiv(value, P.dpi ? (int)P.dpi : 96, 96);
}

static WCHAR *preview_text(void)
{
    int length = GetWindowTextLengthW(P.edit);
    WCHAR *text = mem_alloc(((size_t)length + 1) * sizeof *text);
    if (text) GetWindowTextW(P.edit, text, length + 1);
    return text;
}

static void preview_scrollbars(void)
{
    if (!P.canvas) return;
    RECT r;
    GetClientRect(P.canvas, &r);
    int pad = preview_px(20);
    SCROLLINFO si = {sizeof si, SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL};
    si.nMin = 0;
    si.nMax = MAXI(0, P.width + pad * 2 - 1);
    si.nPage = (UINT)MAXI(0, r.right);
    si.nPos = P.scroll_x;
    SetScrollInfo(P.canvas, SB_HORZ, &si, TRUE);
    si.nMax = MAXI(0, P.height + pad * 2 - 1);
    si.nPage = (UINT)MAXI(0, r.bottom);
    si.nPos = P.scroll_y;
    SetScrollInfo(P.canvas, SB_VERT, &si, TRUE);
    si.fMask = SIF_POS;
    GetScrollInfo(P.canvas, SB_HORZ, &si); P.scroll_x = si.nPos;
    GetScrollInfo(P.canvas, SB_VERT, &si); P.scroll_y = si.nPos;
    InvalidateRect(P.canvas, NULL, FALSE);
}

static void preview_render(void)
{
    if (!P.window || !P.edit) return;
    KillTimer(P.window, PREVIEW_TIMER);
    if (P.render) P.destroy(P.render);
    P.render = NULL;
    P.width = P.height = P.scroll_x = P.scroll_y = 0;
    WCHAR *text = preview_text();
    WCHAR error[512] = {0};
    if (!text) {
        lstrcpyW(error, L"Cannot allocate formula text.");
    } else if (text[0]) {
        RECT r;
        GetClientRect(P.canvas, &r);
        int width = MAXI(preview_px(80), r.right - preview_px(40));
        P.render = P.create(text, width, (float)preview_px(26), 0xff193252u,
                            &P.width, &P.height, error, COUNTOF(error));
        error[COUNTOF(error) - 1] = 0;
        if (!P.render && !error[0]) lstrcpyW(error, L"MicroTeX could not parse this formula.");
        if (P.render) error[0] = 0;
    }
    mem_free(text);
    SetWindowTextW(P.status, error);
    preview_scrollbars();
}

static void preview_copy(void)
{
    WCHAR *text = preview_text();
    if (!text || !clip_put_text(P.window, text))
        SetWindowTextW(P.status, L"Cannot copy LaTeX to the clipboard.");
    mem_free(text);
}

static void preview_fonts(void)
{
    HFONT ui = CreateFontW(-MulDiv(10, (int)P.dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                          DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                          DEFAULT_PITCH, L"Segoe UI");
    HFONT editor = CreateFontW(-MulDiv(11, (int)P.dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                              FIXED_PITCH, L"Consolas");
    SendMessageW(P.label, WM_SETFONT, (WPARAM)ui, TRUE);
    SendMessageW(P.status, WM_SETFONT, (WPARAM)ui, TRUE);
    SendMessageW(P.copy, WM_SETFONT, (WPARAM)ui, TRUE);
    SendMessageW(P.edit, WM_SETFONT, (WPARAM)editor, TRUE);
    if (P.ui_font) DeleteObject(P.ui_font);
    if (P.edit_font) DeleteObject(P.edit_font);
    P.ui_font = ui;
    P.edit_font = editor;
}

static void preview_layout(void)
{
    if (!P.canvas) return;
    RECT r;
    GetClientRect(P.window, &r);
    int pad = preview_px(12), toolbar = preview_px(32), error = preview_px(48);
    int width = MAXI(1, r.right - pad * 2), editor = MAXI(preview_px(90), r.bottom * 35 / 100);
    int canvas = MAXI(1, r.bottom - toolbar - error - editor - pad * 4);
    int button = preview_px(190);
    MoveWindow(P.label, pad, pad + preview_px(5), MAXI(1, width - button - pad), toolbar, TRUE);
    MoveWindow(P.copy, MAXI(pad, r.right - pad - button), pad, button, toolbar, TRUE);
    MoveWindow(P.canvas, pad, pad * 2 + toolbar, width, canvas, TRUE);
    MoveWindow(P.status, pad, pad * 2 + toolbar + canvas + preview_px(4), width, error, TRUE);
    MoveWindow(P.edit, pad, pad * 3 + toolbar + canvas + error, width, editor, TRUE);
    if (P.layout_width != width) {
        P.layout_width = width;
        SetTimer(P.window, PREVIEW_TIMER, 180, NULL);
    }
}

static void preview_scroll(HWND h, int bar, UINT command)
{
    SCROLLINFO si = {sizeof si, SIF_ALL};
    GetScrollInfo(h, bar, &si);
    int position = si.nPos;
    switch (command) {
    case SB_LINEUP: position -= preview_px(32); break;
    case SB_LINEDOWN: position += preview_px(32); break;
    case SB_PAGEUP: position -= (int)si.nPage; break;
    case SB_PAGEDOWN: position += (int)si.nPage; break;
    case SB_TOP: position = si.nMin; break;
    case SB_BOTTOM: position = si.nMax; break;
    case SB_THUMBTRACK: case SB_THUMBPOSITION: position = si.nTrackPos; break;
    default: return;
    }
    si.fMask = SIF_POS;
    si.nPos = position;
    SetScrollInfo(h, bar, &si, TRUE);
    GetScrollInfo(h, bar, &si);
    if (bar == SB_HORZ) P.scroll_x = si.nPos; else P.scroll_y = si.nPos;
    InvalidateRect(h, NULL, FALSE);
}

static LRESULT CALLBACK canvas_proc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m) {
    case WM_SIZE: preview_scrollbars(); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_HSCROLL: preview_scroll(h, SB_HORZ, LOWORD(wp)); return 0;
    case WM_VSCROLL: preview_scroll(h, SB_VERT, LOWORD(wp)); return 0;
    case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL: {
        int bar = m == WM_MOUSEHWHEEL || (LOWORD(wp) & MK_SHIFT) ? SB_HORZ : SB_VERT;
        int delta = (short)HIWORD(wp);
        if (m == WM_MOUSEHWHEEL) delta = -delta;
        SCROLLINFO si = {sizeof si, SIF_POS};
        GetScrollInfo(h, bar, &si);
        si.nPos -= MulDiv(delta, preview_px(96), WHEEL_DELTA);
        SetScrollInfo(h, bar, &si, TRUE);
        GetScrollInfo(h, bar, &si);
        if (bar == SB_HORZ) P.scroll_x = si.nPos; else P.scroll_y = si.nPos;
        InvalidateRect(h, NULL, FALSE);
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT r;
        GetClientRect(h, &r);
        FillRect(dc, &ps.rcPaint, P.background);
        if (P.render) {
            int pad = preview_px(20);
            int x = MAXI(pad, (r.right - P.width) / 2) - P.scroll_x;
            int y = MAXI(pad, (r.bottom - P.height) / 2) - P.scroll_y;
            if (!P.draw(P.render, dc, (float)x, (float)y)) {
                P.destroy(P.render);
                P.render = NULL;
                P.width = P.height = 0;
                SetWindowTextW(P.status, L"MicroTeX could not draw this formula.");
                preview_scrollbars();
            }
        }
        EndPaint(h, &ps);
        return 0;
    }
    }
    return DefWindowProcW(h, m, wp, lp);
}

static LRESULT CALLBACK editor_proc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    if (m == WM_KEYDOWN) {
        if (wp == VK_RETURN && (GetKeyState(VK_CONTROL) & 0x8000)) { preview_copy(); return 0; }
        if (wp == 'A' && (GetKeyState(VK_CONTROL) & 0x8000)) { SendMessageW(h, EM_SETSEL, 0, -1); return 0; }
        if (wp == VK_ESCAPE) { SendMessageW(P.window, WM_CLOSE, 0, 0); return 0; }
        if (wp == VK_TAB && !(GetKeyState(VK_CONTROL) & 0x8000)) { SetFocus(P.copy); return 0; }
    }
    if (m == WM_CHAR && (wp == 10 || wp == 1 || wp == 9 || wp == VK_ESCAPE)) return 0;
    return CallWindowProcW(P.edit_proc, h, m, wp, lp);
}

static LRESULT CALLBACK copy_proc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    if (m == WM_KEYDOWN) {
        if (wp == VK_RETURN) { preview_copy(); return 0; }
        if (wp == VK_TAB) { SetFocus(P.edit); return 0; }
        if (wp == VK_ESCAPE) { SendMessageW(P.window, WM_CLOSE, 0, 0); return 0; }
    }
    if (m == WM_CHAR && (wp == VK_RETURN || wp == 10 || wp == 9 || wp == VK_ESCAPE)) return 0;
    return CallWindowProcW(P.copy_proc, h, m, wp, lp);
}

static LRESULT CALLBACK preview_proc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m) {
    case WM_CREATE: {
        P.window = h;
        P.dpi = GetDpiForWindow(h);
        P.background = CreateSolidBrush(RGB(255, 255, 255));
        HINSTANCE instance = GetModuleHandleW(NULL);
        P.label = CreateWindowW(L"STATIC", L"LaTeX", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, h, NULL, instance, NULL);
        P.copy = CreateWindowW(L"BUTTON", L"复制 LaTeX (Ctrl+Enter)", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                               0, 0, 0, 0, h, (HMENU)(INT_PTR)PREVIEW_COPY, instance, NULL);
        P.canvas = CreateWindowExW(WS_EX_CLIENTEDGE, CANVAS_CLASS, L"", WS_CHILD | WS_VISIBLE | WS_HSCROLL | WS_VSCROLL,
                                   0, 0, 0, 0, h, NULL, instance, NULL);
        P.status = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
                                 0, 0, 0, 0, h, NULL, instance, NULL);
        P.edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | WS_HSCROLL |
                                 ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_WANTRETURN,
                                 0, 0, 0, 0, h, (HMENU)(INT_PTR)PREVIEW_EDIT, instance, NULL);
        if (!P.background || !P.label || !P.copy || !P.canvas || !P.status || !P.edit) return -1;
        SendMessageW(P.edit, EM_SETLIMITTEXT, PREVIEW_TEXT_LIMIT, 0);
        P.edit_proc = (WNDPROC)SetWindowLongPtrW(P.edit, GWLP_WNDPROC, (LONG_PTR)editor_proc);
        P.copy_proc = (WNDPROC)SetWindowLongPtrW(P.copy, GWLP_WNDPROC, (LONG_PTR)copy_proc);
        if (!P.edit_proc || !P.copy_proc) return -1;
        preview_fonts();
        preview_layout();
        return 0;
    }
    case WM_SIZE: preview_layout(); return 0;
    case WM_GETMINMAXINFO: {
        MINMAXINFO *mm = (MINMAXINFO *)lp;
        mm->ptMinTrackSize.x = preview_px(440);
        mm->ptMinTrackSize.y = preview_px(420);
        return 0;
    }
    case WM_DPICHANGED: {
        P.dpi = HIWORD(wp);
        RECT *r = (RECT *)lp;
        preview_fonts();
        SetWindowPos(h, NULL, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        preview_layout();
        preview_render();
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == PREVIEW_COPY && HIWORD(wp) == BN_CLICKED) { preview_copy(); return 0; }
        if (LOWORD(wp) == PREVIEW_EDIT && HIWORD(wp) == EN_CHANGE) { SetTimer(h, PREVIEW_TIMER, 180, NULL); return 0; }
        break;
    case WM_TIMER:
        if (wp == PREVIEW_TIMER) { preview_render(); return 0; }
        break;
    case WM_KEYDOWN:
        if (wp == VK_RETURN && (GetKeyState(VK_CONTROL) & 0x8000)) { preview_copy(); return 0; }
        if (wp == VK_ESCAPE) { SendMessageW(h, WM_CLOSE, 0, 0); return 0; }
        break;
    case WM_SETFOCUS: if (P.edit) SetFocus(P.edit); return 0;
    case WM_CTLCOLORSTATIC:
        SetBkMode((HDC)wp, TRANSPARENT);
        SetTextColor((HDC)wp, (HWND)lp == P.status ? RGB(180, 35, 35) : RGB(25, 50, 82));
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    case WM_CLOSE:
        DestroyWindow(h);
        if (ov_active()) ov_leave();
        return 0;
    case WM_DESTROY:
        KillTimer(h, PREVIEW_TIMER);
        if (P.render) P.destroy(P.render);
        P.render = NULL;
        if (P.ui_font) DeleteObject(P.ui_font);
        if (P.edit_font) DeleteObject(P.edit_font);
        if (P.background) DeleteObject(P.background);
        P.ui_font = P.edit_font = NULL;
        P.background = NULL;
        mem_free(P.source);
        P.source = NULL;
        P.window = P.canvas = P.edit = P.copy = P.label = P.status = NULL;
        P.width = P.height = P.scroll_x = P.scroll_y = P.layout_width = 0;
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

static bool preview_center(HMONITOR monitor)
{
    MONITORINFO mi = {sizeof mi};
    if (!monitor || !GetMonitorInfoW(monitor, &mi)) return false;
    float scale = dpi_scale_of(monitor);
    int width = MINI((int)(800 * scale), mi.rcWork.right - mi.rcWork.left - 32);
    int height = MINI((int)(640 * scale), mi.rcWork.bottom - mi.rcWork.top - 32);
    SetWindowPos(P.window, HWND_TOPMOST, mi.rcWork.left + (mi.rcWork.right - mi.rcWork.left - width) / 2,
                 mi.rcWork.top + (mi.rcWork.bottom - mi.rcWork.top - height) / 2,
                 width, height, SWP_SHOWWINDOW);
    return true;
}

bool formula_preview_host(HWND owner, const WCHAR *latex, HMONITOR monitor, bool refresh, WCHAR *detail, int cap)
{
    if (detail && cap > 0) detail[0] = 0;
    if (!latex || lstrlenW(latex) > PREVIEW_TEXT_LIMIT)
        return preview_fail(detail, cap, L"Formula source", ERROR_INVALID_PARAMETER);
    if (!preview_load(detail, cap)) return false;
    if (!monitor) monitor = MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST);
    if (P.window && IsWindow(P.window)) {
        if (refresh || !P.source || lstrcmpW(P.source, latex)) {
            WCHAR *source = wcs_dup(latex, -1);
            if (!source) return preview_fail(detail, cap, L"Allocate formula source", ERROR_OUTOFMEMORY);
            if (!SetWindowTextW(P.edit, latex)) {
                mem_free(source);
                return preview_fail(detail, cap, L"Replace formula source", GetLastError());
            }
            mem_free(P.source);
            P.source = source;
            preview_render();
        }
        if (monitor && MonitorFromWindow(P.window, MONITOR_DEFAULTTONEAREST) != monitor) preview_center(monitor);
        if (IsIconic(P.window)) ShowWindow(P.window, SW_RESTORE);
        SetForegroundWindow(P.window);
        SetFocus(P.edit);
        return true;
    }
    HINSTANCE instance = GetModuleHandleW(NULL);
    WNDCLASSW wc = {0};
    wc.lpfnWndProc = canvas_proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = CANVAS_CLASS;
    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return preview_fail(detail, cap, L"Register formula canvas", GetLastError());
    wc.lpfnWndProc = preview_proc;
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP));
    wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    wc.lpszClassName = PREVIEW_CLASS;
    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return preview_fail(detail, cap, L"Register formula preview", GetLastError());
    WCHAR *source = wcs_dup(latex, -1);
    if (!source) return preview_fail(detail, cap, L"Allocate formula source", ERROR_OUTOFMEMORY);
    HWND window = CreateWindowExW(WS_EX_APPWINDOW | WS_EX_TOPMOST, PREVIEW_CLASS, L"sniplite · 公式预览",
                                 WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 800, 640,
                                 owner, NULL, instance, NULL);
    if (!window) {
        mem_free(source);
        return preview_fail(detail, cap, L"Create formula preview", GetLastError());
    }
    P.source = source;
    if (!SetWindowTextW(P.edit, latex)) {
        DWORD error = GetLastError();
        DestroyWindow(window);
        return preview_fail(detail, cap, L"Set formula source", error);
    }
    if (!preview_center(monitor)) ShowWindow(window, SW_SHOWNORMAL);
    preview_render();
    UpdateWindow(window);
    SetForegroundWindow(window);
    SetFocus(P.edit);
    return true;
}

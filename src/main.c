/* sniplite.exe: the background process (hotkeys, tray icon, capture overlay, pins, IPC),
 * plus the internal helper modes (--save-dialog, --settings, --shortcuts,
 * --update-check, --ocr, --quick-save, --uninstall), each its own short-lived process. */
#include "common.h"
#include "config.h"
#include "image.h"
#include "install.h"
#include "lang.h"
#include "ocr.h"
#include "overlay.h"
#include "pin.h"
#include "settings.h"
#include "shot.h"
#include "update.h"
#include "resource.h"
#include "ov_layout.inc"
#include <shellapi.h>
#include <shlobj.h>
#include <windowsx.h>

#define WM_APP_TRAY (WM_APP + 2)
#define WM_APP_SETTINGS_DONE (WM_APP + 3)
#define TIMER_MENU_CAPTURE 1
#define TIMER_UPDATE 2
#define TIMER_THEME 3 /* a theme that changed during a capture waits for its end */
#define UPDATE_FIRST_MS (60 * 1000)           /* first check a minute after start */
#define UPDATE_EVERY_MS (24 * 60 * 60 * 1000) /* then once a day */

typedef struct {
    UINT mod, vk;
    bool ok;
    WCHAR text[40];
} Hotkey;

typedef struct {
    UINT mod[HK_COUNT], vk[HK_COUNT];
    bool set[HK_COUNT];
    int backend; /* 0 keep, 1 dxgi, 2 gdi */
} StartOpts;

typedef BOOL(WINAPI *PFN_ShellNotify)(DWORD, PNOTIFYICONDATAW);

static struct {
    HINSTANCE inst;
    HWND hwnd;
    Hotkey hk[HK_COUNT];
    Config cfg;          /* effective settings (ini, then command-line overrides) */
    bool tray_on;
    NOTIFYICONDATAW nid;
    HICON icon;
    UINT wm_taskbar;     /* "TaskbarCreated": Explorer restarted */
    PFN_ShellNotify notify;
    HANDLE settings;     /* the preferences helper process while it is open */
    bool update_timer;   /* the daily update check is scheduled */
    WCHAR update_tag[32]; /* newer release already announced in a balloon */
    bool asking;         /* the question before a capture is left is on the screen */
} D;

extern uint8_t O[];                          /* the capture's state, laid out as ov_layout.inc says */
extern const uint32_t kPalLight[], kPalDark[]; /* the colors of the capture's bars (ov_base.S), by P_* */
extern const uint32_t *ui_pal;

/* ---- hotkeys --------------------------------------------------------------------- */

static void hotkey_apply(int id, UINT mod, UINT vk)
{
    Hotkey *k = &D.hk[id];
    UnregisterHotKey(D.hwnd, id);
    k->mod = mod;
    k->vk = vk;
    hotkey_format(mod, vk, k->text);
    /* the two for text only while there is a module to read it: without one they would take keys for nothing */
    bool wanted = vk && (id < HK_OCR || ocr_there);
    k->ok = !wanted || RegisterHotKey(D.hwnd, id, mod | MOD_NOREPEAT, vk);
}

static bool hotkeys_ok(void)
{
    for (int i = 1; i < HK_COUNT; i++)
        if (!D.hk[i].ok)
            return false;
    return true;
}

/* ---- command helpers ---------------------------------------------------------------- */

static const WCHAR *opt_val(int argc, WCHAR **argv, const WCHAR *name)
{
    for (int i = 1; i + 1 < argc; i++)
        if (arg_eq(argv[i], name))
            return argv[i + 1];
    return NULL;
}

static bool opt_has(int argc, WCHAR **argv, const WCHAR *name)
{
    for (int i = 1; i < argc; i++)
        if (arg_eq(argv[i], name))
            return true;
    return false;
}

/* Every token after the command must be a known flag, a known option + value,
 * or (when allowed) the single positional argument. */
static bool args_valid(int argc, WCHAR **argv, const WCHAR *const *flags, const WCHAR *const *opts, int positional,
                       Str *out)
{
    int pos = 0;
    for (int i = 1; i < argc; i++) {
        bool known = false;
        for (int k = 0; flags[k] && !known; k++)
            known = arg_eq(argv[i], flags[k]);
        for (int k = 0; opts[k] && !known; k++)
            if (arg_eq(argv[i], opts[k])) {
                if (i + 1 >= argc) {
                    str_addf(out, T(ERR_MISSING_VALUE), argv[i]);
                    return false;
                }
                i++;
                known = true;
            }
        if (!known && argv[i][0] != L'-' && pos < positional) {
            pos++;
            known = true;
        }
        if (!known) {
            str_addf(out, T(ERR_UNKNOWN_ARG), argv[i]);
            return false;
        }
    }
    return true;
}

static bool parse_ints(const WCHAR *s, int *v, int n)
{
    for (int i = 0; i < n; i++) {
        bool neg = *s == L'-';
        if (neg)
            s++;
        if (*s < L'0' || *s > L'9')
            return false;
        long long x = 0;
        while (*s >= L'0' && *s <= L'9') {
            x = x * 10 + (*s++ - L'0');
            if (x > 1000000)
                return false;
        }
        v[i] = (int)(neg ? -x : x);
        if (i + 1 < n && *s++ != L',')
            return false;
    }
    return *s == 0;
}

static void resolve_path(const WCHAR *cwd, const WCHAR *p, WCHAR *out, int cap)
{
    WCHAR tmp[1025]; /* the 1024 characters wsprintfW can write, and the 0 */
    bool abs = (p[0] && p[1] == L':') || (p[0] == L'\\' && p[1] == L'\\');
    if (abs || !cwd[0])
        lstrcpynW(tmp, p, MAX_PATH * 2);
    else if (p[0] == L'\\' || p[0] == L'/') /* root of the caller's drive */
        wsprintfW(tmp, L"%c:%s", cwd[0], p);
    else
        wsprintfW(tmp, L"%s\\%s", cwd, p);
    DWORD n = GetFullPathNameW(tmp, (DWORD)cap, out, NULL);
    if (!n || n >= (DWORD)cap) /* failed, or out is too small */
        lstrcpynW(out, tmp, cap);
}

static bool start_opts_parse(int argc, WCHAR **argv, StartOpts *so, Str *err)
{
    static const WCHAR *names[HK_COUNT] = {NULL,                L"--hotkey",     L"--quick-hotkey",   L"--pin-hotkey",
                                           L"--toggle-hotkey", L"--ocr-hotkey", L"--ocr-copy-hotkey"};
    memset(so, 0, sizeof *so);
    for (int id = 1; id < HK_COUNT; id++) {
        const WCHAR *v = opt_val(argc, argv, names[id]);
        if (!v)
            continue;
        if (!hotkey_parse(v, &so->mod[id], &so->vk[id])) {
            if (err)
                str_addf(err, T(ERR_BAD_HOTKEY), v);
            return false;
        }
        so->set[id] = true;
    }
    bool gdi = opt_has(argc, argv, L"--gdi"), dxgi = opt_has(argc, argv, L"--dxgi");
    if (gdi && dxgi) {
        if (err)
            str_add(err, T(ERR_GDI_DXGI));
        return false;
    }
    so->backend = dxgi ? 1 : gdi ? 2 : 0;
    return true;
}

/* ---- commands ---------------------------------------------------------------------- */

/* "  label" padded to column 16 (Chinese, Japanese and Korean characters take two
 * columns), with at least one space after a longer one. */
static void add_label(Str *out, const WCHAR *label)
{
    int w = 0;
    for (const WCHAR *p = label; *p; p++)
        w += *p >= 0x1100 ? 2 : 1;
    str_add(out, L"  ");
    str_add(out, label);
    do
        str_add(out, L" ");
    while (++w < 16);
}

static void status_text(Str *out)
{
    str_addf(out, T(ST_RUNNING), SNIPLITE_VERSION, GetCurrentProcessId());
    for (int id = 1; id < HK_COUNT; id++) {
        const Hotkey *k = &D.hk[id];
        add_label(out, g_tr[S_ST_HK_CAPTURE + id - 1]);
        str_addf(out, L"%s%s\n", k->vk ? k->text : T(UNSET), k->ok ? L"" : T(ST_HK_FAILED));
    }
    const WCHAR *b = shot_backend();
    add_label(out, T(ST_BACKEND));
    str_addf(out, L"%s\n", !lstrcmpW(b, L"dxgi") ? T(ST_DXGI) : !lstrcmpW(b, L"starting") ? T(ST_DXGI_STARTING) : L"GDI");
    add_label(out, T(ST_PINS));
    str_addf(out, L"%d\n", pin_count());
    OvTiming t;
    if (ov_last_timing(&t)) {
        add_label(out, T(ST_LAST));
        str_add(out, T(ST_LAST_1));
        str_add_ms(out, t.total);
        str_add(out, T(ST_LAST_2));
        str_add_ms(out, t.grab);
        str_add(out, T(ST_LAST_3));
        str_add_ms(out, t.paint);
        str_add(out, T(ST_LAST_4));
    }
}

static void status_json(Str *out)
{
    static const WCHAR *keys[HK_COUNT] = {NULL, L"capture", L"quick", L"pin", L"toggle_pins", L"ocr", L"ocr_copy"};
    str_addf(out, L"{\"running\":true,\"version\":\"%s\",\"pid\":%lu,\"backend\":\"%s\",\"capturing\":%s,\"pins\":%d,",
             SNIPLITE_VERSION, GetCurrentProcessId(), shot_backend(), ov_active() ? L"true" : L"false", pin_count());
    str_add(out, L"\"hotkeys\":{");
    for (int id = 1; id < HK_COUNT; id++)
        str_addf(out, L"%s\"%s\":{\"key\":\"%s\",\"ok\":%s}", id > 1 ? L"," : L"", keys[id], D.hk[id].text,
                 D.hk[id].ok ? L"true" : L"false");
    str_add(out, L"},\"last_capture\":");
    OvTiming t;
    if (ov_last_timing(&t)) {
        const double v[] = {t.total, t.grab, t.windows, t.paint, t.focus};
        const WCHAR *k[] = {L"total_ms", L"grab_ms", L"windows_ms", L"paint_ms", L"focus_ms"};
        for (int i = 0; i < COUNTOF(v); i++) {
            str_addf(out, L"%s\"%s\":", i ? L"," : L"{", k[i]);
            str_add_ms(out, v[i]);
        }
        str_add(out, L"}");
    } else {
        str_add(out, L"null");
    }
    str_add(out, L"}\n");
}

/* ---- tray icon --------------------------------------------------------------------- */

static void tray_tip(WCHAR *tip, int cap)
{
    WCHAR t[200];
    lstrcpyW(t, L"sniplite");
    if (D.hk[HK_CAPTURE].vk)
        wsprintfW(t + lstrlenW(t), T(TRAY_CAPTURE), D.hk[HK_CAPTURE].text);
    if (D.hk[HK_PIN].vk)
        wsprintfW(t + lstrlenW(t), T(TRAY_PIN), D.hk[HK_PIN].text);
    lstrcpynW(tip, t, cap);
}

static void tray_add(void)
{
    if (D.tray_on)
        return;
    if (!D.notify) { /* shell32 only gets loaded when the icon is wanted */
        HMODULE h = LoadLibraryW(L"shell32.dll");
        D.notify = h ? (PFN_ShellNotify)(void *)GetProcAddress(h, "Shell_NotifyIconW") : NULL;
        if (!D.notify)
            return;
    }
    if (!D.icon) {
        int sz = GetSystemMetricsForDpi(SM_CXSMICON, GetDpiForSystem());
        D.icon = LoadImageW(D.inst, MAKEINTRESOURCEW(1), IMAGE_ICON, sz, sz, 0);
    }
    NOTIFYICONDATAW *n = &D.nid;
    memset(n, 0, sizeof *n);
    n->cbSize = sizeof *n;
    n->hWnd = D.hwnd;
    n->uID = 1;
    n->uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    n->uCallbackMessage = WM_APP_TRAY;
    n->hIcon = D.icon;
    tray_tip(n->szTip, COUNTOF(n->szTip));
    if (D.notify(NIM_ADD, n)) {
        n->uVersion = NOTIFYICON_VERSION_4;
        D.notify(NIM_SETVERSION, n);
        D.tray_on = true;
    }
}

static void tray_remove(void)
{
    if (D.tray_on) {
        D.notify(NIM_DELETE, &D.nid);
        D.tray_on = false;
    }
}

static void tray_refresh_tip(void)
{
    if (!D.tray_on)
        return;
    tray_tip(D.nid.szTip, COUNTOF(D.nid.szTip));
    D.nid.uFlags = NIF_TIP | NIF_SHOWTIP;
    D.notify(NIM_MODIFY, &D.nid);
}

/* The capture's bars, light or dark: as the settings say, or as Windows is set for apps
 * (where it keeps that; no other way is documented). Looked up when it can have changed,
 * never on the way to a capture. A capture that is open keeps its look to its end. */
static void theme_apply(void)
{
    typedef LSTATUS(WINAPI *PFN_RegGetValueW)(HKEY, LPCWSTR, LPCWSTR, DWORD, LPDWORD, PVOID, LPDWORD);
    static PFN_RegGetValueW get;
    if (ov_active()) {
        SetTimer(D.hwnd, TIMER_THEME, 500, NULL);
        return;
    }
    KillTimer(D.hwnd, TIMER_THEME);
    bool dark = D.cfg.theme == THEME_DARK;
    if (D.cfg.theme == THEME_AUTO) {
        if (!get) { /* advapi32 is in every process that has a window; named, it is not one more import */
            HMODULE a = LoadLibraryW(L"advapi32.dll");
            get = a ? (PFN_RegGetValueW)(void *)GetProcAddress(a, "RegGetValueW") : NULL;
        }
        DWORD light = 1, n = sizeof light;
        if (get && get(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"AppsUseLightTheme",
                       RRF_RT_REG_DWORD, NULL, &light, &n) == ERROR_SUCCESS)
            dark = !light;
    }
    ui_pal = dark ? kPalDark : kPalLight;
}

/* Apply settings. force_keys re-registers every hotkey (after the preferences window). */
static void apply_config(const Config *c, bool force_keys)
{
    for (int id = 1; id < HK_COUNT; id++)
        if (force_keys || c->mod[id] != D.hk[id].mod || c->vk[id] != D.hk[id].vk || !D.hk[id].ok)
            hotkey_apply(id, c->mod[id], c->vk[id]);
    pin_top_all(c->pin_top);
    if (c->gdi && gpu_enabled())
        gpu_stop();
    else if (!c->gdi && !gpu_enabled())
        gpu_start(D.hwnd);
    if (c->tray)
        tray_add();
    else
        tray_remove();
    /* the result is shown as a tray balloon: without the icon there is no point checking */
    bool check = c->auto_update && c->tray;
    if (check && !D.update_timer) {
        D.update_timer = SetTimer(D.hwnd, TIMER_UPDATE, UPDATE_FIRST_MS, NULL) != 0;
    } else if (!check && D.update_timer) {
        KillTimer(D.hwnd, TIMER_UPDATE);
        D.update_timer = false;
    }
    D.cfg = *c;
    tray_refresh_tip();
    theme_apply();
}

/* A newer release was found by `--update-check`: say so once, in a balloon. */
static void update_notify(const WCHAR *tag)
{
    for (const WCHAR *p = tag; *p; p++)
        if (!((*p >= L'0' && *p <= L'9') || (*p >= L'a' && *p <= L'z') || (*p >= L'A' && *p <= L'Z') || *p == L'.' ||
              *p == L'-' || *p == L'_'))
            return;
    if (!*tag || lstrlenW(tag) >= COUNTOF(D.update_tag) || !lstrcmpW(tag, D.update_tag) || !D.tray_on)
        return;
    lstrcpyW(D.update_tag, tag);
    NOTIFYICONDATAW n = D.nid;
    n.uFlags = NIF_INFO | NIF_SHOWTIP; /* without NIF_SHOWTIP the tooltip would go away */
    n.dwInfoFlags = NIIF_INFO;
    lstrcpynW(n.szInfoTitle, T(UPD_BALLOON_TITLE), COUNTOF(n.szInfoTitle));
    wsprintfW(n.szInfo, T(UPD_BALLOON_TEXT), tag);
    D.notify(NIM_MODIFY, &n);
}

static void apply_start(const StartOpts *so)
{
    Config c = D.cfg;
    for (int id = 1; id < HK_COUNT; id++)
        if (so->set[id]) {
            c.mod[id] = so->mod[id];
            c.vk[id] = so->vk[id];
        }
    if (so->backend)
        c.gdi = so->backend == 2;
    apply_config(&c, false);
}

/* ---- preferences window (separate short-lived process) -------------------------------- */

static DWORD WINAPI settings_wait(void *proc)
{
    WaitForSingleObject((HANDLE)proc, INFINITE);
    PostMessageW(D.hwnd, WM_APP_SETTINGS_DONE, 0, 0);
    return 0;
}

/* Start sniplite.exe with a helper switch (its own short-lived process). quiet: a
 * background job, so no busy cursor while it starts. */
static bool run_helper(const WCHAR *args, bool quiet)
{
    WCHAR exe[MAX_PATH];
    if (!GetModuleFileNameW(NULL, exe, MAX_PATH))
        return false;
    Str cmd = {0};
    str_add_quoted(&cmd, exe);
    str_add(&cmd, args);
    STARTUPINFOW si = {sizeof si};
    if (quiet)
        si.dwFlags = STARTF_FORCEOFFFEEDBACK;
    PROCESS_INFORMATION pi;
    AllowSetForegroundWindow(ASFW_ANY);
    bool ok = cmd.p && CreateProcessW(exe, cmd.p, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
    str_free(&cmd);
    if (ok) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    return ok;
}

/* page: NULL, or the tab to open the window on: "update", "ocr" (an open window switches) */
bool settings_open(const WCHAR *page)
{
    WCHAR args[40] = L" --settings ";
    lstrcpynW(args + 12, page ? page : L"", COUNTOF(args) - 12);
    if (D.settings) {
        if (page)
            return run_helper(args, false); /* finds the open window and tells it */
        HWND w = ipc_find_dialog(SETTINGS_MARK);
        if (w) {
            ShowWindow(w, SW_RESTORE);
            SetForegroundWindow(w);
        }
        return true;
    }
    WCHAR exe[MAX_PATH];
    if (!GetModuleFileNameW(NULL, exe, MAX_PATH))
        return false;
    Str cmd = {0};
    str_add_quoted(&cmd, exe);
    str_add(&cmd, args);
    STARTUPINFOW si = {sizeof si};
    PROCESS_INFORMATION pi;
    AllowSetForegroundWindow(ASFW_ANY);
    bool ok = cmd.p && CreateProcessW(exe, cmd.p, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
    str_free(&cmd);
    HANDLE th = NULL;
    if (ok) {
        CloseHandle(pi.hThread);
        D.settings = pi.hProcess;
        th = CreateThread(NULL, 0, settings_wait, pi.hProcess, 0, NULL);
    }
    if (!th) { /* without a watcher a crash mid-recording could leave hotkeys off */
        if (ok) {
            TerminateProcess(pi.hProcess, 1);
            CloseHandle(pi.hProcess);
            D.settings = NULL;
        }
        return false;
    }
    CloseHandle(th);
    return true;
}

/* sniplite.ini again: the preferences window saves every change, the language too. */
static void reload(void)
{
    lang_init(NULL);
    Config c;
    config_load(&c);
    ocr_refresh(); /* the module may have been installed or removed there: its buttons and hotkeys follow */
    apply_config(&c, true);
}

static void settings_done(void)
{
    if (D.settings) {
        CloseHandle(D.settings);
        D.settings = NULL;
    }
    reload(); /* also restores the hotkeys if the window died while recording one */
}

static int cmd_start(int argc, WCHAR **argv, bool json, Str *out)
{
    static const WCHAR *flags[] = {L"--json", L"--gdi", L"--dxgi", NULL};
    static const WCHAR *opts[] = {L"--hotkey", L"--pin-hotkey", L"--toggle-hotkey", NULL};
    StartOpts so;
    if (!args_valid(argc, argv, flags, opts, 0, out) || !start_opts_parse(argc, argv, &so, out))
        return EXIT_USAGE;
    apply_start(&so);
    if (json)
        status_json(out);
    else
        status_text(out);
    return hotkeys_ok() ? EXIT_OK : EXIT_HOTKEY;
}

static int capture_now(RECT sr, const WCHAR *path, bool clip, bool pin, bool json, Str *out)
{
    Shot sh;
    if (!shot_take(&sh, NULL, NULL)) {
        str_add(out, T(CAP_FAILED));
        return EXIT_FAIL;
    }
    RECT vs = {sh.vx, sh.vy, sh.vx + sh.vw, sh.vy + sh.vh};
    RECT r = rect_isect(sr, vs);
    if (rect_empty(&r)) {
        shot_free(&sh);
        str_add(out, T(CAP_OUTSIDE));
        return EXIT_USAGE;
    }
    RECT cr = {r.left - sh.vx, r.top - sh.vy, r.right - sh.vx, r.bottom - sh.vy};
    int w = r.right - r.left, h = r.bottom - r.top;
    double grab = sh.grab_ms;
    Img im;
    if (!img_create(&im, w, h)) {
        shot_free(&sh);
        str_add(out, T(NO_MEMORY));
        return EXIT_FAIL;
    }
    shot_copy(&sh, cr, im.px, w);
    shot_free(&sh);
    bool saved = false, copied = false, pinned = false;
    if (path)
        saved = image_save(path, im.px, w, w, h);
    if (clip)
        copied = clip_put_image(D.hwnd, im.px, w, w, h);
    if (pin)
        pinned = pin_add(&im, r.left, r.top, false);
    else
        img_destroy(&im);
    bool ok = (!path || saved) && (!clip || copied) && (!pin || pinned);
    if (json) {
        str_addf(out, L"{\"ok\":%s,\"x\":%d,\"y\":%d,\"width\":%d,\"height\":%d,\"grab_ms\":", ok ? L"true" : L"false",
                 (int)r.left, (int)r.top, w, h);
        str_add_ms(out, grab);
        str_add(out, L",\"file\":");
        if (path)
            str_add_json(out, path);
        else
            str_add(out, L"null");
        str_addf(out, L",\"saved\":%s,\"clipboard\":%s,\"pinned\":%s}\n", saved ? L"true" : L"false",
                 copied ? L"true" : L"false", pinned ? L"true" : L"false");
    } else {
        str_addf(out, T(CAP_DONE_1), w, h);
        str_add_ms(out, grab);
        str_add(out, T(CAP_DONE_2));
        if (path)
            str_addf(out, saved ? T(SAVED) : T(SAVE_FAILED), path);
        if (clip)
            str_add(out, copied ? T(COPIED) : T(COPY_FAILED));
        if (pin)
            str_add(out, pinned ? T(PINNED) : T(PIN_FAILED));
    }
    return ok ? EXIT_OK : EXIT_FAIL;
}

static int cmd_capture(int argc, WCHAR **argv, const WCHAR *cwd, bool json, Str *out)
{
    static const WCHAR *flags[] = {L"--json", L"--full", L"--clip", L"--pin", NULL};
    static const WCHAR *opts[] = {L"--rect", L"--out", NULL};
    if (!args_valid(argc, argv, flags, opts, 0, out))
        return EXIT_USAGE;
    const WCHAR *rect = opt_val(argc, argv, L"--rect"), *file = opt_val(argc, argv, L"--out");
    bool full = opt_has(argc, argv, L"--full"), clip = opt_has(argc, argv, L"--clip"), pin = opt_has(argc, argv, L"--pin");
    if (ov_active()) {
        str_add(out, T(CAPTURING));
        return EXIT_FAIL;
    }
    if (!rect && !full) {
        if (file || clip || pin) {
            str_add(out, T(ERR_NEED_RECT));
            return EXIT_USAGE;
        }
        if (!ov_begin()) {
            str_add(out, T(CAP_CANNOT));
            return EXIT_FAIL;
        }
        return EXIT_OK;
    }
    RECT sr;
    if (rect) {
        int v[4];
        if (full || !parse_ints(rect, v, 4) || v[2] <= 0 || v[3] <= 0) {
            str_add(out, T(ERR_RECT));
            return EXIT_USAGE;
        }
        SetRect(&sr, v[0], v[1], v[0] + v[2], v[1] + v[3]);
    } else {
        int x = GetSystemMetrics(SM_XVIRTUALSCREEN), y = GetSystemMetrics(SM_YVIRTUALSCREEN);
        SetRect(&sr, x, y, x + GetSystemMetrics(SM_CXVIRTUALSCREEN), y + GetSystemMetrics(SM_CYVIRTUALSCREEN));
    }
    WCHAR path[MAX_PATH * 2];
    if (file)
        resolve_path(cwd, file, path, COUNTOF(path));
    if (!file && !pin)
        clip = true; /* the useful default */
    return capture_now(sr, file ? path : NULL, clip, pin, json, out);
}

static int cmd_pin(int argc, WCHAR **argv, const WCHAR *cwd, bool json, Str *out)
{
    static const WCHAR *flags[] = {L"--json", NULL};
    static const WCHAR *opts[] = {NULL};
    if (!args_valid(argc, argv, flags, opts, 1, out))
        return EXIT_USAGE;
    const WCHAR *file = NULL;
    for (int i = 1; i < argc; i++)
        if (argv[i][0] != L'-')
            file = argv[i];
    Img im;
    int rc;
    WCHAR path[MAX_PATH * 2];
    if (file) {
        resolve_path(cwd, file, path, COUNTOF(path));
        rc = image_load(path, &im) ? EXIT_OK : EXIT_FAIL;
        if (rc)
            str_addf(out, T(ERR_READ_IMAGE), path);
    } else {
        rc = clip_get_image(D.hwnd, &im);
        if (rc == EXIT_NOTHING)
            str_add(out, T(CLIP_EMPTY));
        else if (rc)
            str_add(out, T(CLIP_FAILED));
    }
    if (rc)
        return rc;
    int w = im.w, h = im.h;
    if (!pin_add(&im, 0, 0, true)) {
        str_add(out, T(PIN_FAILED));
        return EXIT_FAIL;
    }
    if (json)
        str_addf(out, L"{\"ok\":true,\"width\":%d,\"height\":%d}\n", w, h);
    else
        str_addf(out, T(PIN_DONE), w, h);
    return EXIT_OK;
}

static int cmd_run(int argc, WCHAR **argv, const WCHAR *cwd, Str *out)
{
    WCHAR *tmp[66];
    if (argc == 0 || argv[0][0] == L'-') { /* no command or bare options mean "start" */
        int tc = 0;
        tmp[tc++] = L"start";
        for (int i = 0; i < argc && tc < COUNTOF(tmp); i++)
            tmp[tc++] = argv[i];
        argv = tmp;
        argc = tc;
    }
    bool json = opt_has(argc, argv, L"--json");
    const WCHAR *c = argv[0];
    if (arg_eq(c, L"start"))
        return cmd_start(argc, argv, json, out);
    if (arg_eq(c, L"capture"))
        return cmd_capture(argc, argv, cwd, json, out);
    if (arg_eq(c, L"pin"))
        return cmd_pin(argc, argv, cwd, json, out);
    if (arg_eq(c, L"ocr")) { /* a capture for its text: shown beside the selection, or with --copy copied at once */
        ocr_capture(opt_has(argc, argv, L"--copy"));
        return EXIT_OK;
    }
    if (arg_eq(c, L"status")) {
        json ? status_json(out) : status_text(out);
        return EXIT_OK;
    }
    if (arg_eq(c, L"quit") || arg_eq(c, L"exit")) {
        PostMessageW(D.hwnd, WM_CLOSE, 0, 0);
        str_add(out, json ? L"{\"ok\":true}\n" : T(QUIT_DONE));
        return EXIT_OK;
    }
    if (arg_eq(c, L"_hotkeys")) { /* a hotkey field is recording: let the keys through */
        for (int id = 1; id < HK_COUNT; id++)
            UnregisterHotKey(D.hwnd, id);
        return EXIT_OK;
    }
    if (arg_eq(c, L"_reload") || arg_eq(c, L"_hkstate")) {
        if (arg_eq(c, L"_reload"))
            reload();
        for (int id = 1; id < HK_COUNT; id++) /* 1 registered, 0 failed, 2 none */
            str_addf(out, L"%d ", !D.hk[id].vk || (id >= HK_OCR && !ocr_there) ? 2 : D.hk[id].ok ? 1 : 0);
        return EXIT_OK;
    }
    if (arg_eq(c, L"_update")) { /* from `--update-check` */
        if (argc > 1)
            update_notify(argv[1]);
        return EXIT_OK;
    }
    if (arg_eq(c, L"_cancel")) { /* from the text panel: its text was copied, the capture is done */
        ov_cancel();
        long_cancel();
        return EXIT_OK;
    }
    if (arg_eq(c, L"_again")) { /* sniplite was started while it runs: a capture. With the tray icon hidden,
                                   the preferences: they have no other door then */
        static WCHAR *capture[1] = {L"capture"};
        if (D.cfg.tray)
            return cmd_capture(1, capture, cwd, json, out);
        if (!settings_open(NULL)) {
            str_add(out, T(ERR_SETTINGS));
            return EXIT_FAIL;
        }
        return EXIT_OK;
    }
    if (arg_eq(c, L"settings")) { /* `settings update`, `settings ocr`, `settings about`: on that tab */
        static const WCHAR *pages[] = {L"update", L"ocr", L"about"};
        const WCHAR *page = NULL;
        for (int i = 0; argc > 1 && i < COUNTOF(pages); i++)
            if (arg_eq(argv[1], pages[i]))
                page = pages[i];
        if (!settings_open(page)) {
            str_add(out, T(ERR_SETTINGS));
            return EXIT_FAIL;
        }
        return EXIT_OK;
    }
    if (arg_eq(c, L"version") || arg_eq(c, L"--version")) {
        str_addf(out, L"sniplite %s\n", SNIPLITE_VERSION);
        return EXIT_OK;
    }
    str_addf(out, T(ERR_UNKNOWN_CMD), c);
    return EXIT_USAGE;
}

/* ---- daemon window ------------------------------------------------------------------- */

static LRESULT ipc_handle(HWND h, HWND reply, const COPYDATASTRUCT *cd)
{
    if (!cd || cd->dwData != IPC_REQ || cd->cbData < sizeof(WCHAR) || cd->cbData > 256 * 1024)
        return 0;
    int n = (int)(cd->cbData / sizeof(WCHAR));
    WCHAR *buf = mem_alloc(((size_t)n + 1) * sizeof(WCHAR));
    if (!buf)
        return 0;
    memcpy(buf, cd->lpData, (size_t)n * sizeof(WCHAR));
    buf[n] = 0;
    WCHAR *parts[64];
    int np = 0;
    parts[np++] = buf;
    for (WCHAR *p = buf; *p; p++)
        if (*p == IPC_SEP) {
            *p = 0;
            if (np < COUNTOF(parts))
                parts[np++] = p + 1;
        }
    Str out = {0};
    int code = cmd_run(np - 1, parts + 1, parts[0], &out);
    if (reply && IsWindow(reply)) {
        COPYDATASTRUCT rc = {IPC_REPLY, (DWORD)((out.n + 1) * sizeof(WCHAR)), out.p ? out.p : (WCHAR *)L""};
        SendMessageTimeoutW(reply, WM_COPYDATA, (WPARAM)h, (LPARAM)&rc, SMTO_ABORTIFHUNG, 5000, NULL);
    }
    str_free(&out);
    mem_free(buf);
    return IPC_BASE + code;
}

static void pin_clipboard(void)
{
    Img im;
    if (clip_get_image(D.hwnd, &im) == EXIT_OK)
        pin_add(&im, 0, 0, true);
    else
        MessageBeep(MB_ICONWARNING);
}

/* ---- the quick capture: the screen the mouse is on, into a file, at once and without a window ---- */

/* What the helper is given: this at the start of a file mapping, the pixels a page further. */
typedef struct {
    int32_t w, h;
    POINT at;            /* the mouse: the notice goes to its monitor */
    SYSTEMTIME when;     /* for the file's name */
    WCHAR dir[MAX_PATH]; /* the folder from the settings, or empty: the desktop */
} Quick;
#define QUICK_PX 4096

/* sniplite.exe --quick-save <mapping>,<event>, with those two handles inherited and no
 * other. A thread: it takes some milliseconds, and the pixels are copied meanwhile. */
static DWORD WINAPI quick_helper(void *give)
{
    WCHAR exe[MAX_PATH], cmd[MAX_PATH + 60];
    SIZE_T list[16], n = sizeof list; /* room for a list of one attribute */
    HANDLE *h = give;
    STARTUPINFOEXW si = {{sizeof si}};
    PROCESS_INFORMATION pi;
    if (!GetModuleFileNameW(NULL, exe, MAX_PATH) || !InitializeProcThreadAttributeList((void *)list, 1, 0, &n))
        return 0;
    wsprintfW(cmd, L"\"%s\" --quick-save %d,%d", exe, (int)(INT_PTR)h[0], (int)(INT_PTR)h[1]);
    si.StartupInfo.dwFlags = STARTF_FORCEOFFFEEDBACK;
    si.lpAttributeList = (void *)list;
    BOOL ok = UpdateProcThreadAttribute(si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, h, 2 * sizeof *h, NULL, NULL) &&
              CreateProcessW(exe, cmd, NULL, NULL, TRUE, EXTENDED_STARTUPINFO_PRESENT, NULL, NULL, &si.StartupInfo, &pi);
    DeleteProcThreadAttributeList(si.lpAttributeList);
    if (ok) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    return (DWORD)ok;
}

/* Only the screen is read here. Naming the file, encoding and writing it take far
 * longer, and a folder can be slow: all that, and the notice, is the helper's. */
static void quick_capture(void)
{
    POINT at;
    MONITORINFO mi = {sizeof mi};
    Shot sh;
    if (ov_active() || D.asking)
        return;
    GetCursorPos(&at);
    if (!GetMonitorInfoW(MonitorFromPoint(at, MONITOR_DEFAULTTONEAREST), &mi) || !shot_take(&sh, NULL, NULL)) {
        MessageBeep(MB_ICONWARNING);
        return;
    }
    RECT vs = {sh.vx, sh.vy, sh.vx + sh.vw, sh.vy + sh.vh};
    RECT r = rect_isect(mi.rcMonitor, vs);
    int w = r.right - r.left, h = r.bottom - r.top;
    HANDLE give[2] = {NULL, NULL}, th = NULL; /* the mapping, and an event: the pixels are in it */
    Quick *q = NULL;
    DWORD started = 0;
    if (w > 0 && h > 0) {
        uint64_t bytes = QUICK_PX + (uint64_t)w * h * 4;
        SECURITY_ATTRIBUTES sa = {sizeof sa, NULL, TRUE};
        give[0] = CreateFileMappingW(INVALID_HANDLE_VALUE, &sa, PAGE_READWRITE, (DWORD)(bytes >> 32), (DWORD)bytes, NULL);
        give[1] = CreateEventW(&sa, TRUE, FALSE, NULL);
        q = give[0] && give[1] ? MapViewOfFile(give[0], FILE_MAP_WRITE, 0, 0, 0) : NULL;
    }
    if (q && (th = CreateThread(NULL, 0, quick_helper, give, 0, NULL)) != NULL) {
        q->w = w, q->h = h, q->at = at;
        GetLocalTime(&q->when);
        lstrcpynW(q->dir, D.cfg.quick_dir, MAX_PATH);
        OffsetRect(&r, -sh.vx, -sh.vy);
        shot_copy(&sh, r, (uint32_t *)((uint8_t *)q + QUICK_PX), w);
        SetEvent(give[1]);
    }
    shot_free(&sh);
    if (q)
        UnmapViewOfFile(q);
    if (th) {
        WaitForSingleObject(th, INFINITE); /* done long ago as a rule; the handles are its until then */
        GetExitCodeThread(th, &started);
        CloseHandle(th);
    }
    if (give[0])
        CloseHandle(give[0]);
    if (give[1])
        CloseHandle(give[1]);
    if (!started)
        MessageBeep(MB_ICONWARNING);
}

/* The helper: name the file, write it, and say so in the corner. */
static int quick_save(const WCHAR *arg)
{
    typedef HRESULT(WINAPI *PFN_SHGetFolderPathW)(HWND, int, HANDLE, DWORD, LPWSTR);
    int v[2];
    if (!parse_ints(arg, v, 2))
        return EXIT_USAGE;
    HANDLE map = (HANDLE)(INT_PTR)v[0], ready = (HANDLE)(INT_PTR)v[1];
    const Quick *q = WaitForSingleObject(ready, 10000) == WAIT_OBJECT_0 ? MapViewOfFile(map, FILE_MAP_READ, 0, 0, 0) : NULL;
    if (!q)
        return EXIT_FAIL;
    WCHAR dir[MAX_PATH], path[MAX_PATH + 80] = L"";
    lstrcpynW(dir, q->dir, MAX_PATH);
    bool ok = dir[0] != 0;
    if (!ok) {
        HMODULE sh = LoadLibraryW(L"shell32.dll");
        PFN_SHGetFolderPathW folder = sh ? (PFN_SHGetFolderPathW)(void *)GetProcAddress(sh, "SHGetFolderPathW") : NULL;
        ok = folder && SUCCEEDED(folder(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, dir));
    }
    if (ok) {
        CreateDirectoryW(dir, NULL); /* a chosen folder that is gone comes back */
        const SYSTEMTIME *t = &q->when;
        int n = wsprintfW(path, L"%s\\sniplite_%04d-%02d-%02d_%02d-%02d-%02d", dir, t->wYear, t->wMonth, t->wDay, t->wHour,
                          t->wMinute, t->wSecond);
        lstrcpyW(path + n, L".png"); /* named like one saved by hand; a second one in the same second gets a number */
        HANDLE f = INVALID_HANDLE_VALUE;
        for (int k = 2; k < 100; k++) { /* CREATE_NEW: two helpers at once never take the same name */
            f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
            if (f != INVALID_HANDLE_VALUE || GetLastError() != ERROR_FILE_EXISTS)
                break;
            wsprintfW(path + n, L"_%d.png", k);
        }
        ok = f != INVALID_HANDLE_VALUE;
        if (ok) {
            CloseHandle(f);
            ok = image_save(path, (const uint32_t *)((const uint8_t *)q + QUICK_PX), q->w, q->w, q->h);
            if (!ok)
                DeleteFileW(path);
        }
    }
    notice_corner(ok ? T(QUICK_SAVED) : T(OV_SAVE_FAILED), path, q->at, ok ? 2000 : 4000);
    UnmapViewOfFile(q);
    CloseHandle(map);
    notice_wait();
    return ok ? EXIT_OK : EXIT_FAIL;
}

/* ---- leaving a capture ---------------------------------------------------------------------- */

static INT_PTR CALLBACK leave_proc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    (void)lp;
    switch (m) {
    case WM_INITDIALOG:
        if (GetWindow(h, GW_OWNER) != *(HWND *)(O + OV_HWND)) /* a long screenshot goes on beneath it: not in its picture */
            SetWindowDisplayAffinity(h, 0x11);                /* WDA_EXCLUDEFROMCAPTURE */
        SetDlgItemTextW(h, IDC_L_TEXT, T(LEAVE_ASK));
        SetDlgItemTextW(h, IDC_L_ASK, T(LEAVE_KEEP));
        SetDlgItemTextW(h, IDOK, T(LEAVE_YES));
        SetDlgItemTextW(h, IDCANCEL, T(O_CANCEL_BTN));
        CheckDlgButton(h, IDC_L_ASK, BST_CHECKED);
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK || LOWORD(wp) == IDCANCEL) {
            if (IsDlgButtonChecked(h, IDC_L_ASK) != BST_CHECKED) { /* not again: for this run, and into the file */
                D.cfg.exit_ask = false;
                config_set_exit_ask(false);
            }
            EndDialog(h, LOWORD(wp));
            return TRUE;
        }
        break;
    }
    return FALSE;
}

/* May a capture that holds something be left? Asked above owner while the settings want it
 * (exit_ask); the question has a box that switches it off. */
bool leave_ok(HWND owner)
{
    if (!D.cfg.exit_ask)
        return true;
    if (D.asking)
        return false; /* it is being asked: that answer decides */
    D.asking = true;
    bool go = lang_dialog_box(D.inst, IDD_LEAVE, owner, leave_proc) == IDOK;
    D.asking = false;
    return go;
}

/* The user leaves the capture: Esc, the close button. With a selection made there is work to lose. */
void ov_leave(void)
{
    if (*(int32_t *)(O + OV_PHASE) == PH_EDIT && !leave_ok(*(HWND *)(O + OV_HWND)))
        return;
    ov_cancel();
}

/* ---- the tray menu ------------------------------------------------------------------------- */

enum { TM_CAPTURE = 1, TM_PIN, TM_TOGGLE, TM_CLOSE_PINS, TM_SETTINGS, TM_HELP, TM_UPDATE, TM_ABOUT, TM_EXIT };

static void menu_item(HMENU m, UINT id, const WCHAR *text, int hk, bool enabled)
{
    WCHAR t[96];
    if (hk && D.hk[hk].vk)
        wsprintfW(t, L"%s\t%s", text, D.hk[hk].text);
    else
        lstrcpynW(t, text, COUNTOF(t));
    AppendMenuW(m, MF_STRING | (enabled ? 0 : MF_GRAYED), id, t);
}

static void tray_menu(POINT at)
{
    HMENU m = CreatePopupMenu();
    bool pins = pin_count() > 0;
    menu_item(m, TM_CAPTURE, T(M_CAPTURE), HK_CAPTURE, true);
    menu_item(m, TM_PIN, T(M_PIN), HK_PIN, true);
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    menu_item(m, TM_TOGGLE, pins && !pin_any_visible() ? T(M_SHOW_PINS) : T(M_HIDE_PINS), HK_TOGGLE, pins);
    menu_item(m, TM_CLOSE_PINS, T(M_CLOSE_PINS), 0, pins);
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    menu_item(m, TM_SETTINGS, T(M_SETTINGS), 0, true);
    menu_item(m, TM_HELP, T(M_HELP), 0, true);
    menu_item(m, TM_UPDATE, T(M_UPDATE), 0, true);
    menu_item(m, TM_ABOUT, T(M_ABOUT), 0, true);
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    menu_item(m, TM_EXIT, T(M_EXIT), 0, true);
    SetMenuDefaultItem(m, TM_CAPTURE, FALSE); /* bold: what a left click does */
    SetForegroundWindow(D.hwnd);              /* so the menu closes when clicking elsewhere */
    UINT align = GetSystemMetrics(SM_MENUDROPALIGNMENT) ? TPM_RIGHTALIGN : TPM_LEFTALIGN;
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON | align | TPM_BOTTOMALIGN, at.x, at.y,
                             0, D.hwnd, NULL);
    PostMessageW(D.hwnd, WM_NULL, 0, 0);
    DestroyMenu(m);
    switch (cmd) {
    case TM_CAPTURE: /* let the menu's fade-out finish so it is not in the screenshot */
        SetTimer(D.hwnd, TIMER_MENU_CAPTURE, 200, NULL);
        break;
    case TM_PIN:
        pin_clipboard();
        break;
    case TM_TOGGLE:
        pin_toggle_all();
        break;
    case TM_CLOSE_PINS:
        pin_close_all();
        break;
    case TM_SETTINGS:
        settings_open(NULL);
        break;
    case TM_UPDATE:
        settings_open(L"update");
        break;
    case TM_HELP:
        run_helper(L" --shortcuts", false);
        break;
    case TM_ABOUT:
        settings_open(L"about");
        break;
    case TM_EXIT:
        PostMessageW(D.hwnd, WM_CLOSE, 0, 0);
        break;
    }
}

static LRESULT CALLBACK daemon_proc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    if (m == D.wm_taskbar && m) { /* Explorer restarted: the icon is gone */
        D.tray_on = false;
        if (D.cfg.tray)
            tray_add();
        return 0;
    }
    switch (m) {
    case WM_APP_TRAY:
        switch (LOWORD(lp)) {
        case NIN_SELECT:
        case NIN_KEYSELECT:
            if (!ov_active())
                ov_begin();
            break;
        case WM_CONTEXTMENU: {
            POINT at = {GET_X_LPARAM(wp), GET_Y_LPARAM(wp)};
            tray_menu(at);
            break;
        }
        case NIN_BALLOONUSERCLICK: /* the "new version" balloon was clicked */
            settings_open(L"update");
            break;
        }
        return 0;
    case WM_TIMER:
        if (wp == TIMER_MENU_CAPTURE) {
            KillTimer(h, TIMER_MENU_CAPTURE);
            if (!ov_active())
                ov_begin();
        } else if (wp == TIMER_UPDATE) { /* the check runs in its own process */
            SetTimer(h, TIMER_UPDATE, UPDATE_EVERY_MS, NULL);
            if (D.cfg.auto_update && D.cfg.tray)
                run_helper(L" --update-check", true);
        } else if (wp == TIMER_THEME) {
            theme_apply();
        }
        return 0;
    case WM_SETTINGCHANGE: /* Windows went dark or light */
        if (lp && !lstrcmpW((const WCHAR *)lp, L"ImmersiveColorSet"))
            theme_apply();
        return 0;
    case WM_APP_SETTINGS_DONE:
        settings_done();
        return 0;
    case WM_HOTKEY:
        if (wp == HK_CAPTURE) {
            if (!ov_active())
                ov_begin();
        } else if (D.asking) { /* a capture is waiting for its answer: nothing else happens to it */
        } else if (wp == HK_QUICK) {
            quick_capture();
        } else if (wp == HK_PIN) {
            if (ov_active())
                ov_pin_hotkey();
            else
                pin_clipboard();
        } else if (wp == HK_TOGGLE) {
            pin_toggle_all();
        } else if (wp == HK_OCR || wp == HK_OCRCOPY) {
            ocr_capture(wp == HK_OCRCOPY);
        }
        return 0;
    case WM_APP_OCR_THEN:
        ocr_then((int)wp);
        return 0;
    case WM_COPYDATA:
        return ipc_handle(h, (HWND)wp, (const COPYDATASTRUCT *)lp);
    case WM_APP_GPU_READY:
        gpu_ready((void *)lp);
        return 0;
    case WM_DISPLAYCHANGE:
        gpu_display_changed();
        return 0;
    case WM_CLOSE:
        ov_cancel();
        pin_close_all();
        tray_remove();
        for (int id = 1; id < HK_COUNT; id++)
            UnregisterHotKey(h, id);
        gpu_stop();
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

static int daemon_main(HINSTANCE inst, int argc, WCHAR **argv, bool from_cli)
{
    D.inst = inst;
    WCHAR cwd[MAX_PATH * 2];
    if (!GetCurrentDirectoryW(COUNTOF(cwd), cwd))
        cwd[0] = 0;

    /* Wake up fast: above-normal priority, and never put into EcoQoS/efficiency mode.
     * Both cost nothing while idle because the thread sleeps in GetMessage. */
    SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
#ifdef PROCESS_POWER_THROTTLING_CURRENT_VERSION
    PROCESS_POWER_THROTTLING_STATE pt = {PROCESS_POWER_THROTTLING_CURRENT_VERSION,
                                         PROCESS_POWER_THROTTLING_EXECUTION_SPEED, 0};
    SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &pt, sizeof pt);
#endif
    /* Do not keep the caller's directory locked. */
    WCHAR exe_dir[MAX_PATH];
    if (GetModuleFileNameW(NULL, exe_dir, MAX_PATH)) {
        WCHAR *sl = wcsrchr(exe_dir, L'\\');
        if (sl) {
            *sl = 0;
            SetCurrentDirectoryW(exe_dir);
            /* left by an update that had to move running files aside */
            WIN32_FIND_DATAW fd;
            HANDLE fh = FindFirstFileW(L"sniplite.*.old", &fd);
            if (fh != INVALID_HANDLE_VALUE) {
                do
                    if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                        DeleteFileW(fd.cFileName);
                while (FindNextFileW(fh, &fd));
                FindClose(fh);
            }
        }
    }

    WNDCLASSW wc = {0};
    wc.lpfnWndProc = daemon_proc;
    wc.hInstance = inst;
    wc.lpszClassName = DAEMON_CLASS;
    if (!RegisterClassW(&wc))
        return EXIT_FAIL;
    D.hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, DAEMON_CLASS, L"sniplite", WS_POPUP, 0, 0, 0, 0, NULL, NULL, inst, NULL);
    if (!D.hwnd)
        return EXIT_FAIL;
    ChangeWindowMessageFilterEx(D.hwnd, WM_COPYDATA, MSGFLT_ALLOW, NULL);
    D.wm_taskbar = RegisterWindowMessageW(L"TaskbarCreated");
    ChangeWindowMessageFilterEx(D.hwnd, D.wm_taskbar, MSGFLT_ALLOW, NULL);
    ocr_refresh();
    if (!ov_init(inst, D.hwnd) || !pin_init(inst, D.hwnd))
        return EXIT_FAIL;

    /* Settings: defaults, then sniplite.ini, then command-line start options
     * (lenient here: invalid ones are reported by the CLI's own start call). */
    bool start = argc == 0 || arg_eq(argv[0], L"start") || argv[0][0] == L'-';
    StartOpts so = {0};
    if (start) {
        WCHAR *tmp[66];
        int tc = 0;
        tmp[tc++] = L"start";
        for (int i = argc > 0 && arg_eq(argv[0], L"start") ? 1 : 0; i < argc && tc < 66; i++)
            tmp[tc++] = argv[i];
        if (!start_opts_parse(tc, tmp, &so, NULL))
            memset(&so, 0, sizeof so);
    }
    Config c;
    config_load(&c);
    for (int id = 1; id < HK_COUNT; id++)
        if (so.set[id]) {
            c.mod[id] = so.mod[id];
            c.vk[id] = so.vk[id];
        }
    if (so.backend)
        c.gdi = so.backend == 2;
    apply_config(&c, true);

    if (!from_cli && !hotkeys_ok()) {
        Str msg = {0};
        str_add(&msg, T(HK_WARN_1));
        status_text(&msg);
        str_add(&msg, T(HK_WARN_2));
        MessageBoxW(NULL, msg.p, L"sniplite", MB_OK | MB_ICONWARNING);
        str_free(&msg);
    }
    if (!start) { /* e.g. `sniplite.exe capture` or `sniplite.exe pin a.png` */
        Str out = {0};
        int code = cmd_run(argc, argv, cwd, &out);
        if (code && out.n && !from_cli)
            MessageBoxW(NULL, out.p, L"sniplite", MB_OK | MB_ICONWARNING);
        str_free(&out);
    }

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return EXIT_OK;
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR cmdline, int show)
{
    (void)prev;
    (void)cmdline;
    (void)show;
    int argc;
    WCHAR **argv = cmdline_split(GetCommandLineW(), &argc);
    if (!argv)
        return EXIT_FAIL;
    lang_init(NULL); /* every mode below talks to the user */
    if (argc > 1 && arg_eq(argv[1], L"--save-dialog"))
        return save_dialog_helper(argc, argv);
    if (argc > 1 && arg_eq(argv[1], L"--settings"))
        return settings_main(inst, argc > 2 ? argv[2] : NULL);
    if (argc > 1 && arg_eq(argv[1], L"--shortcuts"))
        return shortcuts_main(inst);
    if (argc > 1 && arg_eq(argv[1], L"--update-check"))
        return update_check_main();
    if (argc > 1 && (arg_eq(argv[1], L"--ocr") || arg_eq(argv[1], L"--ocr-file")))
        return ocr_helper(argc, argv);
    if (argc > 2 && arg_eq(argv[1], L"--quick-save"))
        return quick_save(argv[2]);
    if (argc > 1 && arg_eq(argv[1], L"--uninstall"))
        return uninstall_main(argc > 2 && (arg_eq(argv[2], L"/S") || arg_eq(argv[2], L"-S")));

    bool from_cli = false;
    WCHAR *av[64];
    int ac = 0;
    for (int i = 1; i < argc; i++) {
        if (arg_eq(argv[i], L"--daemon"))
            from_cli = true;
        else if (ac < COUNTOF(av))
            av[ac++] = argv[i];
    }

    HANDLE mutex = CreateMutexW(NULL, FALSE, DAEMON_MUTEX);
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        /* Already running: act as a client. No arguments: what a second start does (cmd_run, "_again"). */
        if (from_cli)
            return EXIT_OK;
        HWND d = NULL;
        for (int i = 0; i < 300 && !(d = ipc_find()); i++)
            Sleep(10);
        if (!d)
            return EXIT_FAIL;
        WCHAR *again[1] = {L"_again"};
        Str reply = {0};
        int code = ac ? ipc_call(d, ac, av, &reply) : ipc_call(d, 1, again, &reply);
        if (code != 0 && reply.n)
            MessageBoxW(NULL, reply.p, L"sniplite", MB_OK | MB_ICONWARNING);
        str_free(&reply);
        return code < 0 ? EXIT_FAIL : code;
    }
    int rc = daemon_main(inst, ac, av, from_cli);
    if (mutex)
        CloseHandle(mutex);
    return rc;
}

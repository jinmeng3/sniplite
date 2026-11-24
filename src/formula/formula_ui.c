/* The screenshot editor's CrispEmbed formula command uses an isolated helper process,
 * keeping the model runtime out of the capture process. The helper exits after 60 s idle. */
#include "common.h"
#include "config.h"
#include "formula.h"
#include "image.h"
#include "lang.h"
#include "ov_layout.inc"
#include "shot.h"

extern uint8_t O[];
extern bool ov_active(void);
extern bool formula_installed(void);

#define FORMULA_MAX_PIXELS 16000000u
#define FORMULA_OUTPUT_CAP 65536
#define OI32(f) (*(int32_t *)(O + (f)))
#define ORC(f) ((RECT *)(O + (f)))
uint8_t formula_there;

void formula_refresh(void)
{
    formula_there = formula_installed();
}

static void formula_notice(HWND owner, const WCHAR *text, UINT flags)
{
    MessageBoxW(owner, text, T(F_TITLE), MB_OK | flags);
}

static void formula_compact(WCHAR *text)
{
    WCHAR *read = text, *write = text;
    bool space = false;
    while (*read) {
        WCHAR c = *read++;
        if (c == L' ' || c == L'\t' || c == L'\r' || c == L'\n') {
            space = write != text;
            continue;
        }
        bool tight = c == L'{' || c == L'}' || c == L'_' || c == L'^' ||
                     c == L'=' || c == L'+' || c == L'-' || c == L'*' || c == L'/' || c == L'&';
        WCHAR prev = write > text ? write[-1] : 0;
        bool prev_tight = prev == L'{' || prev == L'}' || prev == L'_' || prev == L'^' ||
                          prev == L'=' || prev == L'+' || prev == L'-' || prev == L'*' || prev == L'/' || prev == L'&';
        if (space && !tight && !prev_tight) *write++ = L' ';
        *write++ = c;
        space = false;
    }
    *write = 0;
}
typedef struct {
    HANDLE worker;
    uint32_t *pixels;
    DWORD bytes, used, code;
    int w, h;
    int backend;
    bool wrote, response, too_long;
    char text[FORMULA_OUTPUT_CAP];
} FormulaJob;
typedef struct { HANDLE process, input, output; } FormulaHost;
static FormulaHost formula_host;

static FormulaJob *current_job; /* UI thread only; also guards nested toolbar commands. */
static WCHAR *last_formula;
static RECT last_formula_sel;
static double last_formula_grab_ms;
static const uint8_t *last_formula_bits;
bool formula_scan(void);

static bool formula_current(void)
{
    if (!ov_active() || OI32(OV_PHASE) != PH_EDIT) return false;
    const Shot *shot = (const Shot *)(O + OV_SHOT);
    const uint8_t *bits = shot->ntile ? shot->tile[0].bits : NULL;
    RECT selection = *ORC(OV_SEL);
    return last_formula && last_formula_grab_ms == shot->grab_ms &&
        last_formula_bits == bits && !memcmp(&last_formula_sel, &selection, sizeof selection);
}

static void formula_host_close(bool terminate)
{
    if (formula_host.process && terminate && WaitForSingleObject(formula_host.process, 0) == WAIT_TIMEOUT)
        TerminateProcess(formula_host.process, 1);
    if (formula_host.input) CloseHandle(formula_host.input);
    if (formula_host.output) CloseHandle(formula_host.output);
    if (formula_host.process) CloseHandle(formula_host.process);
    memset(&formula_host, 0, sizeof formula_host);
}

static bool formula_pipe_write(HANDLE pipe, const void *data, DWORD bytes)
{
    const uint8_t *p = data;
    while (bytes) {
        DWORD wrote = 0;
        if (!WriteFile(pipe, p, bytes, &wrote, NULL) || !wrote) return false;
        p += wrote;
        bytes -= wrote;
    }
    return true;
}

static bool formula_pipe_read(HANDLE pipe, void *data, DWORD bytes)
{
    uint8_t *p = data;
    while (bytes) {
        DWORD got = 0;
        if (!ReadFile(pipe, p, bytes, &got, NULL) || !got) return false;
        p += got;
        bytes -= got;
    }
    return true;
}

static bool formula_host_start(const WCHAR *exe)
{
    if (formula_host.process) {
        if (WaitForSingleObject(formula_host.process, 0) == WAIT_TIMEOUT) return true;
        formula_host_close(false);
    }
    HANDLE child_in = NULL, child_out = NULL, errors = INVALID_HANDLE_VALUE;
    SECURITY_ATTRIBUTES sa = {sizeof sa, NULL, TRUE};
    WCHAR command[MAX_PATH * 2 + 64];
    wsprintfW(command, L"\"%s\" --formula-worker", exe);
    bool ok = false;
    if (!CreatePipe(&child_in, &formula_host.input, &sa, 0) ||
        !CreatePipe(&formula_host.output, &child_out, &sa, 0)) goto done;
    if (!SetHandleInformation(formula_host.input, HANDLE_FLAG_INHERIT, 0) ||
        !SetHandleInformation(formula_host.output, HANDLE_FLAG_INHERIT, 0)) goto done;
    errors = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, NULL);
    if (errors == INVALID_HANDLE_VALUE) goto done;
    STARTUPINFOW si = {sizeof si};
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = child_in;
    si.hStdOutput = child_out;
    si.hStdError = errors;
    PROCESS_INFORMATION pi = {0};
    if (!CreateProcessW(exe, command, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) goto done;
    formula_host.process = pi.hProcess;
    CloseHandle(pi.hThread);
    ok = true;
done:
    if (child_in) CloseHandle(child_in);
    if (child_out) CloseHandle(child_out);
    if (errors != INVALID_HANDLE_VALUE) CloseHandle(errors);
    if (!ok) formula_host_close(false);
    return ok;
}

void formula_preview(void)
{
    bool refresh = !formula_current();
    if (refresh) formula_scan();
    if (!formula_current()) return;
    HWND owner = *(HWND *)(O + OV_HWND);
    RECT selection = *ORC(OV_SEL);
    HMONITOR monitor = MonitorFromRect(&selection, MONITOR_DEFAULTTONEAREST);
    WCHAR detail[320] = {0};
    if (!formula_preview_host(owner, last_formula, monitor, refresh, detail, COUNTOF(detail)))
        formula_notice(owner, detail[0] ? detail : T(F_PREVIEW_ERROR), MB_ICONERROR);
}

static DWORD WINAPI formula_worker(void *ctx)
{
    FormulaJob *j = ctx;
    FormulaRequest req = {(uint32_t)j->w, (uint32_t)j->h, (uint32_t)j->backend};
    j->wrote = formula_pipe_write(formula_host.input, &req, sizeof req) &&
               formula_pipe_write(formula_host.input, j->pixels, j->bytes);
    VirtualFree(j->pixels, 0, MEM_RELEASE);
    j->pixels = NULL;
    if (!j->wrote) {
        formula_host_close(true);
        return 0;
    }
    FormulaReply reply;
    if (!formula_pipe_read(formula_host.output, &reply, sizeof reply)) {
        formula_host_close(true);
        return 0;
    }
    j->code = reply.code;
    if (reply.bytes >= sizeof j->text) {
        j->too_long = true;
        formula_host_close(true);
        return 0;
    }
    j->used = reply.bytes;
    if (j->used && !formula_pipe_read(formula_host.output, j->text, j->used)) {
        formula_host_close(true);
        return 0;
    }
    j->text[j->used] = 0;
    j->response = true;
    return 0;
}

static void formula_wait(HWND owner, HANDLE worker)
{
    MSG msg;
    bool quit = false;
    int quit_code = 0;
    EnableWindow(owner, FALSE);
    while (WaitForSingleObject(worker, 0) != WAIT_OBJECT_0) {
        DWORD wait = MsgWaitForMultipleObjects(1, &worker, FALSE, INFINITE, QS_ALLINPUT);
        if (wait == WAIT_OBJECT_0) break;
        if (wait == WAIT_OBJECT_0 + 1) {
            while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) {
                    quit = true;
                    quit_code = (int)msg.wParam;
                    continue;
                }
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        } else if (wait == WAIT_FAILED) {
            WaitForSingleObject(worker, INFINITE);
            break;
        }
    }
    if (IsWindow(owner)) EnableWindow(owner, TRUE);
    if (quit) PostQuitMessage(quit_code);
}



bool formula_scan(void)
{
    if (current_job || !ov_active() || !formula_there || OI32(OV_PHASE) != PH_EDIT || OI32(OV_DRAG) != DR_NONE)
        return false;

    HWND owner = *(HWND *)(O + OV_HWND);
    const Shot *shot = (const Shot *)(O + OV_SHOT);
    RECT r = *ORC(OV_SEL);
    int w = r.right - r.left, h = r.bottom - r.top;
    if (w <= 0 || h <= 0 || (uint64_t)w * h > FORMULA_MAX_PIXELS) {
        formula_notice(owner, T(F_ERROR), MB_ICONERROR);
        return false;
    }
    FormulaJob *j = mem_alloc(sizeof *j);
    if (!j) { formula_notice(owner, T(F_ERROR), MB_ICONERROR); return false; }
    current_job = j;
    j->w = w;
    j->h = h;
    Config config;
    config_load(&config);
    j->backend = config.formula_backend;
    j->bytes = (DWORD)((size_t)w * h * 4);
    j->pixels = VirtualAlloc(NULL, j->bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    bool failed = true;
    bool copied = false;
    if (!j->pixels) goto cleanup;
    shot_copy(shot, r, j->pixels, w);

    WCHAR exe[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(NULL, exe, COUNTOF(exe));
    if (!n || n >= COUNTOF(exe) || !formula_host_start(exe)) goto cleanup;
    j->worker = CreateThread(NULL, 0, formula_worker, j, 0, NULL);
    if (!j->worker) { formula_host_close(true); goto cleanup; }
    formula_wait(owner, j->worker);
    if (!j->wrote || !j->response || j->too_long || j->code != 0) {
        if (j->code == 4) {
            formula_notice(owner, T(F_NO_RESULT), MB_ICONINFORMATION);
            failed = false;
        }
        goto cleanup;
    }
    int chars = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, j->text, (int)j->used, NULL, 0);
    WCHAR *text = chars > 0 ? mem_alloc((size_t)(chars + 1) * sizeof(WCHAR)) : NULL;
    if (!text) goto cleanup;
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, j->text, (int)j->used, text, chars) == chars) {
        formula_compact(text);
        if (clip_put_text(owner, text)) {
            copied = true;
            mem_free(last_formula);
            last_formula = text;
            last_formula_sel = r;
            last_formula_grab_ms = shot->grab_ms;
            last_formula_bits = shot->ntile ? shot->tile[0].bits : NULL;
            text = NULL;
            failed = false;
        }
    }
    mem_free(text);

cleanup:
    if (j->pixels) VirtualFree(j->pixels, 0, MEM_RELEASE);
    if (j->worker) CloseHandle(j->worker);
    mem_free(j);
    current_job = NULL;
    if (failed) formula_notice(owner, T(F_ERROR), MB_ICONERROR);
    return copied;
}

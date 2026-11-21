/* The screenshot editor's FormulaNet command. It sends the selected pixels to a short-lived
 * helper process so the ONNX Runtime DLL and its model never enter the capture process. */
#include "common.h"
#include "formula.h"
#include "image.h"
#include "lang.h"
#include "ov_layout.inc"
#include "shot.h"
#include "resource.h"
#include <shlwapi.h>

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
    HANDLE process, worker, input, output;
    uint32_t *pixels;
    DWORD bytes, used, code;
    bool wrote, too_long;
    char text[FORMULA_OUTPUT_CAP];
} FormulaJob;

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

void formula_preview(void)
{
    bool refresh = !formula_current();
    if (refresh) formula_scan();
    if (!formula_current()) return;
    WCHAR temp[MAX_PATH], dir[MAX_PATH], page[MAX_PATH], zip[MAX_PATH], asset[MAX_PATH], url[MAX_PATH + 30000];
    DWORD temp_n = GetTempPathW(COUNTOF(temp), temp);
    if (!temp_n || temp_n >= COUNTOF(temp)) return;
    wsprintfW(dir, L"%ssniplite-formula-preview", temp);
    if (!CreateDirectoryW(dir, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) return;
    wsprintfW(page, L"%s\\formula_preview.html", dir);
    wsprintfW(zip, L"%s\\katex.zip", dir);
    wsprintfW(asset, L"%s\\katex\\katex.min.js", dir);
    if (!resource_file(IDR_FORMULA_PREVIEW, page)) return;
    if (GetFileAttributesW(asset) == INVALID_FILE_ATTRIBUTES) {
        if (!resource_file(IDR_KATEX_ZIP, zip)) return;
        WCHAR tar[MAX_PATH];
        UINT sys = GetSystemDirectoryW(tar, COUNTOF(tar));
        if (!sys || sys + 8 >= COUNTOF(tar)) return;
        lstrcatW(tar, L"\\tar.exe");
        WCHAR cmd[MAX_PATH * 3];
        wsprintfW(cmd, L"\"%s\" -xf \"%s\" -C \"%s\"", tar, zip, dir);
        STARTUPINFOW si = {sizeof si};
        PROCESS_INFORMATION pi = {0};
        if (!CreateProcessW(tar, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) return;
        CloseHandle(pi.hThread);
        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD code = 1;
        GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hProcess);
        DeleteFileW(zip);
        if (code) return;
    }
    DWORD cap = COUNTOF(url);
    if (FAILED(UrlCreateFromPathW(page, url, &cap, 0))) return;
    int at = lstrlenW(url);
    const WCHAR *text = last_formula ? last_formula : L"";
    int bytes = WideCharToMultiByte(CP_UTF8, 0, text, -1, NULL, 0, NULL, NULL);
    if (bytes <= 0 || at + bytes * 3 >= COUNTOF(url)) return;
    char *utf8 = mem_alloc(bytes);
    if (!utf8) return;
    WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8, bytes, NULL, NULL);
    static const WCHAR hex[] = L"0123456789ABCDEF";
    url[at++] = L'#';
    for (int i = 0; i < bytes - 1; i++) {
        unsigned char c = (unsigned char)utf8[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~')
            url[at++] = c;
        else {
            url[at++] = L'%'; url[at++] = hex[c >> 4]; url[at++] = hex[c & 15];
        }
    }
    url[at] = 0;
    mem_free(utf8);
    HWND owner = *(HWND *)(O + OV_HWND);
    RECT selection = *ORC(OV_SEL);
    HMONITOR monitor = MonitorFromRect(&selection, MONITOR_DEFAULTTONEAREST);
    if (!formula_preview_host(owner, url, monitor, refresh))
        formula_notice(owner, T(F_PREVIEW_ERROR), MB_ICONERROR);
}

static DWORD WINAPI formula_worker(void *ctx)
{
    FormulaJob *j = ctx;
    DWORD sent = 0, got = 0;
    j->wrote = WriteFile(j->input, j->pixels, j->bytes, &sent, NULL) && sent == j->bytes;
    CloseHandle(j->input); j->input = NULL;
    VirtualFree(j->pixels, 0, MEM_RELEASE); j->pixels = NULL;
    if (!j->wrote) TerminateProcess(j->process, 1);
    while (ReadFile(j->output, j->text + j->used, sizeof j->text - 1 - j->used, &got, NULL) && got) {
        j->used += got;
        if (j->used == sizeof j->text - 1) {
            j->too_long = true;
            TerminateProcess(j->process, 1);
            break;
        }
    }
    CloseHandle(j->output); j->output = NULL;
    WaitForSingleObject(j->process, INFINITE);
    j->code = 1;
    GetExitCodeProcess(j->process, &j->code);
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
    j->bytes = (DWORD)((size_t)w * h * 4);
    j->pixels = VirtualAlloc(NULL, j->bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    HANDLE child_in = NULL, child_out = NULL, errors = INVALID_HANDLE_VALUE;
    bool failed = true;
    bool copied = false;
    if (!j->pixels) goto cleanup;
    shot_copy(shot, r, j->pixels, w);

    WCHAR exe[MAX_PATH * 2], command[MAX_PATH * 2 + 64];
    DWORD n = GetModuleFileNameW(NULL, exe, COUNTOF(exe));
    if (!n || n >= COUNTOF(exe)) goto cleanup;
    wsprintfW(command, L"\"%s\" --formula-raw %d %d", exe, w, h);
    SECURITY_ATTRIBUTES sa = {sizeof sa, NULL, TRUE};
    if (!CreatePipe(&child_in, &j->input, &sa, 0) || !CreatePipe(&j->output, &child_out, &sa, 0)) goto cleanup;
    if (!SetHandleInformation(j->input, HANDLE_FLAG_INHERIT, 0) ||
        !SetHandleInformation(j->output, HANDLE_FLAG_INHERIT, 0)) goto cleanup;
    errors = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, NULL);
    if (errors == INVALID_HANDLE_VALUE) goto cleanup;
    STARTUPINFOW si = {sizeof si};
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = child_in;
    si.hStdOutput = child_out;
    si.hStdError = errors; /* runtime diagnostics must never become clipboard text */
    PROCESS_INFORMATION pi = {0};
    if (!CreateProcessW(exe, command, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) goto cleanup;
    j->process = pi.hProcess;
    CloseHandle(pi.hThread);
    CloseHandle(child_in); child_in = NULL;
    CloseHandle(child_out); child_out = NULL;
    CloseHandle(errors); errors = INVALID_HANDLE_VALUE;

    j->worker = CreateThread(NULL, 0, formula_worker, j, 0, NULL);
    if (!j->worker) { TerminateProcess(j->process, 1); goto cleanup; }
    formula_wait(owner, j->worker);
    if (!j->wrote || j->too_long || j->code != 0) {
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
    if (child_in) CloseHandle(child_in);
    if (child_out) CloseHandle(child_out);
    if (errors != INVALID_HANDLE_VALUE) CloseHandle(errors);
    if (j->input) CloseHandle(j->input);
    if (j->output) CloseHandle(j->output);
    if (j->pixels) VirtualFree(j->pixels, 0, MEM_RELEASE);
    if (j->worker) CloseHandle(j->worker);
    if (j->process) CloseHandle(j->process);
    mem_free(j);
    current_job = NULL;
    if (failed) formula_notice(owner, T(F_ERROR), MB_ICONERROR);
    return copied;
}

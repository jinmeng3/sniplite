/* sniplite.com: console front end. Starts the background process when needed,
 * forwards the command, prints the reply and returns its exit code. */
#include "common.h"
#include "lang.h"
#include "update.h"

static void print(const WCHAR *s, bool err)
{
    HANDLE h = GetStdHandle(err ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
    int n = lstrlenW(s);
    DWORD mode, w;
    if (!n || !h || h == INVALID_HANDLE_VALUE)
        return;
    if (GetConsoleMode(h, &mode)) { /* a real console: UTF-16 straight through, any code page */
        WriteConsoleW(h, s, (DWORD)n, &w, NULL);
        return;
    }
    int bn = WideCharToMultiByte(CP_UTF8, 0, s, n, NULL, 0, NULL, NULL); /* pipes and files get UTF-8 */
    char *b = bn > 0 ? mem_alloc((size_t)bn) : NULL;
    if (!b)
        return;
    WideCharToMultiByte(CP_UTF8, 0, s, n, b, bn, NULL, NULL);
    WriteFile(h, b, (DWORD)bn, &w, NULL);
    mem_free(b);
}

static bool has_json(int argc, WCHAR **argv)
{
    for (int i = 1; i < argc; i++)
        if (arg_eq(argv[i], L"--json"))
            return true;
    return false;
}

static bool main_exe(WCHAR *exe)
{
    DWORD n = GetModuleFileNameW(NULL, exe, MAX_PATH);
    WCHAR *dot = n && n < MAX_PATH ? wcsrchr(exe, L'.') : NULL;
    if (!dot || dot - exe + 4 >= MAX_PATH)
        return false;
    lstrcpyW(dot, L".exe");
    if (GetFileAttributesW(exe) != INVALID_FILE_ATTRIBUTES)
        return true;

    WCHAR *slash = wcsrchr(exe, L'\\');
    if (!slash)
        return false;
    slash[1] = 0;
    static const WCHAR *names[] = {
        L"sniplite.exe",
        L"sniplite_" SNIPLITE_VERSION L"_portable_x64.exe",
        L"sniplite_" SNIPLITE_VERSION L"_portable_with_OCR_x64.exe",
    };
    for (int i = 0; i < COUNTOF(names); i++) {
        size_t dir = (size_t)(slash + 1 - exe), name = wcslen(names[i]);
        if (dir + name >= MAX_PATH)
            continue;
        memcpy(exe + dir, names[i], (name + 1) * sizeof(WCHAR));
        if (GetFileAttributesW(exe) != INVALID_FILE_ATTRIBUTES)
            return true;
    }
    return false;
}

static HWND spawn_daemon(int argc, WCHAR **argv, bool is_start)
{
    WCHAR exe[MAX_PATH];
    if (!main_exe(exe)) {
        print(T(NO_EXE), true);
        return NULL;
    }
    Str cmd = {0};
    str_add_quoted(&cmd, exe);
    str_add(&cmd, L" --daemon");
    if (is_start) /* so --gdi / hotkeys apply before anything is loaded */
        for (int i = 0; i < argc; i++) {
            str_add(&cmd, L" ");
            str_add_quoted(&cmd, argv[i]);
        }
    STARTUPINFOW si = {sizeof si};
    PROCESS_INFORMATION pi;
    /* Break away from the terminal's job object, if allowed, so closing the terminal keeps it running. */
    BOOL ok = cmd.p && CreateProcessW(exe, cmd.p, NULL, NULL, FALSE, CREATE_BREAKAWAY_FROM_JOB, NULL, NULL, &si, &pi);
    if (!ok && cmd.p)
        ok = CreateProcessW(exe, cmd.p, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
    str_free(&cmd);
    if (!ok) {
        print(T(CANNOT_START), true);
        return NULL;
    }
    HWND d = NULL;
    for (int i = 0; i < 1000 && !(d = ipc_find()); i++)
        if (WaitForSingleObject(pi.hProcess, 5) == WAIT_OBJECT_0) {
            d = ipc_find(); /* lost a start race against another instance */
            break;
        }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    if (!d)
        print(T(START_FAILED), true);
    return d;
}

static bool open_url(const WCHAR *url)
{
    HMODULE shell = LoadLibraryW(L"shell32.dll");
    if (!shell)
        return false;
    typedef HINSTANCE(WINAPI *PFN_ShellExecuteW)(HWND, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, INT);
    PFN_ShellExecuteW run = (PFN_ShellExecuteW)(void *)GetProcAddress(shell, "ShellExecuteW");
    bool ok = run && (INT_PTR)run(NULL, L"open", url, NULL, NULL, SW_SHOWNORMAL) > 32;
    FreeLibrary(shell);
    return ok;
}
/* `sniplite update [--install] [--json]`: runs here, no background process needed. */
static int cmd_update(int ac, WCHAR **av, bool json)
{
    bool install = false;
    for (int i = 1; i < ac; i++) {
        if (arg_eq(av[i], L"--install")) {
            install = true;
        } else if (!arg_eq(av[i], L"--json")) {
            print(T(UPD_USAGE), true);
            return EXIT_USAGE;
        }
    }
    static const WCHAR *kState[] = {L"error", L"none", L"latest", L"newer"};
    UpdateResult r;
    update_check(&r);
    bool installed = false, portable_copy = !update_installed_copy(),
         portable = install && r.state == UPD_NEWER && portable_copy, opened = false;
    WCHAR url[256] = {0};
    int code = r.state == UPD_ERROR ? EXIT_FAIL : EXIT_OK;
    if (r.state == UPD_NEWER && portable_copy)
        wsprintfW(url, L"https://github.com" UPDATE_REPO L"/releases/tag/%s", r.tag);
    if (portable) {
        opened = open_url(url);
        code = opened ? EXIT_OK : EXIT_FAIL;
    } else if (install && r.state == UPD_NEWER) {
        WCHAR setup[MAX_PATH];
        if (!json)
            print(T(DOWNLOADING), false);
        code = EXIT_FAIL;
        if (update_download(r.tag, setup, MAX_PATH, NULL, NULL)) {
            Str cmd = {0};
            update_setup_cmd(&cmd, setup);
            STARTUPINFOW si = {sizeof si};
            PROCESS_INFORMATION pi;
            if (cmd.p && CreateProcessW(setup, cmd.p, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
                DWORD rc = 1;
                WaitForSingleObject(pi.hProcess, 120000);
                if (GetExitCodeProcess(pi.hProcess, &rc) && rc == 0) {
                    installed = true;
                    code = EXIT_OK;
                }
                CloseHandle(pi.hThread);
                CloseHandle(pi.hProcess);
            }
            str_free(&cmd);
            DeleteFileW(setup);
        }
    }
    Str out = {0};
    if (json) {
        str_addf(&out, L"{\"state\":\"%s\",\"current\":\"%s\",\"latest\":\"%s\"", kState[r.state],
                 SNIPLITE_VERSION, r.tag);
        if (install)
            str_addf(&out, L",\"installed\":%s", installed ? L"true" : L"false");
        if (r.state == UPD_NEWER && portable_copy)
            str_addf(&out, L",\"portable\":true,\"release_url\":\"%s\"", url);
        if (portable)
            str_addf(&out, L",\"opened\":%s", opened ? L"true" : L"false");
        str_add(&out, L"}\n");
    } else if (installed) {
        str_addf(&out, T(UPDATED), r.tag);
    } else if (portable) {
        if (opened)
            str_addf(&out, T(PORTABLE_UPDATE), url);
        else
            str_addf(&out, T(PORTABLE_NEWER), r.tag, SNIPLITE_VERSION, url);
    } else if (code != EXIT_OK) {
        str_add(&out, r.state == UPD_ERROR ? T(NET_FAILED) : T(UPD_FAILED));
    } else if (r.state == UPD_NEWER && portable_copy) {
        str_addf(&out, T(PORTABLE_NEWER), r.tag, SNIPLITE_VERSION, url);
    } else if (r.state == UPD_NEWER) {
        str_addf(&out, T(NEWER), r.tag, SNIPLITE_VERSION);
    } else if (r.state == UPD_NONE) {
        str_add(&out, T(NO_RELEASE));
    } else {
        str_addf(&out, T(LATEST), SNIPLITE_VERSION);
    }
    if (out.p)
        print(out.p, code != EXIT_OK && !json);
    str_free(&out);
    return code;
}

int wmain(int argc, WCHAR **argv)
{
    lang_init(NULL);
    /* argv[0] is the program; the command starts at argv[1] */
    WCHAR *def[] = {L"start"};
    int ac = argc - 1;
    WCHAR **av = argv + 1;
    if (ac == 0) {
        ac = 1;
        av = def;
    }
    const WCHAR *c = av[0];
    if (arg_eq(c, L"help") || arg_eq(c, L"-h") || arg_eq(c, L"--help") || arg_eq(c, L"/?")) {
        print(L"sniplite " SNIPLITE_VERSION, false);
        print(T(HELP), false);
        return EXIT_OK;
    }
    if (arg_eq(c, L"version") || arg_eq(c, L"--version")) {
        print(L"sniplite " SNIPLITE_VERSION L"\n", false);
        return EXIT_OK;
    }
    bool json = has_json(ac, av);
    if (arg_eq(c, L"update"))
        return cmd_update(ac, av, json);
    bool is_start = arg_eq(c, L"start") || c[0] == L'-';
    HWND d = ipc_find();
    if (!d) {
        if (arg_eq(c, L"status") || arg_eq(c, L"quit") || arg_eq(c, L"exit")) {
            print(json ? L"{\"running\":false}\n" : T(NOT_RUNNING), false);
            return EXIT_NOT_RUNNING;
        }
        d = spawn_daemon(ac, av, is_start);
        if (!d)
            return EXIT_FAIL;
    }
    Str reply = {0};
    int code = ipc_call(d, ac, av, &reply);
    if (code < 0) {
        print(T(NO_CONNECT), true);
        str_free(&reply);
        return EXIT_FAIL;
    }
    if (reply.n)
        print(reply.p, code != EXIT_OK && !json);
    str_free(&reply);
    return code;
}

/* `sniplite.exe --uninstall [/S]`: removes exactly what the installer and the app
 * created, by name: the Start menu folder, the Startup shortcut, the program files
 * and sniplite.ini next to this exe. Never deletes anything recursively; the folder
 * itself is only removed when it is empty. A hidden instance deletes the running exe
 * after this process exits. */
#include "ocr.h"
#include "common.h"
#include "install.h"
#include "lang.h"
#include "settings.h"
#include "formula.h"
#include <shlobj.h>

typedef HRESULT(WINAPI *PFN_SHGetFolderPathW)(HWND, int, HANDLE, DWORD, LPWSTR);

static bool join(WCHAR *out, const WCHAR *a, const WCHAR *b)
{
    if (lstrlenW(a) + lstrlenW(b) + 2 > MAX_PATH)
        return false;
    wsprintfW(out, L"%s\\%s", a, b);
    return true;
}

static bool special(PFN_SHGetFolderPathW get, int csidl, const WCHAR *name, WCHAR *out)
{
    WCHAR base[MAX_PATH];
    return get && SUCCEEDED(get(NULL, csidl, NULL, 0, base)) && join(out, base, name);
}

/* Close a sniplite window (and with it its process) and wait for the process to end,
 * so its exe is no longer in use. */
static void close_wait(HWND w)
{
    if (!w)
        return;
    DWORD pid = 0;
    GetWindowThreadProcessId(w, &pid);
    HANDLE h = pid ? OpenProcess(SYNCHRONIZE, FALSE, pid) : NULL;
    ipc_close(w);
    if (h) {
        WaitForSingleObject(h, 5000);
        CloseHandle(h);
    }
}

/* Launched as a second instance of this exe. It waits for the uninstaller to exit,
 * then removes the executable and its directory using literal Win32 paths. */
int uninstall_cleanup(const WCHAR *pid_arg)
{
    DWORD pid = 0;
    for (const WCHAR *c = pid_arg; *c; c++) {
        if (*c < L'0' || *c > L'9' || pid > (MAXDWORD - (*c - L'0')) / 10)
            return EXIT_FAIL;
        pid = pid * 10 + (*c - L'0');
    }
    if (!pid)
        return EXIT_FAIL;

    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (!process)
        return EXIT_OK;
    WaitForSingleObject(process, INFINITE);
    CloseHandle(process);

    WCHAR exe[MAX_PATH], dir[MAX_PATH];
    DWORD n = GetModuleFileNameW(NULL, exe, MAX_PATH);
    if (!n || n >= MAX_PATH)
        return EXIT_OK;
    lstrcpyW(dir, exe);
    WCHAR *sl = wcsrchr(dir, L'\\');
    if (!sl)
        return EXIT_OK;
    *sl = 0;
    DeleteFileW(exe);
    RemoveDirectoryW(dir); /* only succeeds when empty */
    return EXIT_OK;
}

int uninstall_main(bool silent)
{
    WCHAR exe[MAX_PATH], dir[MAX_PATH], p[MAX_PATH], q[MAX_PATH];
    DWORD n = GetModuleFileNameW(NULL, exe, MAX_PATH);
    if (!n || n >= MAX_PATH)
        return EXIT_FAIL;
    lstrcpyW(dir, exe);
    WCHAR *sl = wcsrchr(dir, L'\\');
    if (!sl)
        return EXIT_FAIL;
    *sl = 0;
    if (!silent &&
        MessageBoxW(NULL, T(UNINST_CONFIRM), T(UNINST_TITLE), MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
        return EXIT_FAIL;

    /* the helper windows first (they could write the settings file again), then the
     * background process and its pins */
    close_wait(ipc_find_dialog(SHORTCUTS_MARK));
    close_wait(ipc_find_dialog(SETTINGS_MARK));
    close_wait(FindWindowW(DAEMON_CLASS, NULL));

    HMODULE sh = LoadLibraryW(L"shell32.dll");
    PFN_SHGetFolderPathW get = sh ? (PFN_SHGetFolderPathW)(void *)GetProcAddress(sh, "SHGetFolderPathW") : NULL;
    if (special(get, CSIDL_PROGRAMS, MENU_FOLDER, p)) {
        if (join(q, p, MENU_APP_LNK))
            DeleteFileW(q);
        for (int i = 0; i < LG_COUNT; i++) { /* named in the language it was installed in */
            WCHAR name[UNINSTALL_LNK_MAX];
            uninstall_lnk(i, name);
            if (join(q, p, name))
                DeleteFileW(q);
        }
        RemoveDirectoryW(p); /* only succeeds when empty */
    }
    if (special(get, CSIDL_STARTUP, STARTUP_LNK, p))
        DeleteFileW(p);

    for (int i = 0; i < 20 && !ocr_remove(); i++) /* the text recognition module; its helper may take a moment to leave */
        Sleep(100);
    for (int i = 0; i < 20 && !formula_remove(); i++) /* the formula worker exits when its pipe closes */
        Sleep(100);
    static const WCHAR *files[] = {
        L"sniplite.com", L"sniplite_qr.exe", L"LICENSE-ZXing.txt", L"LICENSE-MicroTeX.txt", L"LICENSE-TinyXML2.txt",
        L"sniplite.ini", L"sniplite.exe.new", L"sniplite.com.new", L"sniplite_qr.exe.new", L"LICENSE-ZXing.txt.new",
        L"LICENSE-MicroTeX.txt.new", L"LICENSE-TinyXML2.txt.new", L"LICENSE-ZXing.txt.old", L"LICENSE-MicroTeX.txt.old",
        L"LICENSE-TinyXML2.txt.old"};
    for (int i = 0; i < COUNTOF(files); i++)
        if (join(p, dir, files[i]))
            DeleteFileW(p);
    WIN32_FIND_DATAW fd; /* sniplite.exe.old, sniplite.com.old, sniplite.exe.<n>.old */
    HANDLE fh = join(q, dir, L"sniplite.*.old") ? FindFirstFileW(q, &fd) : INVALID_HANDLE_VALUE;
    if (fh != INVALID_HANDLE_VALUE) {
        do
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && join(p, dir, fd.cFileName))
                DeleteFileW(p);
        while (FindNextFileW(fh, &fd));
        FindClose(fh);
    }

    if (!silent)
        MessageBoxW(NULL, T(UNINST_DONE), T(UNINST_TITLE), MB_OK | MB_ICONINFORMATION);

    /* The helper is this executable in a no-UI mode. Its arguments contain only the
     * numeric PID; it derives literal paths from its own module filename. */
    WCHAR cmd[MAX_PATH + 64];
    wsprintfW(cmd, L"\"%s\" --uninstall-cleanup %lu", exe, GetCurrentProcessId());
    STARTUPINFOW si = {sizeof si};
    PROCESS_INFORMATION pi;
    si.dwFlags = STARTF_FORCEOFFFEEDBACK;
    if (CreateProcessW(exe, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, dir, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    return EXIT_OK;
}

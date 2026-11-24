/* sniplite_win_setup_amd64.exe: the installer.
 *
 * Per-user (no admin, no UAC), no registry. Copies the two program files that are
 * embedded as resources into the install folder, adds a Start menu folder (sniplite and
 * its uninstaller), optionally the Startup shortcut, then starts sniplite. The new files
 * are written first (*.new); only then is a running sniplite closed and both files
 * swapped in, or neither. Replaced files are moved aside as *.old (allowed even while
 * they run); sniplite deletes them on its next start.
 *
 * The install folder: the one given with /D=, else where sniplite is installed already
 * (the folder its Start menu shortcut points into), else %LOCALAPPDATA%\Programs\sniplite.
 * A first install may choose another folder in the window; an installation is replaced
 * where it is (to move it, uninstall it first).
 *
 *   sniplite_win_setup_amd64.exe            window: the folder, and two checkboxes: start with
 *                                           Windows, and the text recognition module (off unless
 *                                           ticked: sniplite then downloads it, in its preferences)
 *   sniplite_win_setup_amd64.exe /S         silent; a fresh install turns start with Windows on
 *   sniplite_win_setup_amd64.exe /update    silent, keeps the current choice
 *   sniplite_win_setup_amd64.exe /portable-update silent, replaces existing files in place
 *   ... "/D=C:\some folder\sniplite"        into that folder (a whole path, not a drive alone)
 * Exit code 0 = installed, 1 = failed or cancelled.
 *
 * The window speaks the language sniplite is set to (an installed sniplite.ini),
 * else the language of Windows. */
#include "common.h"
#include "install.h"
#include "resource.h"
#include <shlobj.h>
#include <shobjidl.h>

static struct {
    WCHAR dir[MAX_PATH], exe[MAX_PATH];
    HFONT title_font;
    bool done;
    bool portable_update;
} I;

static bool exists(const WCHAR *p) { return GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES; }

static bool join(WCHAR *out, const WCHAR *a, const WCHAR *b)
{
    if (lstrlenW(a) + lstrlenW(b) + 2 > MAX_PATH)
        return false;
    wsprintfW(out, L"%s\\%s", a, b);
    return true;
}

static bool special(int csidl, const WCHAR *name, WCHAR *out)
{
    WCHAR base[MAX_PATH];
    return SUCCEEDED(SHGetFolderPathW(NULL, csidl, NULL, 0, base)) && join(out, base, name);
}

static bool make_link(const WCHAR *lnk, const WCHAR *args)
{
    IShellLinkW *sl = NULL;
    IPersistFile *pf = NULL;
    bool ok = false;
    if (SUCCEEDED(CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLinkW, (void **)&sl))) {
        IShellLinkW_SetPath(sl, I.exe);
        if (args)
            IShellLinkW_SetArguments(sl, args);
        IShellLinkW_SetWorkingDirectory(sl, I.dir);
        IShellLinkW_SetIconLocation(sl, I.exe, 0);
        if (SUCCEEDED(IShellLinkW_QueryInterface(sl, &IID_IPersistFile, (void **)&pf))) {
            ok = SUCCEEDED(IPersistFile_Save(pf, lnk, TRUE));
            IPersistFile_Release(pf);
        }
        IShellLinkW_Release(sl);
    }
    return ok;
}

static bool autostart_on(void)
{
    WCHAR p[MAX_PATH];
    return special(CSIDL_STARTUP, STARTUP_LNK, p) && exists(p);
}

/* I.dir and I.exe from a path: made whole, without a last backslash. Not a drive by
 * itself: "C:" would even mean the folder the installer was started in. */
static bool set_dir(const WCHAR *dir)
{
    WCHAR full[MAX_PATH];
    int given = lstrlenW(dir);
    while (given > 0 && (dir[given - 1] == L'\\' || dir[given - 1] == L'/'))
        given--;
    DWORD n = given > 0 && dir[given - 1] != L':' ? GetFullPathNameW(dir, MAX_PATH, full, NULL) : 0;
    if (!n || n >= MAX_PATH)
        return false;
    while (n > 0 && full[n - 1] == L'\\')
        full[--n] = 0;
    if (!join(I.exe, full, L"sniplite.exe"))
        return false;
    lstrcpyW(I.dir, full);
    return true;
}

/* Where sniplite is installed: the folder its Start menu shortcut points into, if the
 * program is still there. The shortcut is read as it is, nothing is searched for. */
static bool installed_dir(WCHAR *dir)
{
    WCHAR menu[MAX_PATH], lnk[MAX_PATH], exe[MAX_PATH] = L"";
    IShellLinkW *sl = NULL;
    IPersistFile *pf = NULL;
    if (!special(CSIDL_PROGRAMS, MENU_FOLDER, menu) || !join(lnk, menu, MENU_APP_LNK) || !exists(lnk))
        return false;
    if (SUCCEEDED(CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLinkW, (void **)&sl))) {
        if (SUCCEEDED(IShellLinkW_QueryInterface(sl, &IID_IPersistFile, (void **)&pf))) {
            if (FAILED(IPersistFile_Load(pf, lnk, STGM_READ)) || IShellLinkW_GetPath(sl, exe, MAX_PATH, NULL, SLGP_RAWPATH) != S_OK)
                exe[0] = 0;
            IPersistFile_Release(pf);
        }
        IShellLinkW_Release(sl);
    }
    WCHAR *name = wcsrchr(exe, L'\\');
    if (!name || lstrcmpiW(name + 1, L"sniplite.exe") || !exists(exe))
        return false;
    *name = 0;
    lstrcpyW(dir, exe);
    return true;
}

/* The folder picker of Windows. The program gets a folder of its own in what was picked,
 * unless a folder named sniplite was picked. */
static bool browse(HWND owner)
{
    IFileOpenDialog *d = NULL;
    IShellItem *item = NULL;
    WCHAR *path = NULL;
    bool ok = false;
    if (FAILED(CoCreateInstance(&CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, &IID_IFileOpenDialog, (void **)&d)))
        return false;
    DWORD opt = 0;
    IFileOpenDialog_GetOptions(d, &opt);
    IFileOpenDialog_SetOptions(d, opt | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    if (SUCCEEDED(IFileOpenDialog_Show(d, owner)) && SUCCEEDED(IFileOpenDialog_GetResult(d, &item))) {
        if (SUCCEEDED(IShellItem_GetDisplayName(item, SIGDN_FILESYSPATH, &path))) {
            WCHAR dir[MAX_PATH + 16];
            int n = lstrlenW(path);
            if (n > 0 && n < MAX_PATH) {
                lstrcpyW(dir, path);
                while (n > 0 && dir[n - 1] == L'\\')
                    dir[--n] = 0;
                const WCHAR *last = wcsrchr(dir, L'\\');
                if (lstrcmpiW(last ? last + 1 : dir, L"sniplite"))
                    lstrcatW(dir, L"\\sniplite");
                ok = set_dir(dir);
            }
            CoTaskMemFree(path);
        }
        IShellItem_Release(item);
    }
    IFileOpenDialog_Release(d);
    return ok;
}

/* Close the background process for this install only. A different portable copy can
 * own the daemon window while this folder is being updated. */
static void stop_running(void)
{
    HWND d = FindWindowW(DAEMON_CLASS, NULL);
    if (!d)
        return;
    DWORD pid = 0;
    GetWindowThreadProcessId(d, &pid);
    HANDLE p = pid ? OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid) : NULL;
    if (!p)
        return;
    WCHAR exe[MAX_PATH];
    DWORD n = COUNTOF(exe);
    if (!QueryFullProcessImageNameW(p, 0, exe, &n) || CompareStringOrdinal(exe, -1, I.exe, -1, TRUE) != CSTR_EQUAL) {
        CloseHandle(p);
        return;
    }
    PostMessageW(d, WM_CLOSE, 0, 0);
    WaitForSingleObject(p, 5000);
    CloseHandle(p);
}

/* Program files and their third-party notices, embedded as resources. */
static const struct {
    int id;
    const WCHAR *name;
} kFiles[] = {{IDR_APP_EXE, L"sniplite.exe"}, {IDR_APP_COM, L"sniplite.com"},
              {IDR_APP_QR, L"sniplite_qr.exe"}, {IDR_QR_LICENSE, L"LICENSE-ZXing.txt"},
              {IDR_MICROTEX_LICENSE, L"LICENSE-MicroTeX.txt"}, {IDR_TINYXML2_LICENSE, L"LICENSE-TinyXML2.txt"}};
static bool g_had[COUNTOF(kFiles)]; /* the file existed and was moved to *.old by swap() */

static bool paths(int i, WCHAR *path, WCHAR *tmp, WCHAR *old)
{
    if (I.portable_update && i == 1) {
        WCHAR cli[MAX_PATH];
        if (join(cli, I.dir, L"sniplite-cli.com") && exists(cli)) {
            if (!join(path, I.dir, L"sniplite-cli.com") || !join(tmp, I.dir, L"sniplite.com.new") ||
                !join(old, I.dir, L"sniplite.com.old"))
                return false;
            return lstrlenW(old) < MAX_PATH;
        }
    }
    if (!join(path, I.dir, kFiles[i].name) || lstrlenW(path) + 5 > MAX_PATH)
        return false;
    wsprintfW(tmp, L"%s.new", path);
    wsprintfW(old, L"%s.old", path);
    return true;
}

/* A virus scanner often holds a freshly written exe for a moment: retry for ~2 s. */
static bool move_retry(const WCHAR *from, const WCHAR *to, DWORD flags)
{
    for (int k = 0; k < 20; k++) {
        if (MoveFileExW(from, to, flags))
            return true;
        if (GetFileAttributesW(from) == INVALID_FILE_ATTRIBUTES)
            return false;
        Sleep(100);
    }
    return false;
}

/* Step 1: write <name>.new next to the current file. Nothing in use is touched. */
static bool write_new(int i)
{
    HRSRC r = FindResourceW(NULL, MAKEINTRESOURCEW(kFiles[i].id), (LPCWSTR)RT_RCDATA);
    HGLOBAL g = r ? LoadResource(NULL, r) : NULL;
    const void *data = g ? LockResource(g) : NULL;
    DWORD size = r ? SizeofResource(NULL, r) : 0;
    WCHAR path[MAX_PATH], tmp[MAX_PATH], old[MAX_PATH];
    if (!data || !size || !paths(i, path, tmp, old))
        return false;
    HANDLE f = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE)
        return false;
    DWORD w = 0;
    bool ok = WriteFile(f, data, size, &w, NULL) && w == size;
    if (!CloseHandle(f) || !ok) {
        DeleteFileW(tmp);
        return false;
    }
    return true;
}

/* Step 2: move the current file to <name>.old (Windows allows that even while it
 * runs), then <name>.new into place. On failure the current file is put back. */
static bool swap(int i)
{
    WCHAR path[MAX_PATH], tmp[MAX_PATH], old[MAX_PATH];
    if (!paths(i, path, tmp, old))
        return false;
    /* an .old from the last update may still run (a window left open): move it to a
     * unique name; sniplite deletes every sniplite.*.old on its next start */
    if (!DeleteFileW(old) && exists(old) && lstrlenW(path) + 16 < MAX_PATH) {
        WCHAR aside[MAX_PATH];
        wsprintfW(aside, L"%s.%lu.old", path, GetTickCount());
        MoveFileExW(old, aside, 0);
    }
    g_had[i] = exists(path);
    if (g_had[i] && !move_retry(path, old, MOVEFILE_REPLACE_EXISTING))
        return false;
    if (move_retry(tmp, path, 0))
        return true;
    if (g_had[i])
        move_retry(old, path, 0);
    return false;
}

/* Undo a successful swap(i). */
static void unswap(int i)
{
    WCHAR path[MAX_PATH], tmp[MAX_PATH], old[MAX_PATH];
    if (!paths(i, path, tmp, old))
        return;
    if (g_had[i])
        move_retry(old, path, MOVEFILE_REPLACE_EXISTING);
    else
        for (int k = 0; k < 20 && !DeleteFileW(path) && exists(path); k++)
            Sleep(100);
}

static void delete_new(void)
{
    WCHAR path[MAX_PATH], tmp[MAX_PATH], old[MAX_PATH];
    for (int i = 0; i < COUNTOF(kFiles); i++)
        if (paths(i, path, tmp, old))
            DeleteFileW(tmp);
}

/* autostart: 1 on, 0 off, -1 leave as it is. ocr: start the OCR download after install.
 * portable_update: replace only files present in the target and leave shortcuts alone. */
static bool install(int autostart, bool ocr, bool portable_update)
{
    int e = SHCreateDirectoryExW(NULL, I.dir, NULL);
    if (e != ERROR_SUCCESS && e != ERROR_ALREADY_EXISTS && e != ERROR_FILE_EXISTS)
        return false;
    int count = (int)COUNTOF(kFiles);
    bool update_file[COUNTOF(kFiles)], swapped[COUNTOF(kFiles)] = {0};
    for (int i = 0; i < count; i++) {
        update_file[i] = true;
        if (portable_update && i > 0) {
            WCHAR path[MAX_PATH], tmp[MAX_PATH], old[MAX_PATH];
            if (!paths(i, path, tmp, old)) {
                delete_new();
                return false;
            }
            update_file[i] = exists(path);
        }
        if (update_file[i] && !write_new(i)) {
            delete_new();
            return false;
        }
    }
    stop_running();
    bool ok = true;
    for (int i = 0; i < count; i++) {
        if (update_file[i]) {
            if (!swap(i)) {
                ok = false;
                break;
            }
            swapped[i] = true;
        }
    }
    if (!ok)
        for (int i = count - 1; i >= 0; i--)
            if (swapped[i])
                unswap(i);
    delete_new();
    if (ok && !portable_update) {
        WCHAR menu[MAX_PATH], lnk[MAX_PATH];
        if (special(CSIDL_PROGRAMS, MENU_FOLDER, menu)) {
            CreateDirectoryW(menu, NULL);
            if (join(lnk, menu, MENU_APP_LNK))
                make_link(lnk, NULL);
            WCHAR name[UNINSTALL_LNK_MAX];
            for (int i = 0; i < LG_COUNT; i++) { /* one left by an install in another language */
                uninstall_lnk(i, name);
                if (join(lnk, menu, name))
                    DeleteFileW(lnk);
            }
            lstrcpynW(name, T(UNINST_TITLE), UNINSTALL_LNK_MAX - 4);
            lstrcatW(name, L".lnk");
            if (join(lnk, menu, name))
                make_link(lnk, L"--uninstall");
        }
        if (autostart >= 0 && special(CSIDL_STARTUP, STARTUP_LNK, lnk)) {
            if (autostart)
                make_link(lnk, NULL);
            else
                DeleteFileW(lnk);
        }
    }
    /* Start sniplite again: the new version, or the old one if the swap failed.
     * Break away from a terminal's job (`sniplite update --install`) like the CLI does. */
    if (!exists(I.exe)) { /* last resort: never leave the folder without a working exe */
        WCHAR o[MAX_PATH + 8];
        wsprintfW(o, L"%s.old", I.exe);
        move_retry(o, I.exe, 0);
    }
    if (exists(I.exe)) {
        WCHAR cmd[MAX_PATH + 40];
        wsprintfW(cmd, L"\"%s\" start", I.exe);
        STARTUPINFOW si = {sizeof si};
        PROCESS_INFORMATION pi;
        if (CreateProcessW(I.exe, cmd, NULL, NULL, FALSE, CREATE_BREAKAWAY_FROM_JOB, NULL, I.dir, &si, &pi) ||
            CreateProcessW(I.exe, cmd, NULL, NULL, FALSE, 0, NULL, I.dir, &si, &pi)) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
        if (ok && ocr) {
            wsprintfW(cmd, L"\"%s\" --settings ocr-install", I.exe);
            if (CreateProcessW(I.exe, cmd, NULL, NULL, FALSE, CREATE_BREAKAWAY_FROM_JOB, NULL, I.dir, &si, &pi) ||
                CreateProcessW(I.exe, cmd, NULL, NULL, FALSE, 0, NULL, I.dir, &si, &pi)) {
                CloseHandle(pi.hThread);
                CloseHandle(pi.hProcess);
            }
        }
    }
    return ok;
}

static void show_path(HWND h)
{
    WCHAR t[MAX_PATH + 64];
    wsprintfW(t, T(S_PATH), I.dir);
    SetDlgItemTextW(h, IDC_S_PATH, t);
}

static INT_PTR CALLBACK dlg_proc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    (void)lp;
    switch (m) {
    case WM_INITDIALOG: {
        HINSTANCE inst = GetModuleHandleW(NULL);
        SendMessageW(h, WM_SETICON, ICON_BIG,
                     (LPARAM)LoadImageW(inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, GetSystemMetrics(SM_CXICON),
                                        GetSystemMetrics(SM_CYICON), 0));
        SendMessageW(h, WM_SETICON, ICON_SMALL,
                     (LPARAM)LoadImageW(inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                        GetSystemMetrics(SM_CYSMICON), 0));
        LOGFONTW lf;
        GetObjectW((HFONT)SendMessageW(h, WM_GETFONT, 0, 0), sizeof lf, &lf);
        lf.lfWeight = FW_SEMIBOLD;
        lf.lfHeight = lf.lfHeight * 4 / 3;
        I.title_font = CreateFontIndirectW(&lf);
        SendDlgItemMessageW(h, IDC_S_TITLE, WM_SETFONT, (WPARAM)I.title_font, TRUE);
        SetDlgItemTextW(h, IDC_S_TITLE, L"sniplite " SNIPLITE_VERSION);
        SetWindowTextW(h, T(S_CAPTION));
        SetDlgItemTextW(h, IDC_S_SUB, T(S_SUB));
        SetDlgItemTextW(h, IDC_S_AUTOSTART, T(S_AUTOSTART));
        SetDlgItemTextW(h, IDC_S_OCR, T(S_OCR));
        SetDlgItemTextW(h, IDC_S_BROWSE, T(S_BROWSE));
        SetDlgItemTextW(h, IDOK, T(S_INSTALL));
        SetDlgItemTextW(h, IDCANCEL, T(S_CANCEL));
        show_path(h);
        /* first install: on by default, and the folder can be chosen; reinstall/upgrade:
         * the current choice, where it is */
        CheckDlgButton(h, IDC_S_AUTOSTART, !exists(I.exe) || autostart_on() ? BST_CHECKED : BST_UNCHECKED);
        if (exists(I.exe))
            ShowWindow(GetDlgItem(h, IDC_S_BROWSE), SW_HIDE);
        SetForegroundWindow(h);
        return TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK) {
            if (I.done) {
                EndDialog(h, 0);
                return TRUE;
            }
            EnableWindow(GetDlgItem(h, IDOK), FALSE);
            EnableWindow(GetDlgItem(h, IDCANCEL), FALSE);
            EnableWindow(GetDlgItem(h, IDC_S_BROWSE), FALSE);
            HCURSOR old = SetCursor(LoadCursorW(NULL, IDC_WAIT));
            bool ok = install(IsDlgButtonChecked(h, IDC_S_AUTOSTART) == BST_CHECKED,
                              IsDlgButtonChecked(h, IDC_S_OCR) == BST_CHECKED, false);
            SetCursor(old);
            if (ok) {
                I.done = true;
                WCHAR ini[MAX_PATH]; /* own settings may use other keys */
                bool own = join(ini, I.dir, L"sniplite.ini") && exists(ini);
                SetDlgItemTextW(h, IDC_S_PATH, own ? T(S_DONE_OWN) : T(S_DONE));
                ShowWindow(GetDlgItem(h, IDC_S_AUTOSTART), SW_HIDE);
                ShowWindow(GetDlgItem(h, IDC_S_OCR), SW_HIDE);
                ShowWindow(GetDlgItem(h, IDC_S_BROWSE), SW_HIDE);
                ShowWindow(GetDlgItem(h, IDCANCEL), SW_HIDE);
                SetDlgItemTextW(h, IDOK, T(S_FINISH));
                EnableWindow(GetDlgItem(h, IDOK), TRUE);
                SetFocus(GetDlgItem(h, IDOK));
            } else {
                MessageBoxW(h, T(S_FAILED), T(S_CAPTION), MB_OK | MB_ICONERROR);
                EnableWindow(GetDlgItem(h, IDOK), TRUE);
                EnableWindow(GetDlgItem(h, IDCANCEL), TRUE);
                EnableWindow(GetDlgItem(h, IDC_S_BROWSE), TRUE);
                SendMessageW(h, WM_NEXTDLGCTL, (WPARAM)GetDlgItem(h, IDOK), TRUE);
            }
            return TRUE;
        }
        if (LOWORD(wp) == IDCANCEL) {
            EndDialog(h, I.done ? 0 : 1);
            return TRUE;
        }
        if (LOWORD(wp) == IDC_S_BROWSE && browse(h)) {
            show_path(h);
            return TRUE;
        }
        break;
    case WM_CLOSE:
        EndDialog(h, I.done ? 0 : 1);
        return TRUE;
    }
    return FALSE;
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR cmdline, int show)
{
    (void)prev;
    (void)cmdline;
    (void)show;
    int argc;
    WCHAR **argv = cmdline_split(GetCommandLineW(), &argc);
    bool silent = false, update = false, portable_update = false;
    const WCHAR *given = NULL;
    for (int i = 1; i < argc; i++) {
        if (arg_eq(argv[i], L"/S") || arg_eq(argv[i], L"-S"))
            silent = true;
        else if (arg_eq(argv[i], L"/update") || arg_eq(argv[i], L"--update"))
            update = true;
        else if (arg_eq(argv[i], L"/portable-update") || arg_eq(argv[i], L"--portable-update"))
            portable_update = true;
        else if ((argv[i][0] == L'/' || argv[i][0] == L'-') && (argv[i][1] | 32) == L'd' && argv[i][2] == L'=')
            given = argv[i] + 3;
    }
    I.portable_update = portable_update;
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    WCHAR dir[MAX_PATH], ini[MAX_PATH];
    if (given) { /* a folder that cannot be one is an error, not a reason to install somewhere else */
        if (!set_dir(given))
            return 1;
    } else if (!installed_dir(dir) || !set_dir(dir)) {
        if (!ExpandEnvironmentStringsW(INSTALL_DIR, dir, MAX_PATH) || dir[0] == L'%' || !set_dir(dir))
            return 1;
    }
    lang_init(join(ini, I.dir, L"sniplite.ini") ? ini : L"");
    if (silent || update || portable_update)
        return install(update || portable_update || exists(I.exe) ? -1 : 1, false, portable_update) ? 0 : 1;
    return (int)lang_dialog_box(inst, IDD_SETUP, NULL, dlg_proc);
}

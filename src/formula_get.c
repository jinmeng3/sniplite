#include "formula.h"

#define FORMULA_MODEL_SOURCE L"E:\\浏览器下载\\texo-distill-q8_0.gguf"
#define FORMULA_RUNTIME_SOURCE L"E:\\浏览器下载\\crispembed-windows-x86_64-vulkan\\"
#define FORMULA_MODEL_BYTES 22739744u

typedef struct { const WCHAR *name; DWORD size; } FormulaFile;
static const FormulaFile files[] = {
    {L"texo-distill-q8_0.gguf", FORMULA_MODEL_BYTES},
    {L"crispembed.dll", 7956992u},
    {L"ggml.dll", 69120u},
    {L"ggml-base.dll", 680448u},
    {L"ggml-cpu.dll", 955904u},
    {L"ggml-vulkan.dll", 52247552u},
    {L"vcruntime140.dll", 0},
    {L"vcruntime140_1.dll", 0},
    {L"msvcp140_1.dll", 0},
    {L"msvcp140.dll", 0},
    {L"Apache-2.0.txt", 11560u},
    {L"LICENSE", 4850u},
    {L"NOTICE.marker", 4252u},
    {L"NOTICE.sheenbidi", 433u},
    {L"NOTICE.teleocr", 3276u},
};

static bool paths(const WCHAR *name, WCHAR *dir, WCHAR *path, WCHAR *tmp, int cap)
{
    DWORD n = GetModuleFileNameW(NULL, dir, cap - 16);
    if (!n || n >= (DWORD)(cap - 16)) return false;
    while (n && dir[n - 1] != L'\\' && dir[n - 1] != L'/') n--;
    lstrcpyW(dir + n, L"formula");
    if (lstrlenW(dir) + lstrlenW(name) + 8 >= cap) return false;
    wsprintfW(path, L"%s\\%s", dir, name);
    wsprintfW(tmp, L"%s.part", path);
    return true;
}

static bool file_size(const WCHAR *path, DWORD size)
{
    WIN32_FILE_ATTRIBUTE_DATA d;
    return GetFileAttributesExW(path, GetFileExInfoStandard, &d) &&
           !(d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !d.nFileSizeHigh &&
           (size ? d.nFileSizeLow == size : d.nFileSizeLow != 0);
}

bool formula_installed(void)
{
    WCHAR dir[MAX_PATH * 2], path[MAX_PATH * 2], tmp[MAX_PATH * 2];
    for (int i = 0; i < COUNTOF(files); i++) {
        if (!paths(files[i].name, dir, path, tmp, COUNTOF(dir)) || !file_size(path, files[i].size))
            return false;
    }
    return true;
}

static bool remove_legacy(void)
{
    static const WCHAR *const names[] = {
        L"config.json", L"generation_config.json", L"tokenizer.json", L"tokenizer_config.json",
        L"onnxruntime.dll", L"LICENSE.txt", L"LICENSE-AGPL-3.0.txt",
        L"LICENSE-onnxruntime.txt", L"ThirdPartyNotices-onnxruntime.txt",
    };
    WCHAR dir[MAX_PATH * 2], path[MAX_PATH * 2], tmp[MAX_PATH * 2];
    bool good = true;
    for (int i = 0; i < COUNTOF(names); i++) {
        if (!paths(names[i], dir, path, tmp, COUNTOF(dir))) return false;
        if (!DeleteFileW(path) && GetLastError() != ERROR_FILE_NOT_FOUND && GetLastError() != ERROR_PATH_NOT_FOUND)
            good = false;
    }
    if (!paths(L"onnx\\*", dir, path, tmp, COUNTOF(dir))) return false;
    WIN32_FIND_DATAW fd;
    HANDLE search = FindFirstFileW(path, &fd);
    if (search != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            wsprintfW(tmp, L"%s\\onnx\\%s", dir, fd.cFileName);
            if (!DeleteFileW(tmp) && GetLastError() != ERROR_FILE_NOT_FOUND) good = false;
        } while (FindNextFileW(search, &fd));
        FindClose(search);
    }
    wsprintfW(path, L"%s\\onnx", dir);
    if (!RemoveDirectoryW(path) && GetLastError() != ERROR_FILE_NOT_FOUND && GetLastError() != ERROR_PATH_NOT_FOUND &&
        GetLastError() != ERROR_DIR_NOT_EMPTY) good = false;
    return good;
}

static bool cancelled(volatile LONG *c)
{
    return c && InterlockedCompareExchange(c, 0, 0);
}

static int copy_file(const WCHAR *source, const WCHAR *target, const WCHAR *tmp,
                     DWORD expected, volatile LONG *cancel)
{
    HANDLE in = CreateFileW(source, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (in == INVALID_HANDLE_VALUE) return FORMULA_GET_SOURCE;
    LARGE_INTEGER size;
    if (!GetFileSizeEx(in, &size) || size.QuadPart <= 0 ||
        (expected && size.QuadPart != expected) || (!expected && size.QuadPart > 4 * 1024 * 1024)) {
        CloseHandle(in);
        return FORMULA_GET_CHECK;
    }
    expected = (DWORD)size.QuadPart;
    HANDLE out = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (out == INVALID_HANDLE_VALUE) { CloseHandle(in); return FORMULA_GET_WRITE; }
    uint8_t buffer[65536];
    DWORD left = expected;
    bool ok = true;
    while (left) {
        if (cancelled(cancel)) { ok = false; break; }
        DWORD ask = left < sizeof buffer ? left : (DWORD)sizeof buffer, got = 0, wrote = 0;
        if (!ReadFile(in, buffer, ask, &got, NULL) || got != ask ||
            !WriteFile(out, buffer, got, &wrote, NULL) || wrote != got) {
            ok = false;
            break;
        }
        left -= got;
    }
    CloseHandle(out);
    CloseHandle(in);
    if (cancelled(cancel)) { DeleteFileW(tmp); return FORMULA_GET_CANCEL; }
    if (!ok || left) { DeleteFileW(tmp); return FORMULA_GET_WRITE; }
    if (!MoveFileExW(tmp, target, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tmp);
        return FORMULA_GET_WRITE;
    }
    return FORMULA_GET_OK;
}

int formula_get(volatile LONG *cancel, void (*progress)(int, void *), void *ctx)
{
    WCHAR dir[MAX_PATH * 2], path[MAX_PATH * 2], tmp[MAX_PATH * 2];
    if (!paths(files[0].name, dir, path, tmp, COUNTOF(dir)) ||
        (!CreateDirectoryW(dir, NULL) && GetLastError() != ERROR_ALREADY_EXISTS))
        return FORMULA_GET_WRITE;
    uint64_t total = 0, done = 0;
    for (int i = 0; i < COUNTOF(files); i++) total += files[i].size;
    for (int i = 0; i < COUNTOF(files); i++) {
        if (cancelled(cancel)) return FORMULA_GET_CANCEL;
        if (!paths(files[i].name, dir, path, tmp, COUNTOF(dir))) return FORMULA_GET_WRITE;
        WCHAR source[MAX_PATH * 2];
        if (i == 0) {
            lstrcpynW(source, FORMULA_MODEL_SOURCE, COUNTOF(source));
        } else if (i >= 6 && i <= 9) {
            DWORD n = GetSystemDirectoryW(source, COUNTOF(source));
            if (!n || n + lstrlenW(files[i].name) + 2 >= COUNTOF(source)) return FORMULA_GET_SOURCE;
            lstrcatW(source, L"\\");
            lstrcatW(source, files[i].name);
        } else {
            wsprintfW(source, L"%s%s", FORMULA_RUNTIME_SOURCE, files[i].name);
        }
        int result = copy_file(source, path, tmp, files[i].size, cancel);
        if (result != FORMULA_GET_OK) return result;
        done += files[i].size;
        if (progress) progress((int)(done * 100 / total), ctx);
    }
    return formula_installed() ? (remove_legacy() ? FORMULA_GET_OK : FORMULA_GET_WRITE) : FORMULA_GET_CHECK;
}

int formula_size_mb(void)
{
    WCHAR dir[MAX_PATH * 2], path[MAX_PATH * 2], tmp[MAX_PATH * 2];
    uint64_t bytes = 0;
    for (int i = 0; i < COUNTOF(files); i++) {
        if (!paths(files[i].name, dir, path, tmp, COUNTOF(dir))) return 0;
        WIN32_FILE_ATTRIBUTE_DATA d;
        if (!GetFileAttributesExW(path, GetFileExInfoStandard, &d)) return 0;
        bytes += ((uint64_t)d.nFileSizeHigh << 32) | d.nFileSizeLow;
    }
    return (int)((bytes + 512 * 1024) >> 20);
}

bool formula_remove(void)
{
    WCHAR dir[MAX_PATH * 2], path[MAX_PATH * 2], tmp[MAX_PATH * 2];
    bool good = true;
    if (!paths(L"*", dir, path, tmp, COUNTOF(dir))) return false;
    WIN32_FIND_DATAW fd;
    HANDLE search = FindFirstFileW(path, &fd);
    if (search != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            wsprintfW(tmp, L"%s\\%s", dir, fd.cFileName);
            if (!DeleteFileW(tmp) && GetLastError() != ERROR_FILE_NOT_FOUND) good = false;
        } while (FindNextFileW(search, &fd));
        FindClose(search);
    }
    wsprintfW(path, L"%s\\onnx\\*", dir);
    search = FindFirstFileW(path, &fd);
    if (search != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            wsprintfW(tmp, L"%s\\onnx\\%s", dir, fd.cFileName);
            DeleteFileW(tmp);
        } while (FindNextFileW(search, &fd));
        FindClose(search);
    }
    wsprintfW(path, L"%s\\onnx", dir);
    RemoveDirectoryW(path);
    RemoveDirectoryW(dir);
    return good;
}

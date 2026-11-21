#include "formula.h"
#include "update.h"
#include "ocr.h"
#include "resource.h"

#define FORMULA_REPO L"/alephpi/FormulaNet/resolve/b2668efe5112082846fde4d446b9bfaab3989533/"
#define FORMULA_TOTAL (54168561ull + 25946299ull + 5014ull + 213ull + 17318ull + 936ull + 16783672ull)
static const char kLicenseNotice[] =
    "FormulaNet model by alephpi/FormulaNet\r\n"
    "Pinned revision: b2668efe5112082846fde4d446b9bfaab3989533\r\n"
    "License: GNU Affero General Public License v3.0 (AGPL-3.0)\r\n"
    "License text: https://www.gnu.org/licenses/agpl-3.0.txt\r\n";
#include <bcrypt.h>

typedef NTSTATUS (WINAPI *BCryptOpenAlgorithmProviderFn)(BCRYPT_ALG_HANDLE *, LPCWSTR, LPCWSTR, ULONG);
typedef NTSTATUS (WINAPI *BCryptGetPropertyFn)(BCRYPT_HANDLE, LPCWSTR, PUCHAR, ULONG, ULONG *, ULONG);
typedef NTSTATUS (WINAPI *BCryptCreateHashFn)(BCRYPT_ALG_HANDLE, BCRYPT_HASH_HANDLE *, PUCHAR, ULONG, PUCHAR, ULONG, ULONG);
typedef NTSTATUS (WINAPI *BCryptHashDataFn)(BCRYPT_HASH_HANDLE, PUCHAR, ULONG, ULONG);
typedef NTSTATUS (WINAPI *BCryptFinishHashFn)(BCRYPT_HASH_HANDLE, PUCHAR, ULONG, ULONG);
typedef NTSTATUS (WINAPI *BCryptDestroyHashFn)(BCRYPT_HASH_HANDLE);
typedef NTSTATUS (WINAPI *BCryptCloseAlgorithmProviderFn)(BCRYPT_ALG_HANDLE, ULONG);

typedef struct {
    const WCHAR *name, *url;
    uint32_t size;
    uint8_t sha[32];
} FormulaPart;
static const FormulaPart parts[] = {
    {L"config.json", L"config.json", 5014, {0xc3,0x81,0x6c,0x7e,0xe9,0x0a,0xa2,0x81,0x56,0x91,0xd2,0xab,0x37,0x6b,0xe7,0x04,0x96,0x4b,0x37,0x35,0xeb,0xd2,0x38,0xfb,0x81,0x2f,0xf7,0xe0,0xc0,0xcb,0xe3,0x36}},
    {L"generation_config.json", L"generation_config.json", 213, {0x59,0x72,0x41,0x30,0x4b,0x96,0x11,0xf7,0x29,0xaa,0x85,0x94,0x05,0xee,0x60,0x93,0x4f,0xed,0x6c,0x03,0xb3,0x8d,0xb9,0xdb,0xc4,0xae,0x86,0xdb,0xe3,0xd2,0xfd,0x93}},
    {L"tokenizer.json", L"tokenizer.json", 17318, {0x12,0x40,0xf9,0xd1,0x78,0xe1,0xad,0x2a,0x00,0x76,0xfe,0x95,0xba,0x62,0xe3,0x32,0x87,0x1c,0x70,0x2a,0xcc,0xdd,0x5c,0xe3,0xae,0x3e,0xf3,0x3f,0xfd,0x6c,0x3a,0x1e}},
    {L"tokenizer_config.json", L"tokenizer_config.json", 936, {0x71,0x44,0x79,0x32,0x0c,0xe6,0xdb,0xd8,0x2e,0xf3,0xdf,0x74,0xbe,0x4e,0xf5,0x43,0xf3,0x53,0x5f,0x96,0xf0,0x74,0xea,0x65,0x05,0x6b,0x96,0x28,0x05,0xf2,0xcc,0xf7}},
    {L"onnx\\encoder_model.onnx", L"onnx/encoder_model.onnx", 54168561, {0xfb,0xd6,0x9c,0xf6,0x3c,0xf8,0x33,0xdb,0x1e,0x2e,0xf4,0x00,0x13,0xd8,0x59,0xb5,0x60,0x67,0x1c,0x12,0x53,0x27,0x84,0x41,0xa0,0x1b,0xde,0x45,0x16,0xb6,0x24,0xae}},
    {L"onnx\\decoder_model_merged.onnx", L"onnx/decoder_model_merged.onnx", 25946299, {0x61,0xd4,0xe9,0xe6,0x0e,0x3c,0xaa,0x62,0xaf,0x3f,0x28,0xa1,0x5a,0x22,0xbc,0x13,0x56,0x7e,0xb4,0xe6,0x18,0xc8,0x79,0x17,0xd9,0x59,0x74,0x61,0xe5,0x4c,0x46,0xbe}},
};

typedef struct {
    HANDLE file;
    BCRYPT_HASH_HANDLE hash;
    BCryptHashDataFn hash_data;
    uint32_t have, size;
    volatile LONG *cancel;
    void (*progress)(int, void *);
    void *ctx;
    uint64_t base;
    int shown;
} Fetch;

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
static bool notice_present(WCHAR *dir, WCHAR *path, WCHAR *tmp, int cap)
{
    if (!paths(L"LICENSE.txt", dir, path, tmp, cap)) return false;
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    char b[sizeof kLicenseNotice]; DWORD n = 0;
    bool ok = ReadFile(h, b, sizeof kLicenseNotice - 1, &n, NULL) && n == sizeof kLicenseNotice - 1 && !memcmp(b, kLicenseNotice, n);
    CloseHandle(h); return ok;
}

static bool notice_write(void)
{
    WCHAR dir[MAX_PATH * 2], path[MAX_PATH * 2], tmp[MAX_PATH * 2];
    if (!paths(L"LICENSE.txt", dir, path, tmp, COUNTOF(dir))) return false;
    HANDLE h = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD wrote = 0; bool ok = WriteFile(h, kLicenseNotice, sizeof kLicenseNotice - 1, &wrote, NULL) && wrote == sizeof kLicenseNotice - 1;
    CloseHandle(h);
    if (!ok || !MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) { DeleteFileW(tmp); return false; }
    return true;
}

static bool formula_license_data(const void **data, DWORD *size)
{
    HRSRC resource = FindResourceW(NULL, MAKEINTRESOURCEW(IDR_FORMULA_LICENSE), RT_RCDATA);
    HGLOBAL loaded = resource ? LoadResource(NULL, resource) : NULL;
    *size = resource ? SizeofResource(NULL, resource) : 0;
    *data = loaded ? LockResource(loaded) : NULL;
    return *data && *size;
}

static bool formula_license_present(void)
{
    const void *expected; DWORD size;
    if (!formula_license_data(&expected, &size)) return false;
    WCHAR dir[MAX_PATH * 2], path[MAX_PATH * 2], tmp[MAX_PATH * 2];
    if (!paths(L"LICENSE-AGPL-3.0.txt", dir, path, tmp, COUNTOF(dir))) return false;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER actual;
    if (!GetFileSizeEx(file, &actual) || actual.QuadPart != size) { CloseHandle(file); return false; }
    uint8_t buf[4096]; const uint8_t *want = expected; DWORD at = 0, got;
    bool ok = true;
    while (at < size) {
        DWORD ask = size - at < sizeof buf ? size - at : sizeof buf;
        if (!ReadFile(file, buf, ask, &got, NULL) || got != ask || memcmp(buf, want + at, got)) { ok = false; break; }
        at += got;
    }
    CloseHandle(file);
    return ok;
}

static bool formula_license_write(void)
{
    const void *data; DWORD size;
    if (!formula_license_data(&data, &size)) return false;
    WCHAR dir[MAX_PATH * 2], path[MAX_PATH * 2], tmp[MAX_PATH * 2];
    if (!paths(L"LICENSE-AGPL-3.0.txt", dir, path, tmp, COUNTOF(dir))) return false;
    HANDLE file = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD wrote = 0; bool ok = WriteFile(file, data, size, &wrote, NULL) && wrote == size;
    CloseHandle(file);
    if (!ok || !MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tmp);
        return false;
    }
    return true;
}

static bool runtime_present(WCHAR *dir, WCHAR *path, WCHAR *tmp, int cap)
{
    if (!paths(L"onnxruntime.dll", dir, path, tmp, cap)) return false;
    WIN32_FILE_ATTRIBUTE_DATA d;
    return GetFileAttributesExW(path, GetFileExInfoStandard, &d) && !d.nFileSizeHigh && d.nFileSizeLow == 16783672u;
}

static bool runtime_licenses_present(void)
{
    static const struct { const WCHAR *name; DWORD size; } license[] = {
        {L"LICENSE-onnxruntime.txt", 1073},
        {L"ThirdPartyNotices-onnxruntime.txt", 325054},
    };
    WCHAR dir[MAX_PATH * 2], path[MAX_PATH * 2], tmp[MAX_PATH * 2];
    for (int i = 0; i < COUNTOF(license); i++) {
        if (!paths(license[i].name, dir, path, tmp, COUNTOF(dir))) return false;
        WIN32_FILE_ATTRIBUTE_DATA d;
        if (!GetFileAttributesExW(path, GetFileExInfoStandard, &d) || d.nFileSizeHigh || d.nFileSizeLow != license[i].size)
            return false;
    }
    return true;
}

bool formula_installed(void)
{
    WCHAR dir[MAX_PATH * 2], path[MAX_PATH * 2], tmp[MAX_PATH * 2];
    for (int i = 0; i < COUNTOF(parts); i++) {
        if (!paths(parts[i].name, dir, path, tmp, COUNTOF(dir))) return false;
        WIN32_FILE_ATTRIBUTE_DATA d;
        if (!GetFileAttributesExW(path, GetFileExInfoStandard, &d) || d.nFileSizeHigh || d.nFileSizeLow != parts[i].size) return false;
    }
    return runtime_present(dir, path, tmp, COUNTOF(dir)) && runtime_licenses_present() &&
           formula_license_present() && notice_present(dir, path, tmp, COUNTOF(dir));
}

static bool cancelled(volatile LONG *c) { return c && InterlockedCompareExchange(c, 0, 0); }
static bool sink(const void *data, DWORD n, void *ctx)
{
    Fetch *f = ctx;
    if (cancelled(f->cancel) || n > f->size - f->have) return false;
    DWORD wrote;
    if (!WriteFile(f->file, data, n, &wrote, NULL) || wrote != n || f->hash_data(f->hash, (PUCHAR)data, n, 0) < 0) return false;
    f->have += n;
    int p = (int)((f->base + f->have) * 100 / FORMULA_TOTAL);
    if (p != f->shown && f->progress) f->progress(f->shown = p, f->ctx);
    return true;
}

#define FORMULA_RUNTIME_BYTES 16783672u
typedef struct { void (*progress)(int, void *); void *ctx; uint64_t base; int shown; } RuntimeProgress;
static void runtime_progress(int p, void *ctx)
{
    RuntimeProgress *r = ctx;
    if (!r->progress || p < 0) return;
    uint64_t done = r->base + (uint64_t)FORMULA_RUNTIME_BYTES * (unsigned)p / 100;
    int shown = (int)(done * 98 / FORMULA_TOTAL);
    if (shown != r->shown) r->progress(r->shown = shown, r->ctx);
}

int formula_get(volatile LONG *cancel, void (*progress)(int, void *), void *ctx)
{
    HMODULE bcrypt = LoadLibraryW(L"bcrypt.dll");
    if (!bcrypt) return FORMULA_GET_WRITE;
    BCryptOpenAlgorithmProviderFn open_alg = (void *)GetProcAddress(bcrypt, "BCryptOpenAlgorithmProvider");
    BCryptGetPropertyFn get_prop = (void *)GetProcAddress(bcrypt, "BCryptGetProperty");
    BCryptCreateHashFn create_hash = (void *)GetProcAddress(bcrypt, "BCryptCreateHash");
    BCryptHashDataFn hash_data = (void *)GetProcAddress(bcrypt, "BCryptHashData");
    BCryptFinishHashFn finish_hash = (void *)GetProcAddress(bcrypt, "BCryptFinishHash");
    BCryptDestroyHashFn destroy_hash = (void *)GetProcAddress(bcrypt, "BCryptDestroyHash");
    BCryptCloseAlgorithmProviderFn close_alg = (void *)GetProcAddress(bcrypt, "BCryptCloseAlgorithmProvider");
    if (!open_alg || !get_prop || !create_hash || !hash_data || !finish_hash || !destroy_hash || !close_alg) { FreeLibrary(bcrypt); return FORMULA_GET_WRITE; }
    WCHAR dir[MAX_PATH * 2], path[MAX_PATH * 2], tmp[MAX_PATH * 2];
    if (!paths(parts[0].name, dir, path, tmp, COUNTOF(dir)) || (!CreateDirectoryW(dir, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)) { FreeLibrary(bcrypt); return FORMULA_GET_WRITE; }
    WCHAR sub[MAX_PATH * 2]; wsprintfW(sub, L"%s\\onnx", dir);
    if (!CreateDirectoryW(sub, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) { FreeLibrary(bcrypt); return FORMULA_GET_WRITE; }
    BCRYPT_ALG_HANDLE alg = NULL;
    if (open_alg(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0) < 0) { FreeLibrary(bcrypt); return FORMULA_GET_WRITE; }
    DWORD objlen = 0, got = 0;
    bool good = get_prop(alg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&objlen, sizeof objlen, &got, 0) >= 0 && objlen;
    uint8_t *obj = good ? mem_alloc(objlen) : NULL;
    if (!obj) good = false;
    int result = good ? FORMULA_GET_OK : FORMULA_GET_WRITE;
    uint64_t base = 0;
    for (int i = 0; good && i < COUNTOF(parts); i++) {
        if (cancelled(cancel)) { result = FORMULA_GET_CANCEL; break; }
        const FormulaPart *p = &parts[i];
        if (!paths(p->name, dir, path, tmp, COUNTOF(dir))) { result = FORMULA_GET_WRITE; break; }
        WCHAR url[256];
        wsprintfW(url, FORMULA_REPO L"%s?download=true", p->url);
        BCRYPT_HASH_HANDLE hash = NULL;
        if (create_hash(alg, &hash, obj, objlen, NULL, 0, 0) < 0) { result = FORMULA_GET_WRITE; break; }
        HANDLE file = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (file == INVALID_HANDLE_VALUE) { destroy_hash(hash); result = FORMULA_GET_WRITE; break; }
        Fetch f = {file, hash, hash_data, 0, p->size, cancel, progress, ctx, base, -1};
        DWORD status = 0;
        bool fetched = http_fetch(L"huggingface.co", url, NULL, &status, sink, &f) && status == 200 && f.have == p->size;
        uint8_t digest[32];
        bool verified = fetched && finish_hash(hash, digest, sizeof digest, 0) >= 0 && !memcmp(digest, p->sha, 32);
        CloseHandle(file); destroy_hash(hash);
        if (cancelled(cancel)) result = FORMULA_GET_CANCEL;
        else if (!fetched) result = FORMULA_GET_NET;
        else if (!verified) result = FORMULA_GET_CHECK;
        else if (!MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) result = FORMULA_GET_WRITE;
        if (result != FORMULA_GET_OK) { DeleteFileW(tmp); break; }
        base += p->size;
    }
    mem_free(obj);
    close_alg(alg, 0); FreeLibrary(bcrypt);
    if (result == FORMULA_GET_OK) {
        RuntimeProgress rp = {progress, ctx, base, -1};
        int runtime_result = ocr_runtime_get(dir, cancel, runtime_progress, &rp);
        if (runtime_result == OCR_GET_CANCEL) result = FORMULA_GET_CANCEL;
        else if (runtime_result == OCR_GET_NET) result = FORMULA_GET_NET;
        else if (runtime_result == OCR_GET_CHECK) result = FORMULA_GET_CHECK;
        else if (runtime_result != OCR_GET_OK) result = FORMULA_GET_WRITE;
    }
    if (result == FORMULA_GET_OK &&
        (!runtime_present(dir, path, tmp, COUNTOF(dir)) || !runtime_licenses_present() || !notice_write() || !formula_license_write()))
        result = FORMULA_GET_WRITE;
    if (result == FORMULA_GET_OK && progress) progress(100, ctx);
    return result;
}

int formula_size_mb(void) { return (int)((FORMULA_TOTAL + 512 * 1024) >> 20); }

bool formula_remove(void)
{
    WCHAR dir[MAX_PATH * 2], path[MAX_PATH * 2], tmp[MAX_PATH * 2];
    bool good = true;
    for (int i = 0; i < COUNTOF(parts); i++) {
        if (!paths(parts[i].name, dir, path, tmp, COUNTOF(dir))) return false;
        if (!DeleteFileW(path) && GetLastError() != ERROR_FILE_NOT_FOUND && GetLastError() != ERROR_PATH_NOT_FOUND) good = false;
        DeleteFileW(tmp);
    }
    if (paths(L"LICENSE.txt", dir, path, tmp, COUNTOF(dir))) { DeleteFileW(path); DeleteFileW(tmp); }
    if (paths(L"onnxruntime.dll", dir, path, tmp, COUNTOF(dir))) { DeleteFileW(path); DeleteFileW(tmp); }
    WIN32_FIND_DATAW fd;
    if (paths(L"*", dir, path, tmp, COUNTOF(dir))) {
        HANDLE f = FindFirstFileW(path, &fd);
        if (f != INVALID_HANDLE_VALUE) {
            do { if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) { wsprintfW(tmp, L"%s\\%s", dir, fd.cFileName); DeleteFileW(tmp); } } while (FindNextFileW(f, &fd));
            FindClose(f);
        }
    }
    wsprintfW(path, L"%s\\onnx", dir); RemoveDirectoryW(path);
    RemoveDirectoryW(dir);
    return good;
}

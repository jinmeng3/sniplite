/* Settings file and hotkey names.
 * sniplite.ini lives next to the exe and is only written from the preferences window;
 * without it everything uses the defaults. */
#include "config.h"
#include "lang.h"

static const struct {
    const WCHAR *name;
    UINT vk;
} kKeys[] = {
    {L"PrtSc", VK_SNAPSHOT}, {L"PrintScreen", VK_SNAPSHOT}, {L"Print", VK_SNAPSHOT}, {L"Insert", VK_INSERT},
    {L"Ins", VK_INSERT},     {L"Delete", VK_DELETE},       {L"Del", VK_DELETE},     {L"Home", VK_HOME},
    {L"End", VK_END},        {L"PgUp", VK_PRIOR},          {L"PageUp", VK_PRIOR},   {L"PgDn", VK_NEXT},
    {L"PageDown", VK_NEXT},  {L"Pause", VK_PAUSE},         {L"ScrollLock", VK_SCROLL}, {L"Space", VK_SPACE},
    {L"Tab", VK_TAB},        {L"`", VK_OEM_3},             {L"Up", VK_UP},          {L"Down", VK_DOWN},
    {L"Left", VK_LEFT},      {L"Right", VK_RIGHT},
};

bool hotkey_parse(const WCHAR *s, UINT *mod, UINT *vk)
{
    *mod = *vk = 0;
    if (!*s || arg_eq(s, L"none") || arg_eq(s, L"off"))
        return true;
    WCHAR buf[64];
    lstrcpynW(buf, s, COUNTOF(buf));
    for (WCHAR *tok = buf, *next; tok; tok = next) {
        next = wcschr(tok, L'+');
        if (next)
            *next++ = 0;
        if (!*tok)
            return false;
        if (arg_eq(tok, L"ctrl") || arg_eq(tok, L"control"))
            *mod |= MOD_CONTROL;
        else if (arg_eq(tok, L"alt"))
            *mod |= MOD_ALT;
        else if (arg_eq(tok, L"shift"))
            *mod |= MOD_SHIFT;
        else if (arg_eq(tok, L"win"))
            *mod |= MOD_WIN;
        else if (*vk)
            return false; /* two keys */
        else if (!tok[1] && ((tok[0] >= L'a' && tok[0] <= L'z') || (tok[0] >= L'A' && tok[0] <= L'Z') ||
                             (tok[0] >= L'0' && tok[0] <= L'9')))
            *vk = (UINT)(tok[0] >= L'a' ? tok[0] - 32 : tok[0]);
        else if ((tok[0] == L'f' || tok[0] == L'F') && tok[1] >= L'1' && tok[1] <= L'9') {
            int n = 0;
            for (WCHAR *p = tok + 1; *p; p++) {
                if (*p < L'0' || *p > L'9')
                    return false;
                n = n * 10 + (*p - L'0');
            }
            if (n < 1 || n > 24)
                return false;
            *vk = VK_F1 + (UINT)n - 1;
        } else if ((tok[0] == L'n' || tok[0] == L'N') && !wcsncmp(tok + 1, L"um", 2) && tok[3] >= L'0' &&
                   tok[3] <= L'9' && !tok[4]) {
            *vk = VK_NUMPAD0 + (UINT)(tok[3] - L'0');
        } else {
            for (int i = 0; i < COUNTOF(kKeys) && !*vk; i++)
                if (arg_eq(tok, kKeys[i].name))
                    *vk = kKeys[i].vk;
            if (!*vk)
                return false;
        }
    }
    return *vk != 0;
}

void hotkey_format(UINT mod, UINT vk, WCHAR *out)
{
    out[0] = 0;
    if (!vk) {
        lstrcpyW(out, L"none");
        return;
    }
    if (mod & MOD_CONTROL) lstrcatW(out, L"Ctrl+");
    if (mod & MOD_ALT) lstrcatW(out, L"Alt+");
    if (mod & MOD_SHIFT) lstrcatW(out, L"Shift+");
    if (mod & MOD_WIN) lstrcatW(out, L"Win+");
    WCHAR k[16] = {0};
    if (vk >= VK_F1 && vk <= VK_F24)
        wsprintfW(k, L"F%u", vk - VK_F1 + 1);
    else if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9'))
        k[0] = (WCHAR)vk;
    else if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9)
        wsprintfW(k, L"Num%u", vk - VK_NUMPAD0);
    else
        for (int i = 0; i < COUNTOF(kKeys); i++)
            if (kKeys[i].vk == vk) {
                lstrcpyW(k, kKeys[i].name);
                break;
            }
    lstrcatW(out, k);
}

void config_defaults(Config *c)
{
    memset(c, 0, sizeof *c);
    hotkey_parse(L"F1", &c->mod[HK_CAPTURE], &c->vk[HK_CAPTURE]);
    hotkey_parse(L"Shift+F1", &c->mod[HK_QUICK], &c->vk[HK_QUICK]);
    hotkey_parse(L"F3", &c->mod[HK_PIN], &c->vk[HK_PIN]);
    hotkey_parse(L"Shift+F3", &c->mod[HK_TOGGLE], &c->vk[HK_TOGGLE]);
    hotkey_parse(L"F4", &c->mod[HK_OCR], &c->vk[HK_OCR]);
    hotkey_parse(L"Shift+F4", &c->mod[HK_OCRCOPY], &c->vk[HK_OCRCOPY]);
    c->tray = true;
    c->pin_top = true;
    c->auto_update = true;
    c->exit_ask = true;
}

void config_path(WCHAR *out, int cap) { ini_path(out, cap); }

static const WCHAR *kHkKeys[HK_COUNT] = {NULL,           L"hotkey",     L"quick_hotkey",   L"pin_hotkey",
                                         L"toggle_hotkey", L"ocr_hotkey", L"ocr_copy_hotkey"};
static const WCHAR *kThemes[] = {L"auto", L"light", L"dark"}; /* by THEME_* */

/* A value that may be any text (a folder's name). The file is UTF-8, which the profile
 * functions read as the ANSI code page: fine for the keys and values in ASCII, not for this. */
static void ini_utf8(const WCHAR *path, const char *key, WCHAR *out, int cap)
{
    out[0] = 0;
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE)
        return;
    DWORD size = GetFileSize(f, NULL), n = 0;
    char *text = size < 1 << 20 ? mem_alloc(size + 1) : NULL;
    if (text && ReadFile(f, text, size, &n, NULL)) {
        text[n] = 0;
        int klen = lstrlenA(key);
        for (char *p = text; *p;) {
            char *end = p;
            while (*end && *end != '\n')
                end++;
            char *next = *end ? end + 1 : end, *v = p + klen;
            while (end > p && (end[-1] == '\r' || end[-1] == ' ' || end[-1] == '\t'))
                end--;
            if (end - p > klen && CompareStringA(LOCALE_INVARIANT, NORM_IGNORECASE, p, klen, key, klen) == CSTR_EQUAL) {
                while (v < end && (*v == ' ' || *v == '\t'))
                    v++;
                if (v < end && *v == '=') {
                    for (v++; v < end && (*v == ' ' || *v == '\t'); v++) {
                    }
                    int w = MultiByteToWideChar(CP_UTF8, 0, v, (int)(end - v), out, cap - 1);
                    out[w > 0 ? w : 0] = 0;
                }
            }
            p = next;
        }
    }
    mem_free(text);
    CloseHandle(f);
}

bool config_load(Config *c)
{
    config_defaults(c);
    WCHAR path[MAX_PATH];
    config_path(path, MAX_PATH);
    if (!path[0] || GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES)
        return false;
    WCHAR v[64];
    for (int id = 1; id < HK_COUNT; id++) {
        UINT mod, vk;
        GetPrivateProfileStringW(L"sniplite", kHkKeys[id], L"\x1F", v, COUNTOF(v), path);
        if (v[0] != 0x1F && hotkey_parse(v, &mod, &vk)) { /* a bad value keeps the default */
            c->mod[id] = mod;
            c->vk[id] = vk;
        }
    }
    GetPrivateProfileStringW(L"sniplite", L"capture", L"dxgi", v, COUNTOF(v), path);
    c->gdi = arg_eq(v, L"gdi");
    c->tray = GetPrivateProfileIntW(L"sniplite", L"tray", 1, path) != 0;
    c->pin_top = GetPrivateProfileIntW(L"sniplite", L"pin_top", 1, path) != 0;
    c->auto_update = GetPrivateProfileIntW(L"sniplite", L"auto_update", 1, path) != 0;
    c->exit_ask = GetPrivateProfileIntW(L"sniplite", L"exit_ask", 1, path) != 0;
    GetPrivateProfileStringW(L"sniplite", L"theme", kThemes[THEME_AUTO], v, COUNTOF(v), path);
    for (int i = 0; i < COUNTOF(kThemes); i++)
        if (arg_eq(v, kThemes[i]))
            c->theme = i;
    ini_utf8(path, "quick_dir", c->quick_dir, COUNTOF(c->quick_dir));
    return true;
}

bool config_save(const Config *c)
{
    WCHAR path[MAX_PATH];
    config_path(path, MAX_PATH);
    if (!path[0])
        return false;
    Str s = {0};
    str_add(&s, T(INI_1));
    str_add(&s, T(INI_2));
    str_add(&s, T(INI_3));
    str_add(&s, L"[sniplite]\r\n");
    for (int id = 1; id < HK_COUNT; id++) {
        WCHAR k[40];
        hotkey_format(c->mod[id], c->vk[id], k);
        str_addf(&s, L"%s=%s\r\n", kHkKeys[id], k);
    }
    str_addf(&s, L"capture=%s\r\ntray=%d\r\nauto_update=%d\r\npin_top=%d\r\n", c->gdi ? L"gdi" : L"dxgi", c->tray ? 1 : 0,
             c->auto_update ? 1 : 0, c->pin_top ? 1 : 0);
    str_addf(&s, L"exit_ask=%d\r\ntheme=%s\r\nquick_dir=", c->exit_ask ? 1 : 0, kThemes[c->theme]);
    str_add(&s, c->quick_dir); /* by itself: str_addf stops at 1024 characters */
    str_add(&s, L"\r\n");
    if (lang_choice() != LG_AUTO) /* kept by lang.c; without the line the language follows Windows */
        str_addf(&s, L"lang=%s\r\n", lang_code(lang_choice()));
    /* UTF-8 without BOM: Notepad shows the comments, and the ASCII keys/values
     * read back fine through GetPrivateProfileString. */
    int n = WideCharToMultiByte(CP_UTF8, 0, s.p, s.n, NULL, 0, NULL, NULL);
    char *b = n > 0 ? mem_alloc((size_t)n) : NULL;
    bool ok = false;
    if (b) {
        WideCharToMultiByte(CP_UTF8, 0, s.p, s.n, b, n, NULL, NULL);
        HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        DWORD w = 0;
        if (f != INVALID_HANDLE_VALUE) {
            ok = WriteFile(f, b, (DWORD)n, &w, NULL) && w == (DWORD)n;
            CloseHandle(f);
        }
        mem_free(b);
    }
    str_free(&s);
    return ok;
}

void config_set_pin_top(bool on)
{
    Config c;
    config_load(&c); /* what the file says, not what a command line changed for this run */
    c.pin_top = on;
    config_save(&c);
}

void config_set_exit_ask(bool on)
{
    Config c;
    config_load(&c);
    c.exit_ask = on;
    config_save(&c);
}

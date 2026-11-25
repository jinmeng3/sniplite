#ifndef SNIP_UNINSTALL_CLEANUP_H
#define SNIP_UNINSTALL_CLEANUP_H

#include <stdbool.h>
#include <stddef.h>
#include <wchar.h>
#include <windows.h>

static inline bool tick_backup_name(const WCHAR *name, const WCHAR *base)
{
    size_t n = wcslen(name), b = wcslen(base);
    if (n < b + 6 || n > b + 15 ||
        CompareStringOrdinal(name, (int)b, base, (int)b, TRUE) != CSTR_EQUAL ||
        name[b] != L'.' || lstrcmpiW(name + n - 4, L".old"))
        return false;
    size_t digits = n - b - 5;
    for (size_t i = 0; i < digits; i++)
        if (name[b + 1 + i] < L'0' || name[b + 1 + i] > L'9')
            return false;
    return true;
}

#endif

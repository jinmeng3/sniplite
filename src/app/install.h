/* Where the installer puts things, shared by the installer and the uninstaller.
 * Per-user, no admin rights, no registry: the Start menu shortcut is the only record of
 * where sniplite was installed, and the installer reads it to find an installation. */
#pragma once
#include "lang.h"

#define INSTALL_DIR L"%LOCALAPPDATA%\\Programs\\sniplite" /* unless another folder is chosen */
#define MENU_FOLDER L"sniplite"                  /* under the user's Start menu Programs */
#define MENU_APP_LNK L"sniplite.lnk"
#define STARTUP_LNK L"sniplite.lnk"              /* under the user's Startup folder (same as the preferences) */

/* The uninstaller's shortcut in MENU_FOLDER is named in the installer's language
 * ("Uninstall sniplite.lnk", "卸载 sniplite.lnk"). The installer writes the one of its
 * language and removes the others; the uninstaller removes them all. */
#define UNINSTALL_LNK_MAX 64
static inline void uninstall_lnk(int lang, WCHAR *out)
{
    lstrcpynW(out, lang_text(lang, S_UNINST_TITLE), UNINSTALL_LNK_MAX - 4);
    lstrcatW(out, L".lnk");
}

int uninstall_main(bool silent); /* `sniplite.exe --uninstall [/S]` */

/* Preferences window. Runs as `SnapRail.exe --settings` so its DLLs (comctl32,
 * COM, shell) never load into the background process. */
#pragma once
#include "common.h"

/* Window properties that mark the two dialogs for ipc_find_dialog. */
#define SETTINGS_MARK L"sniplite.settings"
#define SHORTCUTS_MARK L"sniplite.shortcuts"

int settings_main(HINSTANCE inst, const WCHAR *page); /* `SnapRail.exe --settings [update|ocr|qr|formula|rack]` */
/* main.c, in the background program: open that window; page selects a named tab. */
bool settings_open(const WCHAR *page);
int shortcuts_main(HINSTANCE inst); /* `SnapRail.exe --shortcuts`: the short shortcut list */

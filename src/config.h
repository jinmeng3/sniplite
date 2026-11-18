/* Settings (sniplite.ini next to the exe) and hotkey text <-> key codes. */
#pragma once
#include "common.h"

/* the last two are for text: taken from the system only while the text recognition module is there */
enum { HK_CAPTURE = 1, HK_QUICK, HK_PIN, HK_TOGGLE, HK_OCR, HK_OCRCOPY, HK_COUNT };
enum { THEME_AUTO, THEME_LIGHT, THEME_DARK }; /* the capture's bars: as Windows is set, or always one */

typedef struct {
    UINT mod[HK_COUNT], vk[HK_COUNT]; /* index HK_*; vk 0 = no hotkey */
    bool gdi;                         /* GDI capture instead of DXGI */
    bool tray;                        /* show the notification-area icon */
    bool pin_top;                     /* every pin stays above other windows */
    bool auto_update;                 /* look for a newer release after start-up, then daily */
    bool exit_ask;                    /* ask before a capture with a selection is left (Esc, the close button) */
    int theme;                        /* THEME_* */
    WCHAR quick_dir[MAX_PATH];        /* where a quick capture is saved; empty: the desktop */
} Config;

void config_defaults(Config *c);             /* Snipaste-compatible: F1 / F3 / Shift+F3; Shift+F1 quick; for text F4 / Shift+F4 */
bool config_load(Config *c);                 /* defaults + sniplite.ini if present */
bool config_save(const Config *c);
void config_set_pin_top(bool on);            /* that one setting, changed from a pin's menu: into the file */
void config_set_exit_ask(bool on);           /* and that one, from the question itself */
void config_path(WCHAR *out, int cap);

bool hotkey_parse(const WCHAR *s, UINT *mod, UINT *vk); /* "Ctrl+Alt+A", "F1", "none" */
void hotkey_format(UINT mod, UINT vk, WCHAR *out);      /* out: >= 40 chars; "none" if vk == 0 */

/* Clipboard, image files and the save dialog. */
#pragma once
#include "common.h"

bool clip_put_image(HWND owner, const uint32_t *px, int stride, int w, int h);
bool clip_put_text(HWND owner, const WCHAR *text);
/* Image, copied image file or (if text is set) text from the clipboard. Returns EXIT_OK,
 * EXIT_NOTHING (nothing usable) or EXIT_FAIL. */
int clip_get_image(HWND owner, Img *out, bool text);

bool image_load(const WCHAR *path, Img *out);
bool image_save(const WCHAR *path, const uint32_t *px, int stride, int w, int h); /* by extension */
bool text_image(const WCHAR *text, Img *out); /* sized for the monitor under the cursor */
bool text_image_at(const WCHAR *text, float scale, int max_width, Img *out);

/* CoCreateInstance with COM (STA) initialized on first use; ole32 is loaded lazily. */
HRESULT com_create(const CLSID *cls, const IID *iid, void **out);

/* Ask for a file name. The dialog runs in a short-lived helper process so the
 * shell's DLLs never stay loaded in the background process. */
bool save_dialog(WCHAR *path, int cap);
/* main.c: the helper's arguments, as the settings say, and what is kept of the file chosen */
void save_dialog_args(Str *cmd);
void save_dialog_done(const WCHAR *path);
/* main.c, in the helper: the file the dialog begins with (file: MAX_PATH * 2 characters) */
void save_dialog_file(int argc, WCHAR **argv, WCHAR *file);
int save_dialog_helper(int argc, WCHAR **argv); /* body of `sniplite.exe --save-dialog` */

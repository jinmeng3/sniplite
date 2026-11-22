/* Pinned images: borderless topmost windows. */
#pragma once
#include "common.h"

bool pin_init(HINSTANCE inst, HWND daemon);
/* Takes ownership of img. at_cursor: center on the cursor and shrink to fit the monitor;
 * otherwise the top-left goes to (x, y) at 100%. */
bool pin_add(Img *img, int x, int y, bool at_cursor);
void pin_also_save(void); /* right after pin_add pinned a capture: also_save for its picture */
void pin_toggle_all(void);
void pin_top_all(bool on); /* the setting: every pin above other windows; off, only those switched on by themselves */
void pin_rack_bottom(bool bottom);
bool pin_any_visible(void);
void pin_close_all(void);
int pin_count(void);

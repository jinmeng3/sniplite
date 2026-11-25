#ifndef SNIP_SCROLL_BUDGET_H
#define SNIP_SCROLL_BUDGET_H

#include <stdbool.h>

#define SCROLL_MAX_ROWS 32000
#define SCROLL_MAX_PIXELS 40000000

static inline bool scroll_canvas_budget(int w, int h, int *max_rows)
{
    if (w <= 0 || h <= 0 || !max_rows)
        return false;
    int cap = SCROLL_MAX_ROWS;
    int pixel_rows = SCROLL_MAX_PIXELS / w;
    if (pixel_rows < cap)
        cap = pixel_rows;
    if (cap <= 0 || h > cap / 2)
        return false;
    *max_rows = cap;
    return true;
}

#endif

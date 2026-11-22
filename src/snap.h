/* Candidate detection. Called on the overlay thread; rectangles use physical
 * screen coordinates. The worker owns all UI Automation interfaces. */
#pragma once
#include "common.h"

#define WM_APP_SNAP_READY (WM_APP + 5)

void snap_begin(HWND overlay);
void snap_request(HWND target, const RECT *bounds, POINT screen);
int snap_take(RECT *rects, int capacity);
void snap_end(void);

/* Overlay integration (the assembly state layout stays unchanged). */
void snap_update(void);
void snap_remember(void);
/* Presentation only: OV_HL remains the exact selection/copy target. */
void snap_visual(RECT *out);

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Windows x64, SSE2. Writes exactly count bytes, each 0 or 1; NaNs compare false.
// Input and output must be disjoint. A zero count permits null pointers.
void sniplite_detect_threshold_sse2(const float *prob, uint8_t *binary,
                                    size_t count, float threshold);

// One 3x3 dilation of a tightly packed 0/1 bitmap; outside pixels are zero.
// src and dst must be disjoint, each width*height bytes. Nonpositive dimensions
// perform no accesses. Every output byte is written, including the border.
void sniplite_detect_dilate3x3_sse2(const uint8_t *src, uint8_t *dst,
                                    int width, int height);

#ifdef __cplusplus
}
#endif

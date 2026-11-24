#ifndef SNIPLITE_PIXEL_OPS_H
#define SNIPLITE_PIXEL_OPS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Windows x64, SSE2. Buffers must be disjoint; alignment is unrestricted.
 * Read exactly count BGRA pixels, ignore alpha, and write exactly count RGB
 * triples or gray bytes. Gray is (77*r + 150*g + 29*b) >> 8, without rounding.
 * No memory is accessed when count is zero (NULL pointers are then allowed).
 * The caller provides count*4 readable bytes and count*3/count writable bytes.
 */
void pixel_bgra_to_rgb(unsigned char *dst, const unsigned char *src, size_t count);
void pixel_bgra_to_gray(unsigned char *dst, const unsigned char *src, size_t count);

#ifdef __cplusplus
}
#endif

#endif

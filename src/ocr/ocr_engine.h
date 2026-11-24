#ifndef SNIPLITE_OCR_ENGINE_H
#define SNIPLITE_OCR_ENGINE_H

#include <stddef.h>

#if defined(_WIN32) && defined(SNIPLITE_OCR_BUILD)
#define SNIPLITE_OCR_API __declspec(dllexport)
#else
#define SNIPLITE_OCR_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SnipliteOcrResult {
    float x, y, w, h, confidence;
    const char *text;
    int text_len;
    int orientation_corrected;
    int orientation_angle;
    float orientation_confidence;
} SnipliteOcrResult;

typedef void *(*SnipliteOcrInit)(const char *det, const char *rec, int threads);
typedef void (*SnipliteOcrFree)(void *context);
typedef const SnipliteOcrResult *(*SnipliteOcrRun)(void *context, const char *bitmap_path, int *count);
typedef const SnipliteOcrResult *(*SnipliteOcrRunBgra)(void *context, const unsigned char *pixels,
    int width, int height, size_t stride_bytes, size_t data_bytes, int *count);

/* UTF-8 model paths. Returns NULL on initialization failure. */
SNIPLITE_OCR_API void *sniplite_ocr_init(const char *det, const char *rec, int threads);
SNIPLITE_OCR_API void sniplite_ocr_free(void *context);
/* Calls on one context must be serialized, including free. Input is the
   uncompressed, bottom-up 32-bit BMP written by the application. Results and
   text remain valid until the next run or free. NULL with count=0 means blank;
   count=-1 means failure. count must be non-NULL. */
SNIPLITE_OCR_API const SnipliteOcrResult *sniplite_ocr_run(void *context, const char *bitmap_path, int *count);
/* Top-down BGRA, alpha ignored. stride_bytes >= width*4; data_bytes must
   include the last visible row (its trailing padding is optional). Pixels
   are borrowed only during this call. Dimensions must be 1..32768 and total
   RGB bytes must fit INT_MAX. Result lifetime/error contract matches run. */
SNIPLITE_OCR_API const SnipliteOcrResult *sniplite_ocr_run_bgra(void *context,
    const unsigned char *pixels, int width, int height,
    size_t stride_bytes, size_t data_bytes, int *count);

#ifdef __cplusplus
}
#endif

#endif

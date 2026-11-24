#pragma once

// Internal Windows x64 ABI; keep the assembly offsets and C++ layout together.
#define SNIPLITE_CROP_SRC 0
#define SNIPLITE_CROP_DST 8
#define SNIPLITE_CROP_WIDTH 16
#define SNIPLITE_CROP_HEIGHT 20
#define SNIPLITE_CROP_CHANNELS 24
#define SNIPLITE_CROP_OUT_WIDTH 28
#define SNIPLITE_CROP_OUT_HEIGHT 32
#define SNIPLITE_CROP_PAD 36
#define SNIPLITE_CROP_X 40
#define SNIPLITE_CROP_Y 56
#define SNIPLITE_CROP_SIZE 72

#ifndef __ASSEMBLER__
#include <cstddef>
#include <cstdint>

struct sniplite_ocr_crop_params {
    const std::uint8_t *src;
    std::uint8_t *dst;
    int width;
    int height;
    int channels;
    int out_width;
    int out_height;
    int pad;
    float x[4];
    float y[4];
};

static_assert(offsetof(sniplite_ocr_crop_params, src) == SNIPLITE_CROP_SRC);
static_assert(offsetof(sniplite_ocr_crop_params, dst) == SNIPLITE_CROP_DST);
static_assert(offsetof(sniplite_ocr_crop_params, width) == SNIPLITE_CROP_WIDTH);
static_assert(offsetof(sniplite_ocr_crop_params, height) == SNIPLITE_CROP_HEIGHT);
static_assert(offsetof(sniplite_ocr_crop_params, channels) == SNIPLITE_CROP_CHANNELS);
static_assert(offsetof(sniplite_ocr_crop_params, out_width) == SNIPLITE_CROP_OUT_WIDTH);
static_assert(offsetof(sniplite_ocr_crop_params, out_height) == SNIPLITE_CROP_OUT_HEIGHT);
static_assert(offsetof(sniplite_ocr_crop_params, pad) == SNIPLITE_CROP_PAD);
static_assert(offsetof(sniplite_ocr_crop_params, x) == SNIPLITE_CROP_X);
static_assert(offsetof(sniplite_ocr_crop_params, y) == SNIPLITE_CROP_Y);
static_assert(sizeof(sniplite_ocr_crop_params) == SNIPLITE_CROP_SIZE);

// Corners are TL/TR/BR/BL. Dimensions/channels are positive, pad is nonnegative,
// and dst has out_width * out_height * channels bytes. No alignment is required.
// FMA matches the -mfma build's default contraction; SSE2 separates every add.
extern "C" void sniplite_ocr_crop_sse2(const sniplite_ocr_crop_params *params);
extern "C" void sniplite_ocr_crop_fma(const sniplite_ocr_crop_params *params);
#endif

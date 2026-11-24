#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

struct OcrResizeColumn {
    int left, right;
    float fraction, inverse;
};

struct OcrResizePlan {
    int source_width = 0;
    unsigned rounding = 0;
    std::vector<OcrResizeColumn> columns;
};

void ocr_resize_prepare(OcrResizePlan &plan, int source_width, int target_width);
void ocr_resize_rgb(std::uint8_t *dst, const std::uint8_t *src, int width, int height,
                    int target_width, int target_height, const OcrResizeColumn *columns);
void ocr_normalize_rgb(float *dst, const std::uint8_t *src, std::size_t count, int channels);

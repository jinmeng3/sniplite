#pragma once

#include <cstddef>
#include <cstdint>

struct SnipliteDepthwiseRow {
    const float *src;
    float *dst;
    const float *weights;
    std::size_t row_stride;
    int length;
    int stride;
    float bias;
    int rows;
};
static_assert(offsetof(SnipliteDepthwiseRow, row_stride) == 24 &&
              offsetof(SnipliteDepthwiseRow, length) == 32 &&
              offsetof(SnipliteDepthwiseRow, stride) == 36 &&
              offsetof(SnipliteDepthwiseRow, bias) == 40 &&
              offsetof(SnipliteDepthwiseRow, rows) == 44, "depthwise assembly ABI");

extern "C" {
void sniplite_fp16_to_fp32_f16c(const std::uint16_t *src, float *dst, std::size_t count);
void sniplite_conv2d_mk_block8(const float *weights, const float *positions, int k, float *acc);
void sniplite_conv2d_mk_block8_pair(const float *weights, const float *positions, int k, float *acc);
void sniplite_deconv2_sse2(const float *input, const float *weights, float *output, int width,
                           std::size_t plane, std::size_t weight_stride, int channels);
void sniplite_deconv2_fma(const float *input, const float *weights, float *output, int width,
                          std::size_t plane, std::size_t weight_stride, int channels);
void sniplite_matvec_sse2(const float *input, const float *weights, float *output, int n,
                         std::size_t row_stride, int rows);
void sniplite_matvec_fma(const float *input, const float *weights, float *output, int n,
                        std::size_t row_stride, int rows);
void sniplite_matvec_wide_fma(const float *input, const float *weights, float *output, int n,
                             std::size_t row_stride, int rows);
void sniplite_weighted_v_sse2(const float *scores, const float *v, float *output, int tokens,
                             std::size_t row_stride, int dimensions);
void sniplite_weighted_v_fma(const float *scores, const float *v, float *output, int tokens,
                            std::size_t row_stride, int dimensions);
void sniplite_depthwise3_sse2(const SnipliteDepthwiseRow *row);
void sniplite_depthwise5_sse2(const SnipliteDepthwiseRow *row);
void sniplite_depthwise3_fma(const SnipliteDepthwiseRow *row);
void sniplite_depthwise5_fma(const SnipliteDepthwiseRow *row);
}

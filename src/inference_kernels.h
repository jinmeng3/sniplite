#pragma once

#include <cstddef>

extern "C" {
void sniplite_conv2d_mk_block8(const float *weights, const float *positions, int k, float *acc);
void sniplite_conv2d_mk_block8_pair(const float *weights, const float *positions, int k, float *acc);
void sniplite_deconv2_sse2(const float *input, const float *weights, float *output, int width,
                           std::size_t plane, std::size_t weight_stride, int channels);
void sniplite_deconv2_fma(const float *input, const float *weights, float *output, int width,
                          std::size_t plane, std::size_t weight_stride, int channels);
}

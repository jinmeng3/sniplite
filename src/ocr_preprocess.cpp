#include "ocr_preprocess.h"

#include <algorithm>
#include <emmintrin.h>
#if defined(__FMA__)
#include <immintrin.h>
#endif

void ocr_resize_prepare(OcrResizePlan &plan, int source_width, int target_width)
{
    const unsigned rounding = _mm_getcsr() & _MM_ROUND_MASK;
    if (plan.source_width == source_width && plan.rounding == rounding &&
        plan.columns.size() == static_cast<std::size_t>(target_width)) return;
    plan.columns.resize(static_cast<std::size_t>(target_width));
    for (int x = 0; x < target_width; ++x) {
        const float fx = std::max(0.0f, (x + 0.5f) * source_width / target_width - 0.5f);
        const int left = std::min(source_width - 1, int(fx));
        auto &column = plan.columns[static_cast<std::size_t>(x)];
        column.left = left * 3;
        column.right = std::min(source_width - 1, left + 1) * 3;
        column.fraction = fx - left;
        column.inverse = 1.0f - column.fraction;
    }
    plan.source_width = source_width;
    plan.rounding = rounding;
}

static inline __m128 interpolate(__m128 left, __m128 inverse, __m128 right, __m128 fraction)
{
#if defined(__FMA__)
    return _mm_fmadd_ps(left, inverse, _mm_mul_ps(right, fraction));
#else
    return _mm_add_ps(_mm_mul_ps(left, inverse), _mm_mul_ps(right, fraction));
#endif
}

void ocr_resize_rgb(std::uint8_t *dst, const std::uint8_t *src, int width, int height,
                    int target_width, int target_height, const OcrResizeColumn *columns)
{
    const __m128 zero = _mm_setzero_ps(), maximum = _mm_set1_ps(255.0f);
    for (int y = 0; y < target_height; ++y) {
        const float fy = std::max(0.0f, (y + 0.5f) * height / target_height - 0.5f);
        const int y0 = std::min(height - 1, int(fy)), y1 = std::min(height - 1, y0 + 1);
        const float wy = fy - y0;
        const __m128 weight_y = _mm_set1_ps(wy), inverse_y = _mm_set1_ps(1.0f - wy);
        const auto *top = src + static_cast<std::size_t>(y0) * width * 3;
        const auto *bottom = src + static_cast<std::size_t>(y1) * width * 3;
        auto *out = dst + static_cast<std::size_t>(y) * target_width * 3;
        int x = 0;
        for (; x + 4 <= target_width; x += 4) {
            const auto *column = columns + x;
            const __m128 fraction = _mm_set_ps(column[3].fraction, column[2].fraction, column[1].fraction, column[0].fraction);
            const __m128 inverse = _mm_set_ps(column[3].inverse, column[2].inverse, column[1].inverse, column[0].inverse);
            for (int channel = 0; channel < 3; ++channel) {
                const __m128 tl = _mm_set_ps(top[column[3].left + channel], top[column[2].left + channel],
                                             top[column[1].left + channel], top[column[0].left + channel]);
                const __m128 tr = _mm_set_ps(top[column[3].right + channel], top[column[2].right + channel],
                                             top[column[1].right + channel], top[column[0].right + channel]);
                const __m128 bl = _mm_set_ps(bottom[column[3].left + channel], bottom[column[2].left + channel],
                                             bottom[column[1].left + channel], bottom[column[0].left + channel]);
                const __m128 br = _mm_set_ps(bottom[column[3].right + channel], bottom[column[2].right + channel],
                                             bottom[column[1].right + channel], bottom[column[0].right + channel]);
                const __m128 a = interpolate(tl, inverse, tr, fraction);
                const __m128 b = interpolate(bl, inverse, br, fraction);
                const __m128 value = _mm_min_ps(maximum, _mm_max_ps(zero, interpolate(a, inverse_y, b, weight_y)));
                __m128i bytes = _mm_cvttps_epi32(value);
                out[3 * x + channel] = static_cast<std::uint8_t>(_mm_cvtsi128_si32(bytes));
                bytes = _mm_srli_si128(bytes, 4);
                out[3 * (x + 1) + channel] = static_cast<std::uint8_t>(_mm_cvtsi128_si32(bytes));
                bytes = _mm_srli_si128(bytes, 4);
                out[3 * (x + 2) + channel] = static_cast<std::uint8_t>(_mm_cvtsi128_si32(bytes));
                bytes = _mm_srli_si128(bytes, 4);
                out[3 * (x + 3) + channel] = static_cast<std::uint8_t>(_mm_cvtsi128_si32(bytes));
            }
        }
        for (; x < target_width; ++x) {
            const auto &column = columns[x];
            for (int channel = 0; channel < 3; ++channel) {
                const float a = top[column.left + channel] * column.inverse + top[column.right + channel] * column.fraction;
                const float b = bottom[column.left + channel] * column.inverse + bottom[column.right + channel] * column.fraction;
                out[3 * x + channel] = static_cast<std::uint8_t>(std::clamp(a * (1 - wy) + b * wy, 0.0f, 255.0f));
            }
        }
    }
}

namespace {
struct NormalizedBytes {
    float values[3][256]{};
    constexpr NormalizedBytes() {
        constexpr float mean[3] = {0.485f, 0.456f, 0.406f};
        constexpr float stdev[3] = {0.229f, 0.224f, 0.225f};
        for (int channel = 0; channel < 3; ++channel)
            for (int value = 0; value < 256; ++value)
                values[channel][value] = (value / 255.0f - mean[channel]) / stdev[channel];
    }
};
constexpr NormalizedBytes normalized_bytes;
}

void ocr_normalize_rgb(float *dst, const std::uint8_t *src, std::size_t count, int channels)
{
    // The immutable byte table has the same F32 intermediate rounding as the
    // original expression. Honor callers using a different MXCSR round mode.
    if ((_mm_getcsr() & _MM_ROUND_MASK) == _MM_ROUND_NEAREST) {
        for (int channel = 0; channel < 3; ++channel) {
            const int source_channel = std::min(2 - channel, channels - 1);
            const float *table = normalized_bytes.values[channel];
            float *out = dst + static_cast<std::size_t>(channel) * count;
            for (std::size_t i = 0; i < count; ++i) out[i] = table[src[i * channels + source_channel]];
        }
        return;
    }
    static constexpr float mean[3] = {0.485f, 0.456f, 0.406f};
    static constexpr float stdev[3] = {0.229f, 0.224f, 0.225f};
    const __m128 divisor = _mm_set1_ps(255.0f);
    for (int channel = 0; channel < 3; ++channel) {
        const int source_channel = std::min(2 - channel, channels - 1);
        const __m128 average = _mm_set1_ps(mean[channel]), deviation = _mm_set1_ps(stdev[channel]);
        float *out = dst + static_cast<std::size_t>(channel) * count;
        std::size_t i = 0;
        for (; i + 4 <= count; i += 4) {
            const auto *p = src + i * channels + source_channel;
            const __m128i bytes = _mm_set_epi32(p[3 * channels], p[2 * channels], p[channels], p[0]);
            const __m128 values = _mm_cvtepi32_ps(bytes);
            _mm_storeu_ps(out + i, _mm_div_ps(_mm_sub_ps(_mm_div_ps(values, divisor), average), deviation));
        }
        for (; i < count; ++i)
            out[i] = (src[i * channels + source_channel] / 255.0f - mean[channel]) / stdev[channel];
    }
}

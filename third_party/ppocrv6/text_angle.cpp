// sniplite: retain only the original CrispEmbed 0/180-degree text-line heuristic.
// Extracted from classical_preproc.cpp (CrispStrobe, MIT; upstream provenance retained).
// Classical preprocessing algorithms derive from Leptonica (BSD-2-Clause, Dan Bloomberg).
#include "text_angle.h"
#include <cmath>
#include <utility>
#include <vector>

// =========================================================================
// 7. Text angle classification (0° vs 180°)
// =========================================================================
// Two heuristics combined:
//
// 1. Ascender/descender asymmetry: in correctly-oriented Latin text,
//    text lines have more ink mass in the upper half (ascenders: b,d,f,h,k,l,t)
//    than the lower half (descenders: g,j,p,q,y). Flip 180° and this reverses.
//
// 2. Top-heavy vs bottom-heavy: correctly-oriented text on a page typically
//    has more content near the top (headers, titles) than the bottom (margins).
//    This is weaker but helps for non-Latin scripts.

int detect_text_angle(const uint8_t * gray, int w, int h, float * confidence) {
    if (!gray || w < 20 || h < 20) {
        if (confidence) *confidence = 0;
        return 0;
    }

    // Binarize (Otsu)
    int hist[256] = {};
    for (int i = 0; i < w * h; i++) hist[gray[i]]++;
    double sum = 0;
    for (int i = 0; i < 256; i++) sum += (double)i * hist[i];
    double sumB = 0;
    int wB = 0;
    double maxv = 0;
    int best = 128;
    for (int t = 0; t < 256; t++) {
        wB += hist[t];
        if (!wB) continue;
        int wF = w * h - wB;
        if (!wF) break;
        sumB += (double)t * hist[t];
        double d = sumB / wB - (sum - sumB) / wF;
        double v = (double)wB * wF * d * d;
        if (v > maxv) {
            maxv = v;
            best = t;
        }
    }
    uint8_t thresh = (uint8_t)(best < 255 ? best + 1 : best);

    // Count dark pixels per row
    std::vector<int> row_dark(h, 0);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            if (gray[y * w + x] < thresh) row_dark[y]++;

    // Find text line regions (rows with significant dark pixel count)
    int min_dark = w / 20;                       // at least 5% of row width
    std::vector<std::pair<int, int>> text_bands; // (start_y, end_y)
    int band_start = -1;
    for (int y = 0; y < h; y++) {
        if (row_dark[y] >= min_dark) {
            if (band_start < 0) band_start = y;
        } else {
            if (band_start >= 0) {
                text_bands.push_back({ band_start, y - 1 });
                band_start = -1;
            }
        }
    }
    if (band_start >= 0) text_bands.push_back({ band_start, h - 1 });

    if (text_bands.empty()) {
        if (confidence) *confidence = 0;
        return 0;
    }

    // For each text band, compute upper/lower dark pixel ratio
    // Upper half should have more ink (ascenders) in correctly-oriented text
    double score_normal = 0, score_flipped = 0;
    for (auto & [y0, y1] : text_bands) {
        int band_h = y1 - y0 + 1;
        if (band_h < 4) continue;
        int mid = y0 + band_h / 2;
        int upper = 0, lower = 0;
        for (int y = y0; y < mid; y++) upper += row_dark[y];
        for (int y = mid; y <= y1; y++) lower += row_dark[y];
        // Correctly oriented: upper > lower (ascenders)
        score_normal += (double)upper;
        score_flipped += (double)lower;
    }

    // Also consider page-level asymmetry: more content near top = normal
    int top_third = 0, bot_third = 0;
    int third = h / 3;
    for (int y = 0; y < third; y++) top_third += row_dark[y];
    for (int y = h - third; y < h; y++) bot_third += row_dark[y];
    // Weight page-level asymmetry less than per-line asymmetry
    score_normal += top_third * 0.3;
    score_flipped += bot_third * 0.3;

    double total = score_normal + score_flipped;
    if (total < 1) {
        if (confidence) *confidence = 0;
        return 0;
    }

    float conf = (float)std::abs(score_normal - score_flipped) / (float)total;
    if (confidence) *confidence = conf;

    return score_normal >= score_flipped ? 0 : 180;
}

#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace ppocrv6_det {
namespace policy {

enum class shape_action { build, reuse, fallback, rebuild };

// Retain the most recent failures without accumulating per-image metadata.
struct failed_shapes {
    static constexpr size_t capacity = 16;
    struct shape { int h = 0, w = 0; };
    std::array<shape, capacity> entries{};
    size_t next = 0;

    bool contains(int h, int w) const {
        for (const auto & entry : entries)
            if (entry.h == h && entry.w == w) return true;
        return false;
    }
    void remember(int h, int w) {
        if (contains(h, w)) return;
        entries[next] = { h, w };
        next = (next + 1) % capacity;
    }
};

inline shape_action graph_shape(bool attempted, bool ready, int old_h, int old_w, int h, int w, bool rebuild) {
    if (!attempted) return shape_action::build;
    if (old_h == h && old_w == w) return ready ? shape_action::reuse : shape_action::fallback;
    return rebuild ? shape_action::rebuild : shape_action::fallback;
}

// Reject incomplete or invalid output before postprocessing can read it.
inline bool valid_probability(const std::vector<float> & probability, int h, int w) {
    if (h <= 0 || w <= 0 || probability.size() / size_t(h) != size_t(w) ||
        probability.size() % size_t(h) != 0) return false;
    for (float p : probability)
        if (!std::isfinite(p) || p < 0.0f || p > 1.0f) return false;
    return true;
}

inline bool accept_graph(bool compare, bool accept_empty, size_t boxes, const std::vector<float> & probability, int h,
                         int w) {
    return !compare && (boxes || accept_empty) && valid_probability(probability, h, w);
}

} // namespace policy
} // namespace ppocrv6_det

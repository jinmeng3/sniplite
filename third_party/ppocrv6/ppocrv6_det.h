#pragma once

#include <cstdint>
#include <vector>
#include <string>

namespace ppocrv6_det {

struct box {
    float x, y, w, h, score;
    float qx[4] = {}, qy[4] = {};
};
struct context;

context * init(const char * path, int n_threads, bool prefer_vulkan);
void free(context * ctx);
std::vector<box> detect_raw(context * ctx, const uint8_t * pixels, int width, int height, int channels,
                            float threshold = 0.3f);
const float * last_probability(const context * ctx, int * height, int * width);
// Intermediate copies are opt-in: set CRISPEMBED_PPOCRV6_DET_CAPTURE_STAGES=1.
// DET_GRAPH_COMPARE also enables capture and runs the CPU reference. Graph
// stages use graph_stageN / graph_<name>; reference stages retain their names.
// Snapshots are cleared by the next detect_raw call; absent stages return null.
const float * last_stage(const context * ctx, const char * name, size_t * n);

} // namespace ppocrv6_det

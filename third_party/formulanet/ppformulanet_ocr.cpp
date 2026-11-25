// ppformulanet_ocr.cpp — PPFormulaNet-S (HGNetv2 + MBart) via ggml.
//
// - CPU builds use CPU compute for every encoder and decoder stage
// - Vulkan builds try Vulkan graphs for the encoder, projection, cross K/V,
//   autoregressive decoder layers, logits, and greedy argmax, then retry on CPU
// - Confidence calculation and beam-search ranking remain CPU-side
// Source archive SHA-256: 4dd5a0eebca5f3147741dd8170c8da61986f464da27a0d268220f7451685686e
// sniplite changes: immutable decoder weight cache; matching Windows GGML SDK
// headers and import definitions are in ggml/.

#include "ppformulanet_ocr.h"
#include "ggml.h"
#include "ggml-alloc.h"
#include "ggml-backend.h"
#include "ggml-cpu.h"
#include "core/bpe.h"
#include "core/gguf_loader.h"
#include "core/cpu_ops.h"
#include "core/env_gate.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <unordered_map>
#include <memory>
#include <string>
#include <mutex>
#include <vector>

using core_cpu::conv2d_cpu;
using core_cpu::layernorm_cpu;
using core_cpu::linear_cpu;
using core_cpu::mha_1q_cpu;
using core_cpu::relu_inplace;
using core_cpu::to_f32;

// ---------------------------------------------------------------------------
// Structs
// ---------------------------------------------------------------------------

// Conv layer: weight (2D flattened) + bias (from folded BN)
struct conv_layer {
    ggml_tensor * w = nullptr; // [out_ch, in_ch*kh*kw] flattened
    ggml_tensor * b = nullptr; // [out_ch]
};

// HG_Block conv layers + aggregation
struct hg_block {
    // For regular blocks: layers[i] has one conv
    // For light blocks: layers[i] has conv1 (pointwise) + conv2 (depthwise)
    struct layer_t {
        conv_layer conv;  // regular ConvBNAct
        conv_layer conv1; // light: pointwise 1×1
        conv_layer conv2; // light: depthwise
        bool is_light = false;
    };
    std::vector<layer_t> layers;
    conv_layer agg_squeeze; // 1×1 squeeze
    conv_layer agg_excite;  // 1×1 excitation
};

// Stage: optional downsample + blocks
struct hg_stage {
    conv_layer downsample; // depthwise conv (may be null)
    bool has_downsample = false;
    std::vector<hg_block> blocks;
    // Config
    int in_ch, mid_ch, out_ch, n_blocks, n_layers, kernel_size;
    bool light_block;
};

// Decoder layer (MBart)
struct ppfn_dec_layer {
    ggml_tensor *self_ln_w, *self_ln_b;
    ggml_tensor *self_q_w, *self_q_b, *self_k_w, *self_k_b, *self_v_w, *self_v_b;
    ggml_tensor *self_out_w, *self_out_b;
    ggml_tensor *cross_ln_w, *cross_ln_b;
    ggml_tensor *cross_q_w, *cross_q_b, *cross_k_w, *cross_k_b, *cross_v_w, *cross_v_b;
    ggml_tensor *cross_out_w, *cross_out_b;
    ggml_tensor *ff_ln_w, *ff_ln_b, *ff_up_w, *ff_up_b, *ff_down_w, *ff_down_b;
};

struct ppformulanet_ocr_context {
    ppformulanet_ocr_hparams hparams;

    // Encoder: Stem
    conv_layer stem[5]; // stem1..stem4 (index 0..4, skip index 0 for stem1 at [0])

    // Encoder: Stages
    hg_stage stages[4];

    // Projection
    ggml_tensor *proj_w, *proj_b;

    // Decoder
    ggml_tensor *tok_embed, *pos_embed_dec;
    ggml_tensor *embed_ln_w, *embed_ln_b;
    ggml_tensor *final_ln_w, *final_ln_b;
    ggml_tensor *lm_head_w, *lm_head_b;
    std::vector<ppfn_dec_layer> dec_layers;

    // Infrastructure
    std::vector<std::string> vocab;
    core_gguf::WeightLoad wl;
    ggml_backend_t backend = nullptr;
    int n_threads;

    // ggml graph encoder. Vulkan builds attempt a Vulkan scheduler first.
    ggml_backend_t enc_backend = nullptr;
    ggml_backend_sched_t enc_sched = nullptr;
    ggml_backend_t enc_cpu_backend = nullptr;
    bool enc_sched_is_vulkan = false;
    bool vulkan_failed = false;
    bool decoder_step_vulkan_used = false;
    int decoder_step_argmax = 0;
    std::vector<uint8_t> dec_compute_meta;
    std::vector<uint8_t> enc_compute_meta;
    std::unordered_map<const ggml_tensor *, ggml_tensor *> vulkan_resident;
    std::vector<ggml_context *> vulkan_resident_ctxs;
    std::vector<ggml_backend_buffer_t> vulkan_resident_bufs;

    std::string result_buf;
    std::vector<float> char_confidences; // per-token softmax probabilities

    // Cached encoder output
    std::vector<float> enc_out; // [n_enc_tokens * enc_hidden]
    int n_enc_tokens = 0;
    std::vector<float> proj_out; // [n_enc_tokens * dec_d_model]

    // Cross-attention K/V cache (precomputed from projected encoder output)
    std::vector<std::vector<float>> cross_k_cache;
    std::vector<std::vector<float>> cross_v_cache;

    // Immutable decoder weights are dequantized once per model, not per token.
    core_cpu::DequantCache weights;

    bool bench;
};

static void linear_cached(ppformulanet_ocr_context * ctx, const float * in, float * out,
                          int in_dim, int out_dim, const ggml_tensor * w, const ggml_tensor * b) {
    linear_cpu(in, out, in_dim, out_dim, ctx->weights.get(w), ctx->weights.get(b));
}

static void layernorm_cached(ppformulanet_ocr_context * ctx, const float * in, float * out,
                             int dim, const ggml_tensor * w, const ggml_tensor * b, float eps) {
    layernorm_cpu(in, out, dim, ctx->weights.get(w), ctx->weights.get(b), eps);
}

// ---------------------------------------------------------------------------
// Tensor lookup
// ---------------------------------------------------------------------------

static ggml_tensor * F(const std::unordered_map<std::string, ggml_tensor *> & m, const char * n) {
    auto it = m.find(n);
    if (it != m.end()) return it->second;
    std::string alt(n);
    for (auto & c : alt)
        if (c == '.') c = '_';
    it = m.find(alt);
    return it != m.end() ? it->second : nullptr;
}

static conv_layer map_conv(const std::unordered_map<std::string, ggml_tensor *> & m, const char * prefix) {
    char buf[256];
    conv_layer c;
    snprintf(buf, sizeof(buf), "%s.weight", prefix);
    c.w = F(m, buf);
    snprintf(buf, sizeof(buf), "%s.bias", prefix);
    c.b = F(m, buf);
    return c;
}

static void map_tensors(ppformulanet_ocr_context * ctx) {
    const auto & m = ctx->wl.tensors;
    const auto & hp = ctx->hparams;
    char buf[256];

    // Stem
    const char * stem_names[] = { "enc.stem.stem1", "enc.stem.stem2a", "enc.stem.stem2b", "enc.stem.stem3",
                                  "enc.stem.stem4" };
    for (int i = 0; i < 5; i++) ctx->stem[i] = map_conv(m, stem_names[i]);

    // Stages
    for (int si = 0; si < 4; si++) {
        auto & st = ctx->stages[si];

        // Downsample
        snprintf(buf, sizeof(buf), "enc.stage%d.downsample", si);
        st.downsample = map_conv(m, buf);
        st.has_downsample = (st.downsample.w != nullptr);

        // Blocks
        st.blocks.resize(st.n_blocks);
        for (int bi = 0; bi < st.n_blocks; bi++) {
            auto & blk = st.blocks[bi];
            blk.layers.resize(st.n_layers);

            for (int li = 0; li < st.n_layers; li++) {
                auto & lay = blk.layers[li];
                if (st.light_block) {
                    lay.is_light = true;
                    snprintf(buf, sizeof(buf), "enc.stage%d.block%d.layer%d.conv1", si, bi, li);
                    lay.conv1 = map_conv(m, buf);
                    snprintf(buf, sizeof(buf), "enc.stage%d.block%d.layer%d.conv2", si, bi, li);
                    lay.conv2 = map_conv(m, buf);
                } else {
                    lay.is_light = false;
                    snprintf(buf, sizeof(buf), "enc.stage%d.block%d.layer%d", si, bi, li);
                    lay.conv = map_conv(m, buf);
                }
            }

            snprintf(buf, sizeof(buf), "enc.stage%d.block%d.agg_squeeze", si, bi);
            blk.agg_squeeze = map_conv(m, buf);
            snprintf(buf, sizeof(buf), "enc.stage%d.block%d.agg_excite", si, bi);
            blk.agg_excite = map_conv(m, buf);
        }
    }

    // Projection
    ctx->proj_w = F(m, "enc.proj.weight");
    ctx->proj_b = F(m, "enc.proj.bias");

    // Decoder
    ctx->tok_embed = F(m, "dec.embed_tokens.weight");
    ctx->pos_embed_dec = F(m, "dec.embed_positions.weight");
    ctx->embed_ln_w = F(m, "dec.embed_ln.weight");
    ctx->embed_ln_b = F(m, "dec.embed_ln.bias");
    ctx->final_ln_w = F(m, "dec.final_ln.weight");
    ctx->final_ln_b = F(m, "dec.final_ln.bias");
    ctx->lm_head_w = F(m, "dec.lm_head.weight");
    ctx->lm_head_b = F(m, "dec.lm_head.bias");

    ctx->dec_layers.resize(hp.dec_layers);
    for (int i = 0; i < hp.dec_layers; i++) {
        auto & l = ctx->dec_layers[i];
        auto DL = [&](const char * s) {
            snprintf(buf, sizeof(buf), "dec.layers.%d.%s", i, s);
            return F(m, buf);
        };
        l.self_ln_w = DL("self_attn_ln.weight");
        l.self_ln_b = DL("self_attn_ln.bias");
        l.self_q_w = DL("self_attn.q.weight");
        l.self_q_b = DL("self_attn.q.bias");
        l.self_k_w = DL("self_attn.k.weight");
        l.self_k_b = DL("self_attn.k.bias");
        l.self_v_w = DL("self_attn.v.weight");
        l.self_v_b = DL("self_attn.v.bias");
        l.self_out_w = DL("self_attn.out.weight");
        l.self_out_b = DL("self_attn.out.bias");
        l.cross_ln_w = DL("cross_attn_ln.weight");
        l.cross_ln_b = DL("cross_attn_ln.bias");
        l.cross_q_w = DL("cross_attn.q.weight");
        l.cross_q_b = DL("cross_attn.q.bias");
        l.cross_k_w = DL("cross_attn.k.weight");
        l.cross_k_b = DL("cross_attn.k.bias");
        l.cross_v_w = DL("cross_attn.v.weight");
        l.cross_v_b = DL("cross_attn.v.bias");
        l.cross_out_w = DL("cross_attn.out.weight");
        l.cross_out_b = DL("cross_attn.out.bias");
        l.ff_ln_w = DL("ffn_ln.weight");
        l.ff_ln_b = DL("ffn_ln.bias");
        l.ff_up_w = DL("ffn.up.weight");
        l.ff_up_b = DL("ffn.up.bias");
        l.ff_down_w = DL("ffn.down.weight");
        l.ff_down_b = DL("ffn.down.bias");
    }
}

// ---------------------------------------------------------------------------
// CNN Encoder: CPU-side forward pass
// ---------------------------------------------------------------------------
// All conv weights are pre-folded (BN absorbed). Weights stored as 2D:
//   [out_ch, in_ch * kh * kw] for regular convolutions
//   [out_ch, 1 * kh * kw]     for depthwise (groups=in_ch)
// We perform convolution manually on CPU. For a 57M model at 384×384 this
// is ~0.5-1 second on a modern CPU. Good enough for initial port; can move
// to ggml graph later for SIMD acceleration.

// MaxPool2d(kernel=2, stride=1, ceil_mode=True) on padded input
static void maxpool2d_k2s1_ceil(const float * in, float * out, int ch, int H, int W) {
    // With ceil_mode=True, output size = ceil((H - 2)/1) + 1 = H - 1
    // But since input is already padded (0,1,0,1), the effective H/W is H+1
    // Actually: MaxPool on the already-padded tensor.
    // Output: ceil((H - 2) / 1) + 1 = H - 1
    int out_H = H - 1;
    int out_W = W - 1;
    // With ceil_mode, output = ceil((H + 2*pad - kh) / stride) + 1
    // pad=0, kh=2, stride=1, ceil_mode: out = ceil((H-2)/1) + 1 = H-1
    // But Texo uses ceil_mode=True which can add an extra row:
    // PyTorch: out = floor((H + 2*pad - kh) / stride) + 1, but with ceil_mode
    // out = ceil((H + 2*pad - kh) / stride) + 1
    // For H=193, kh=2, stride=1, pad=0: out = ceil(191/1) + 1 = 192
    out_H = (H - 2 + 1 - 1) / 1 + 1; // ceil div
    out_W = (W - 2 + 1 - 1) / 1 + 1;

    for (int c = 0; c < ch; c++) {
        for (int oy = 0; oy < out_H; oy++) {
            for (int ox = 0; ox < out_W; ox++) {
                float maxv = -1e30f;
                for (int ky = 0; ky < 2; ky++) {
                    for (int kx = 0; kx < 2; kx++) {
                        int iy = oy + ky;
                        int ix = ox + kx;
                        if (iy < H && ix < W) {
                            float v = in[c * H * W + iy * W + ix];
                            if (v > maxv) maxv = v;
                        }
                    }
                }
                out[c * out_H * out_W + oy * out_W + ox] = maxv;
            }
        }
    }
}

// Pad tensor: add right_pad columns and bottom_pad rows (zero-pad)
static std::vector<float> pad_tensor(const float * in, int ch, int H, int W, int top, int bottom, int left, int right) {
    int new_H = H + top + bottom;
    int new_W = W + left + right;
    std::vector<float> out(ch * new_H * new_W, 0.0f);
    for (int c = 0; c < ch; c++)
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++)
                out[c * new_H * new_W + (y + top) * new_W + (x + left)] = in[c * H * W + y * W + x];
    return out;
}

// Apply ConvBNAct: conv (folded weights) + ReLU
// Returns output dimensions via out_H, out_W
static std::vector<float> apply_conv(const conv_layer & cl, const float * in, int in_ch, int H, int W, int out_ch,
                                     int kh, int kw, int stride, int groups, bool use_relu, int & out_H, int & out_W) {
    auto wv = to_f32(cl.w);
    auto bv = to_f32(cl.b);
    int pad = (kh - 1) / 2;
    out_H = (H + 2 * pad - kh) / stride + 1;
    out_W = (W + 2 * pad - kw) / stride + 1;
    std::vector<float> out(out_ch * out_H * out_W);
    conv2d_cpu(in, out.data(), wv.data(), bv.data(), in_ch, out_ch, H, W, kh, kw, stride, pad, groups);
    if (use_relu) relu_inplace(out.data(), (int)out.size());
    return out;
}

// ---------------------------------------------------------------------------
// ggml graph: HGNetv2 encoder
// ---------------------------------------------------------------------------

// Keep Vulkan encoder weights on the device in the format used by its graph.
static ggml_tensor * ppfn_graph_resident(ppformulanet_ocr_context * ctx, const ggml_tensor * src, ggml_type type,
                                         int64_t ne0, int64_t ne1, int64_t ne2, int64_t ne3) {
    if (!src || !ctx->enc_backend) return nullptr;
    auto it = ctx->vulkan_resident.find(src);
    if (it != ctx->vulkan_resident.end()) return it->second;

    ggml_init_params ip = { ggml_tensor_overhead() + 64, nullptr, true };
    ggml_context * wc = ggml_init(ip);
    if (!wc) return nullptr;
    ggml_tensor * dst = ggml_new_tensor_4d(wc, type, ne0, ne1, ne2, ne3);
    ggml_backend_buffer_t buf = dst ? ggml_backend_alloc_ctx_tensors(wc, ctx->enc_backend) : nullptr;
    if (!dst || !buf) {
        if (buf) ggml_backend_buffer_free(buf);
        ggml_free(wc);
        return nullptr;
    }

    std::vector<float> data = to_f32(src);
    if (data.size() != (size_t)ggml_nelements(dst)) {
        ggml_backend_buffer_free(buf);
        ggml_free(wc);
        return nullptr;
    }
    if (type == GGML_TYPE_F16) {
        std::vector<ggml_fp16_t> half(data.size());
        ggml_fp32_to_fp16_row(data.data(), half.data(), (int64_t)half.size());
        ggml_backend_tensor_set(dst, half.data(), 0, ggml_nbytes(dst));
    } else if (type == GGML_TYPE_F32) {
        ggml_backend_tensor_set(dst, data.data(), 0, ggml_nbytes(dst));
    } else {
        ggml_backend_buffer_free(buf);
        ggml_free(wc);
        return nullptr;
    }
    ctx->vulkan_resident[src] = dst;
    ctx->vulkan_resident_ctxs.push_back(wc);
    ctx->vulkan_resident_bufs.push_back(buf);
    return dst;
}

static ggml_tensor * ppfn_prep_bias(ppformulanet_ocr_context * ctx, const ggml_tensor * b) {
    if (!b) return nullptr;
    if (ctx->enc_sched_is_vulkan) {
        ggml_tensor * resident = ppfn_graph_resident(ctx, b, GGML_TYPE_F32, b->ne[0], 1, 1, 1);
        if (resident) return resident;
    }
    return (ggml_tensor *)b;
}

// Prep conv weight in the graph format. Vulkan weights are converted and uploaded once.
static ggml_tensor * ppfn_prep_conv(ppformulanet_ocr_context * ctx, ggml_context * g, ggml_tensor * w, int IC,
                                    int KH, int KW) {
    if (!w) return nullptr;
    if (ctx->enc_sched_is_vulkan) {
        const int64_t OC = ggml_n_dims(w) == 2 ? w->ne[1] : w->ne[3];
        ggml_tensor * resident = ppfn_graph_resident(ctx, w, GGML_TYPE_F16, KW, KH, IC, OC);
        if (resident) return resident;
    }
    if (ggml_n_dims(w) == 2) {
        if (w->type != GGML_TYPE_F32 && w->type != GGML_TYPE_F16) w = ggml_cont(g, ggml_cast(g, w, GGML_TYPE_F32));
        w = ggml_reshape_4d(g, w, KW, KH, IC, w->ne[1]);
    }
    if (w->type != GGML_TYPE_F16) w = ggml_cast(g, w, GGML_TYPE_F16);
    return w;
}

// Prep depthwise conv weight: reshape 2D→4D with IC=1.
static ggml_tensor * ppfn_prep_dw(ppformulanet_ocr_context * ctx, ggml_context * g, ggml_tensor * w, int KH, int KW) {
    if (!w) return nullptr;
    if (ctx->enc_sched_is_vulkan) {
        const int64_t OC = ggml_n_dims(w) == 2 ? w->ne[1] : w->ne[3];
        ggml_tensor * resident = ppfn_graph_resident(ctx, w, GGML_TYPE_F16, KW, KH, 1, OC);
        if (resident) return resident;
    }
    if (ggml_n_dims(w) == 2) {
        if (w->type != GGML_TYPE_F32 && w->type != GGML_TYPE_F16) w = ggml_cont(g, ggml_cast(g, w, GGML_TYPE_F32));
        int64_t OC = w->ne[1];
        w = ggml_reshape_4d(g, w, KW, KH, 1, OC);
    }
    if (w->type != GGML_TYPE_F16) w = ggml_cast(g, w, GGML_TYPE_F16);
    return w;
}

// Conv2D + bias + optional ReLU
static ggml_tensor * ppfn_conv(ppformulanet_ocr_context * ctx, ggml_context * g, ggml_tensor * x, const conv_layer & cl,
                               int IC, int KH, int KW, int stride, bool relu) {
    ggml_tensor * w = ppfn_prep_conv(ctx, g, cl.w, IC, KH, KW);
    if (!w) return nullptr;
    x = ggml_conv_2d(g, w, x, stride, stride, (KH - 1) / 2, (KW - 1) / 2, 1, 1);
    if (cl.b) {
        ggml_tensor * bias = ppfn_prep_bias(ctx, cl.b);
        if (!bias) return nullptr;
        x = ggml_add(g, x, ggml_reshape_3d(g, bias, 1, 1, cl.b->ne[0]));
    }
    if (relu) x = ggml_relu(g, x);
    return x;
}

// Conv2D with explicit padding (for stem k=2 convs that need asymmetric pad)
static ggml_tensor * ppfn_conv_nopad(ppformulanet_ocr_context * ctx, ggml_context * g, ggml_tensor * x,
                                     const conv_layer & cl, int IC, int KH, int KW, int stride, bool relu) {
    ggml_tensor * w = ppfn_prep_conv(ctx, g, cl.w, IC, KH, KW);
    if (!w) return nullptr;
    x = ggml_conv_2d(g, w, x, stride, stride, 0, 0, 1, 1); // no auto-pad
    if (cl.b) {
        ggml_tensor * bias = ppfn_prep_bias(ctx, cl.b);
        if (!bias) return nullptr;
        x = ggml_add(g, x, ggml_reshape_3d(g, bias, 1, 1, cl.b->ne[0]));
    }
    if (relu) x = ggml_relu(g, x);
    return x;
}

// Depthwise Conv2D + bias (no ReLU by default for downsample)
static ggml_tensor * ppfn_dw_conv(ppformulanet_ocr_context * ctx, ggml_context * g, ggml_tensor * x,
                                  const conv_layer & cl, int CH, int KH, int KW, int stride, bool relu) {
    ggml_tensor * w = ppfn_prep_dw(ctx, g, cl.w, KH, KW);
    if (!w) return nullptr;
    x = ggml_conv_2d_dw(g, w, x, stride, stride, (KH - 1) / 2, (KW - 1) / 2, 1, 1);
    if (cl.b) {
        ggml_tensor * bias = ppfn_prep_bias(ctx, cl.b);
        if (!bias) return nullptr;
        x = ggml_add(g, x, ggml_reshape_3d(g, bias, 1, 1, cl.b->ne[0]));
    }
    if (relu) x = ggml_relu(g, x);
    return x;
}

static ggml_cgraph * build_ppfn_encoder_graph(ppformulanet_ocr_context * ctx, int H, int W) {
    // Estimate: 4 stages, up to 3 blocks × 6 layers × ~10 nodes + stem ~50
    // Plus type casts for quantized weights (~5 nodes per conv)
    int graph_size = 4000;
    size_t buf_size = ggml_tensor_overhead() * (graph_size + 500) + ggml_graph_overhead_custom(graph_size, false);
    ctx->enc_compute_meta.resize(buf_size);
    ggml_init_params ip = { buf_size, ctx->enc_compute_meta.data(), true };
    ggml_context * g = ggml_init(ip);
    ggml_cgraph * gf = ggml_new_graph_custom(g, graph_size, false);

    // Input: [W, H, 3] (ggml WHC = CHW in memory)
    ggml_tensor * x = ggml_new_tensor_3d(g, GGML_TYPE_F32, W, H, 3);
    ggml_set_name(x, "pixel_input");
    ggml_set_input(x);

    // ---- Stem ----
    // stem1: Conv(3→32, k=3, s=2, p=1) + ReLU
    x = ppfn_conv(ctx, g, x, ctx->stem[0], 3, 3, 3, 2, true);
    int cur_ch = 32;

    // F.pad(x, (0,1,0,1)) — pad right + bottom by 1 pixel
    // ggml_pad_ext: (lp0, rp0, lp1, rp1, ...) where dim0=W, dim1=H
    x = ggml_pad_ext(g, x, 0, 1, 0, 1, 0, 0, 0, 0);

    // stem2a: Conv(32→16, k=2, s=1, p=0) + ReLU
    ggml_tensor * x2 = ppfn_conv_nopad(ctx, g, x, ctx->stem[1], 32, 2, 2, 1, true);

    // F.pad(x2, (0,1,0,1))
    x2 = ggml_pad_ext(g, x2, 0, 1, 0, 1, 0, 0, 0, 0);

    // stem2b: Conv(16→32, k=2, s=1, p=0) + ReLU
    x2 = ppfn_conv_nopad(ctx, g, x2, ctx->stem[2], 16, 2, 2, 1, true);

    // MaxPool2d(k=2, s=1) on the padded stem1 output (x still has padding)
    // Input is already padded by (0,1,0,1), so maxpool k=2 s=1 gives same spatial as stem2b
    ggml_tensor * x1 = ggml_pool_2d(g, x, GGML_OP_POOL_MAX, 2, 2, 1, 1, 0, 0);

    // Concat [x1, x2] → 64 channels
    x = ggml_concat(g, x1, x2, 2);
    cur_ch = 64;

    // stem3: Conv(64→32, k=3, s=2) + ReLU
    x = ppfn_conv(ctx, g, x, ctx->stem[3], 64, 3, 3, 2, true);
    cur_ch = 32;

    // stem4: Conv(32→48, k=1, s=1) + ReLU
    x = ppfn_conv(ctx, g, x, ctx->stem[4], 32, 1, 1, 1, true);
    cur_ch = 48;

    // ---- 4 Stages ----
    for (int si = 0; si < 4; si++) {
        auto & st = ctx->stages[si];

        // Depthwise downsample (3×3, s=2, no ReLU)
        if (st.has_downsample) {
            x = ppfn_dw_conv(ctx, g, x, st.downsample, cur_ch, 3, 3, 2, false);
        }

        // HG_Blocks
        for (int bi = 0; bi < st.n_blocks; bi++) {
            auto & blk = st.blocks[bi];
            bool residual = (bi > 0);
            ggml_tensor * identity = residual ? x : nullptr;

            // Collect layer outputs for concat (input + N layers)
            ggml_tensor * block_input = x;
            int layer_in_ch = cur_ch;
            int layer_out_ch = st.mid_ch;

            for (int li = 0; li < st.n_layers; li++) {
                auto & lay = blk.layers[li];
                int in_c = (li == 0) ? layer_in_ch : layer_out_ch;

                if (lay.is_light) {
                    // LightConvBNAct: conv1 (1×1, no ReLU) + conv2 (depthwise + ReLU)
                    x = ppfn_conv(ctx, g, x, lay.conv1, in_c, 1, 1, 1, false);
                    x = ppfn_dw_conv(ctx, g, x, lay.conv2, layer_out_ch, st.kernel_size, st.kernel_size, 1, true);
                } else {
                    // ConvBNAct: single conv + ReLU
                    x = ppfn_conv(ctx, g, x, lay.conv, in_c, st.kernel_size, st.kernel_size, 1, true);
                }

                // Concat this layer's output with running concat
                // First iteration: concat = [block_input, layer0_out]
                // Subsequent: concat = [prev_concat, layerN_out]
                if (li == 0) {
                    block_input = ggml_concat(g, block_input, x, 2);
                } else {
                    block_input = ggml_concat(g, block_input, x, 2);
                }
            }

            // Aggregation: squeeze (1×1 + ReLU) → excite (1×1 + ReLU)
            int total_ch = cur_ch + st.n_layers * layer_out_ch;
            int squeeze_ch = st.out_ch / 2;
            x = ppfn_conv(ctx, g, block_input, blk.agg_squeeze, total_ch, 1, 1, 1, true);
            x = ppfn_conv(ctx, g, x, blk.agg_excite, squeeze_ch, 1, 1, 1, true);
            cur_ch = st.out_ch;

            // Residual
            if (residual && identity) {
                x = ggml_add(g, x, identity);
            }
        }
    }

    ggml_set_name(x, "encoder_out");
    ggml_set_output(x);
    ggml_build_forward_expand(gf, x);
    return gf;
}

static bool run_encoder_ggml(ppformulanet_ocr_context * ctx, const float * rgb, int H, int W) {
    auto t0 = std::chrono::steady_clock::now();

    ggml_cgraph * gf = build_ppfn_encoder_graph(ctx, H, W);
    ggml_backend_sched_reset(ctx->enc_sched);
    if (!ggml_backend_sched_alloc_graph(ctx->enc_sched, gf)) {
        fprintf(stderr, "ppfn: failed to allocate encoder graph\n");
        return false;
    }

    // Set input (3-channel RGB, CHW = ggml WHC memory order)
    ggml_tensor * pixel_in = ggml_graph_get_tensor(gf, "pixel_input");
    ggml_backend_tensor_set(pixel_in, rgb, 0, 3 * H * W * sizeof(float));

    // Set threads and compute
    for (int i = 0; i < ggml_backend_sched_get_n_backends(ctx->enc_sched); i++) {
        ggml_backend_t be = ggml_backend_sched_get_backend(ctx->enc_sched, i);
        ggml_backend_dev_t dev = ggml_backend_get_device(be);
        ggml_backend_reg_t reg = dev ? ggml_backend_dev_backend_reg(dev) : nullptr;
        if (reg) {
            auto * fn =
                (ggml_backend_set_n_threads_t)ggml_backend_reg_get_proc_address(reg, "ggml_backend_set_n_threads");
            if (fn) fn(be, ctx->n_threads);
        }
    }
    if (ggml_backend_sched_graph_compute(ctx->enc_sched, gf) != GGML_STATUS_SUCCESS) {
        fprintf(stderr, "ppfn: encoder graph compute failed\n");
        return false;
    }

    // Read output: (W', H', C) → transpose to (H'*W', C)
    ggml_tensor * enc_out = ggml_graph_get_tensor(gf, "encoder_out");
    int out_w = (int)enc_out->ne[0], out_h = (int)enc_out->ne[1];
    int out_c = (int)enc_out->ne[2];
    int N = out_h * out_w;

    std::vector<float> chw(out_c * N);
    ggml_backend_tensor_get(enc_out, chw.data(), 0, chw.size() * sizeof(float));

    ctx->n_enc_tokens = N;
    ctx->enc_out.resize(N * out_c);
    for (int n = 0; n < N; n++) {
        int y = n / out_w, xi = n % out_w;
        for (int c = 0; c < out_c; c++) ctx->enc_out[n * out_c + c] = chw[c * out_h * out_w + y * out_w + xi];
    }

    if (ctx->bench) {
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        fprintf(stderr, "[ppfn-bench] encoder (ggml): %.1f ms\n", ms);
    }
    fprintf(stderr, "ppfn: encoder output (%d, %d)\n", N, out_c);
    return true;
}

// Run the HGNetv2 encoder on CHW float input (scalar fallback)
static void run_encoder(ppformulanet_ocr_context * ctx, const float * rgb, int H, int W) {
    // rgb is [3, H, W] in CHW format, normalized

    fprintf(stderr, "ppfn: encoder start (%dx%d)\n", W, H);

    // ---- StemBlock ----
    // stem1: Conv(3→32, k=3, s=2) + ReLU
    int oH, oW;
    auto x = apply_conv(ctx->stem[0], rgb, 3, H, W, 32, 3, 3, 2, 1, true, oH, oW);
    int cur_ch = 32, cur_H = oH, cur_W = oW;
    fprintf(stderr, "ppfn: stem1 -> (%d, %d, %d)\n", cur_ch, cur_H, cur_W);

    // F.pad(x, (0,1,0,1)) before stem2a
    auto padded = pad_tensor(x.data(), cur_ch, cur_H, cur_W, 0, 1, 0, 1);
    int pH = cur_H + 1, pW = cur_W + 1;

    // stem2a: Conv(32→16, k=2, s=1) + ReLU (padding=0 since k=2, pad=(2-1)/2=0)
    auto x2 = apply_conv(ctx->stem[1], padded.data(), 32, pH, pW, 16, 2, 2, 1, 1, true, oH, oW);
    fprintf(stderr, "ppfn: stem2a -> (%d, %d, %d)\n", 16, oH, oW);

    // F.pad(x2, (0,1,0,1)) before stem2b
    auto x2_padded = pad_tensor(x2.data(), 16, oH, oW, 0, 1, 0, 1);
    int x2pH = oH + 1, x2pW = oW + 1;

    // stem2b: Conv(16→32, k=2, s=1) + ReLU
    x2 = apply_conv(ctx->stem[2], x2_padded.data(), 16, x2pH, x2pW, 32, 2, 2, 1, 1, true, oH, oW);
    fprintf(stderr, "ppfn: stem2b -> (%d, %d, %d)\n", 32, oH, oW);

    // MaxPool2d(k=2, s=1, ceil_mode=True) on the padded stem1 output
    int pool_H = pH - 1, pool_W = pW - 1;
    std::vector<float> x1(32 * pool_H * pool_W);
    maxpool2d_k2s1_ceil(padded.data(), x1.data(), 32, pH, pW);
    // After pool with ceil_mode on [32, pH, pW]: output is [32, pH-1, pW-1]
    // which should equal stem2b output size
    fprintf(stderr, "ppfn: pool -> (%d, %d, %d)\n", 32, pool_H, pool_W);

    // Verify sizes match
    if (pool_H != oH || pool_W != oW) {
        fprintf(stderr, "ppfn: WARNING: pool (%d,%d) != stem2b (%d,%d), adjusting\n", pool_H, pool_W, oH, oW);
    }

    // Concatenate [pool, stem2b] on channel dim → [64, H, W]
    int cat_ch = 64;
    int cat_H = std::min(pool_H, oH), cat_W = std::min(pool_W, oW);
    std::vector<float> cat(cat_ch * cat_H * cat_W);
    for (int c = 0; c < 32; c++)
        for (int y = 0; y < cat_H; y++)
            for (int xi = 0; xi < cat_W; xi++)
                cat[c * cat_H * cat_W + y * cat_W + xi] = x1[c * pool_H * pool_W + y * pool_W + xi];
    for (int c = 0; c < 32; c++)
        for (int y = 0; y < cat_H; y++)
            for (int xi = 0; xi < cat_W; xi++)
                cat[(c + 32) * cat_H * cat_W + y * cat_W + xi] = x2[c * oH * oW + y * oW + xi];

    cur_ch = 64;
    cur_H = cat_H;
    cur_W = cat_W;
    fprintf(stderr, "ppfn: concat -> (%d, %d, %d)\n", cur_ch, cur_H, cur_W);

    // stem3: Conv(64→32, k=3, s=2) + ReLU
    x = apply_conv(ctx->stem[3], cat.data(), 64, cur_H, cur_W, 32, 3, 3, 2, 1, true, oH, oW);
    cur_ch = 32;
    cur_H = oH;
    cur_W = oW;
    fprintf(stderr, "ppfn: stem3 -> (%d, %d, %d)\n", cur_ch, cur_H, cur_W);

    // stem4: Conv(32→48, k=1, s=1) + ReLU
    x = apply_conv(ctx->stem[4], x.data(), 32, cur_H, cur_W, 48, 1, 1, 1, 1, true, oH, oW);
    cur_ch = 48;
    cur_H = oH;
    cur_W = oW;
    fprintf(stderr, "ppfn: stem4 -> (%d, %d, %d)\n", cur_ch, cur_H, cur_W);

    // ---- Stages ----
    for (int si = 0; si < 4; si++) {
        auto & st = ctx->stages[si];

        // Downsample (depthwise conv, no ReLU)
        if (st.has_downsample) {
            auto dw = to_f32(st.downsample.w);
            auto db = to_f32(st.downsample.b);
            int ds_kh = 3, ds_kw = 3;
            int ds_pad = 1;
            int ds_oH = (cur_H + 2 * ds_pad - ds_kh) / 2 + 1;
            int ds_oW = (cur_W + 2 * ds_pad - ds_kw) / 2 + 1;
            std::vector<float> ds_out(cur_ch * ds_oH * ds_oW);
            conv2d_cpu(x.data(), ds_out.data(), dw.data(), db.data(), cur_ch, cur_ch, cur_H, cur_W, ds_kh, ds_kw, 2,
                       ds_pad, cur_ch);
            // No ReLU for downsample (use_act=False)
            x = std::move(ds_out);
            cur_H = ds_oH;
            cur_W = ds_oW;
            fprintf(stderr, "ppfn: stage%d downsample -> (%d, %d, %d)\n", si, cur_ch, cur_H, cur_W);
        }

        // HG_Blocks
        for (int bi = 0; bi < st.n_blocks; bi++) {
            auto & blk = st.blocks[bi];
            bool residual = (bi > 0);
            std::vector<float> identity;
            if (residual) identity = x;

            // Collect outputs for concatenation
            std::vector<std::vector<float>> outputs;
            outputs.push_back(x); // input to block

            int layer_in_ch = cur_ch;
            int layer_out_ch = st.mid_ch;

            for (int li = 0; li < st.n_layers; li++) {
                auto & lay = blk.layers[li];
                int in_c = (li == 0) ? layer_in_ch : layer_out_ch;

                if (lay.is_light) {
                    // LightConvBNAct: conv1 (1×1 pointwise, no ReLU) + conv2 (depthwise + ReLU)
                    auto c1 =
                        apply_conv(lay.conv1, x.data(), in_c, cur_H, cur_W, layer_out_ch, 1, 1, 1, 1, false, oH, oW);
                    x = apply_conv(lay.conv2, c1.data(), layer_out_ch, oH, oW, layer_out_ch, st.kernel_size,
                                   st.kernel_size, 1, layer_out_ch, true, oH, oW);
                } else {
                    // ConvBNAct: single conv + ReLU
                    x = apply_conv(lay.conv, x.data(), in_c, cur_H, cur_W, layer_out_ch, st.kernel_size, st.kernel_size,
                                   1, 1, true, oH, oW);
                }
                cur_H = oH;
                cur_W = oW;
                outputs.push_back(x);
            }

            // Concatenate all outputs on channel dim
            int total_ch = 0;
            for (auto & o : outputs) total_ch += (int)(o.size() / (cur_H * cur_W));
            std::vector<float> concat(total_ch * cur_H * cur_W);
            int ch_offset = 0;
            for (auto & o : outputs) {
                int och = (int)(o.size() / (cur_H * cur_W));
                for (int c = 0; c < och; c++)
                    memcpy(&concat[(ch_offset + c) * cur_H * cur_W], &o[c * cur_H * cur_W],
                           cur_H * cur_W * sizeof(float));
                ch_offset += och;
            }

            // Aggregation: squeeze (1×1) + ReLU → excite (1×1) + ReLU
            int squeeze_ch = st.out_ch / 2;
            x = apply_conv(blk.agg_squeeze, concat.data(), total_ch, cur_H, cur_W, squeeze_ch, 1, 1, 1, 1, true, oH,
                           oW);
            x = apply_conv(blk.agg_excite, x.data(), squeeze_ch, cur_H, cur_W, st.out_ch, 1, 1, 1, 1, true, oH, oW);
            cur_ch = st.out_ch;

            // Residual
            if (residual && !identity.empty()) {
                for (int i = 0; i < (int)x.size() && i < (int)identity.size(); i++) x[i] += identity[i];
            }

            fprintf(stderr, "ppfn: stage%d block%d -> (%d, %d, %d)\n", si, bi, cur_ch, cur_H, cur_W);
        }
    }

    // Flatten: (2048, H, W) → (H*W, 2048) sequence
    int N = cur_H * cur_W;
    ctx->n_enc_tokens = N;
    ctx->enc_out.resize(N * cur_ch);

    // Transpose CHW → NHW (spatial positions as sequence)
    for (int n = 0; n < N; n++) {
        int y = n / cur_W;
        int xi = n % cur_W;
        for (int c = 0; c < cur_ch; c++) ctx->enc_out[n * cur_ch + c] = x[c * cur_H * cur_W + y * cur_W + xi];
    }

    fprintf(stderr, "ppfn: encoder output (%d, %d)\n", N, cur_ch);
}

static void run_encoder_preferred(ppformulanet_ocr_context * ctx, const float * input, int H, int W) {
#ifdef SNIPLITE_VULKAN_PREFERRED
    const bool try_graph = !ctx->vulkan_failed;
#else
    const bool try_graph = !core_env::on("PPFN_SCALAR_ENCODER");
#endif
    if (try_graph && ctx->enc_sched && run_encoder_ggml(ctx, input, H, W)) return;
#ifdef SNIPLITE_VULKAN_PREFERRED
    if (ctx->enc_sched_is_vulkan) {
        ctx->vulkan_failed = true;
        fprintf(stderr, "ppfn: Vulkan encoder failed; retrying on CPU\n");
    }
#endif
    run_encoder(ctx, input, H, W);
}

// ---------------------------------------------------------------------------
// Projection + decoder
// ---------------------------------------------------------------------------

static bool ppfn_linear_matrix_vulkan(ppformulanet_ocr_context * ctx, const float * input, int rows, int in_dim,
                                      int out_dim, const ggml_tensor * weight, const ggml_tensor * bias,
                                      std::vector<float> & output) {
#ifdef SNIPLITE_VULKAN_PREFERRED
    if (ctx->vulkan_failed || !ctx->enc_sched_is_vulkan || !ctx->enc_sched) return false;
    size_t meta_size = ggml_tensor_overhead() * 64 + ggml_graph_overhead_custom(32, false);
    std::vector<uint8_t> meta(meta_size);
    ggml_init_params ip = { meta_size, meta.data(), true };
    ggml_context * g = ggml_init(ip);
    if (!g) return false;
    ggml_cgraph * gf = ggml_new_graph_custom(g, 32, false);
    ggml_tensor * x = ggml_new_tensor_2d(g, GGML_TYPE_F32, in_dim, rows);
    ggml_set_name(x, "ppfn_matrix_input");
    ggml_set_input(x);
    ggml_tensor * y = ggml_mul_mat(g, (ggml_tensor *)weight, x);
    if (bias) y = ggml_add(g, y, ggml_reshape_2d(g, (ggml_tensor *)bias, out_dim, 1));
    y = ggml_cont(g, y);
    ggml_set_name(y, "ppfn_matrix_output");
    ggml_set_output(y);
    ggml_build_forward_expand(gf, y);

    ggml_backend_sched_reset(ctx->enc_sched);
    bool ok = ggml_backend_sched_alloc_graph(ctx->enc_sched, gf);
    if (ok) {
        ggml_backend_tensor_set(x, input, 0, (size_t)rows * in_dim * sizeof(float));
        ok = ggml_backend_sched_graph_compute(ctx->enc_sched, gf) == GGML_STATUS_SUCCESS;
    }
    if (ok) {
        output.resize((size_t)rows * out_dim);
        ggml_backend_tensor_get(y, output.data(), 0, output.size() * sizeof(float));
    }
    ggml_free(g);
    return ok;
#else
    (void)ctx; (void)input; (void)rows; (void)in_dim; (void)out_dim; (void)weight; (void)bias; (void)output;
    return false;
#endif
}

static void project_encoder(ppformulanet_ocr_context * ctx) {
    // Linear projection: enc_out [N, 2048] → proj_out [N, 384]
    const int N = ctx->n_enc_tokens;
    const int E = ctx->hparams.enc_hidden;
    const int D = ctx->hparams.dec_d_model;

#ifdef SNIPLITE_VULKAN_PREFERRED
    if (ppfn_linear_matrix_vulkan(ctx, ctx->enc_out.data(), N, E, D, ctx->proj_w, ctx->proj_b, ctx->proj_out)) {
        fprintf(stderr, "ppfn: projected on Vulkan (%d, %d)\n", N, D);
        return;
    }
    if (ctx->enc_sched_is_vulkan) {
        ctx->vulkan_failed = true;
        fprintf(stderr, "ppfn: Vulkan projection failed; retrying on CPU\n");
    }
#endif

    const float * wv = ctx->weights.get(ctx->proj_w);
    const float * bv = ctx->weights.get(ctx->proj_b);

    ctx->proj_out.resize(N * D);
    for (int n = 0; n < N; n++) {
        linear_cpu(ctx->enc_out.data() + n * E, ctx->proj_out.data() + n * D, E, D, wv, bv);
    }

    fprintf(stderr, "ppfn: projected (%d, %d)\n", N, D);
}

static void precompute_cross_kv(ppformulanet_ocr_context * ctx) {
    const int N = ctx->n_enc_tokens;
    const int D = ctx->hparams.dec_d_model;
    const int n_dec = ctx->hparams.dec_layers;

    ctx->cross_k_cache.resize(n_dec);
    ctx->cross_v_cache.resize(n_dec);

    for (int li = 0; li < n_dec; li++) {
        ctx->cross_k_cache[li].resize(N * D);
        ctx->cross_v_cache[li].resize(N * D);

#ifdef SNIPLITE_VULKAN_PREFERRED
        std::vector<float> gpu_k, gpu_v;
        if (ppfn_linear_matrix_vulkan(ctx, ctx->proj_out.data(), N, D, D, ctx->dec_layers[li].cross_k_w,
                                      ctx->dec_layers[li].cross_k_b, gpu_k) &&
            ppfn_linear_matrix_vulkan(ctx, ctx->proj_out.data(), N, D, D, ctx->dec_layers[li].cross_v_w,
                                      ctx->dec_layers[li].cross_v_b, gpu_v)) {
            ctx->cross_k_cache[li] = std::move(gpu_k);
            ctx->cross_v_cache[li] = std::move(gpu_v);
            continue;
        }
        if (ctx->enc_sched_is_vulkan) {
            ctx->vulkan_failed = true;
            fprintf(stderr, "ppfn: Vulkan cross K/V failed; retrying on CPU\n");
        }
#endif

        for (int n = 0; n < N; n++) {
            linear_cached(ctx, &ctx->proj_out[n * D], &ctx->cross_k_cache[li][n * D], D, D, ctx->dec_layers[li].cross_k_w,
                       ctx->dec_layers[li].cross_k_b);
            linear_cached(ctx, &ctx->proj_out[n * D], &ctx->cross_v_cache[li][n * D], D, D, ctx->dec_layers[li].cross_v_w,
                       ctx->dec_layers[li].cross_v_b);
        }
    }

    fprintf(stderr, "ppfn: cross K/V cached (%d layers, %d tokens)\n", n_dec, N);
}


static bool decoder_step_vulkan(ppformulanet_ocr_context * ctx, int tok, int step,
                                std::vector<std::vector<float>> & kv_k, std::vector<std::vector<float>> & kv_v,
                                std::vector<float> & logits) {
#ifdef SNIPLITE_VULKAN_PREFERRED
    if (ctx->vulkan_failed || !ctx->enc_sched_is_vulkan || !ctx->enc_sched) return false;
    const auto & hp = ctx->hparams;
    const int D = hp.dec_d_model, V = hp.vocab_size, N = ctx->n_enc_tokens;
    const int heads = hp.dec_heads, head_dim = D / heads;
    const int layers = hp.dec_layers;
    const int past = step;
    const size_t meta_size = ggml_tensor_overhead() * 4096 + ggml_graph_overhead_custom(2048, false);
    if (ctx->dec_compute_meta.size() < meta_size) ctx->dec_compute_meta.resize(meta_size);
    ggml_init_params ip = { meta_size, ctx->dec_compute_meta.data(), true };
    ggml_context * g = ggml_init(ip);
    if (!g) return false;
    ggml_cgraph * gf = ggml_new_graph_custom(g, 2048, false);
    auto linear = [g](ggml_tensor * x, ggml_tensor * w, ggml_tensor * b, int out_dim) {
        ggml_tensor * y = ggml_mul_mat(g, w, x);
        if (b) y = ggml_add(g, y, ggml_reshape_2d(g, b, out_dim, 1));
        return y;
    };
    auto norm = [g](ggml_tensor * x, ggml_tensor * w, ggml_tensor * b) {
        return ggml_add(g, ggml_mul(g, ggml_norm(g, x, 1e-5f), w), b);
    };
    auto attention = [g, heads, head_dim](ggml_tensor * q, ggml_tensor * k, ggml_tensor * v) {
        ggml_tensor * joined = nullptr;
        for (int h = 0; h < heads; ++h) {
            size_t off = (size_t)h * head_dim * sizeof(float);
            ggml_tensor * qh = ggml_view_2d(g, q, head_dim, 1, q->nb[1], off);
            ggml_tensor * kh = ggml_view_2d(g, k, head_dim, k->ne[1], k->nb[1], off);
            ggml_tensor * vh = ggml_view_2d(g, v, head_dim, v->ne[1], v->nb[1], off);
            ggml_tensor * scores = ggml_scale(g, ggml_mul_mat(g, kh, qh), 1.0f / sqrtf((float)head_dim));
            ggml_tensor * probs = ggml_soft_max(g, scores);
            ggml_tensor * result = ggml_mul_mat(g, ggml_cont(g, ggml_transpose(g, vh)), probs);
            joined = joined ? ggml_concat(g, joined, result, 0) : result;
        }
        return joined;
    };

    ggml_tensor * token_id = ggml_new_tensor_1d(g, GGML_TYPE_I32, 1);
    ggml_set_input(token_id);
    ggml_tensor * pos_id = ggml_new_tensor_1d(g, GGML_TYPE_I32, 1);
    ggml_set_input(pos_id);
    const int32_t token_value = tok;
    const int32_t position_value = step + 2;
    ggml_tensor * x = ggml_add(g,
        ggml_scale(g, ggml_get_rows(g, ctx->tok_embed, token_id), sqrtf((float)D)),
        ggml_get_rows(g, ctx->pos_embed_dec, pos_id));
    x = norm(x, ctx->embed_ln_w, ctx->embed_ln_b);

    std::vector<ggml_tensor *> updated_k(layers), updated_v(layers);
    std::vector<ggml_tensor *> cache_k_input(layers), cache_v_input(layers);
    std::vector<ggml_tensor *> cross_k_input(layers), cross_v_input(layers);
    for (int li = 0; li < layers; ++li) {
        const auto & l = ctx->dec_layers[li];
        ggml_tensor * residual = x;
        ggml_tensor * xn = norm(x, l.self_ln_w, l.self_ln_b);
        ggml_tensor * q = linear(xn, l.self_q_w, l.self_q_b, D);
        ggml_tensor * k = linear(xn, l.self_k_w, l.self_k_b, D);
        ggml_tensor * v = linear(xn, l.self_v_w, l.self_v_b, D);
        if (past > 0) {
            cache_k_input[li] = ggml_new_tensor_2d(g, GGML_TYPE_F32, D, past);
            cache_v_input[li] = ggml_new_tensor_2d(g, GGML_TYPE_F32, D, past);
            ggml_set_input(cache_k_input[li]); ggml_set_input(cache_v_input[li]);
            k = ggml_concat(g, cache_k_input[li], k, 1);
            v = ggml_concat(g, cache_v_input[li], v, 1);
        }
        updated_k[li] = k; updated_v[li] = v;
        ggml_tensor * sa = attention(q, k, v);
        x = ggml_add(g, residual, linear(sa, l.self_out_w, l.self_out_b, D));

        residual = x;
        xn = norm(x, l.cross_ln_w, l.cross_ln_b);
        ggml_tensor * cq = linear(xn, l.cross_q_w, l.cross_q_b, D);
        cross_k_input[li] = ggml_new_tensor_2d(g, GGML_TYPE_F32, D, N);
        cross_v_input[li] = ggml_new_tensor_2d(g, GGML_TYPE_F32, D, N);
        ggml_set_input(cross_k_input[li]); ggml_set_input(cross_v_input[li]);
        ggml_tensor * ca = attention(cq, cross_k_input[li], cross_v_input[li]);
        x = ggml_add(g, residual, linear(ca, l.cross_out_w, l.cross_out_b, D));

        residual = x;
        xn = norm(x, l.ff_ln_w, l.ff_ln_b);
        ggml_tensor * up = linear(xn, l.ff_up_w, l.ff_up_b, hp.dec_ffn_dim);
        ggml_tensor * gelu = ggml_gelu(g, up);
        x = ggml_add(g, residual, linear(gelu, l.ff_down_w, l.ff_down_b, D));
    }
    x = norm(x, ctx->final_ln_w, ctx->final_ln_b);
    ggml_tensor * result = linear(x, ctx->lm_head_w, ctx->lm_head_b, V);
    ggml_set_name(result, "ppfn_logits"); ggml_set_output(result);
    ggml_tensor * argmax = ggml_argmax(g, result);
    ggml_set_output(argmax);
    ggml_build_forward_expand(gf, argmax);
    ggml_build_forward_expand(gf, result);
    for (int li = 0; li < layers; ++li) {
        ggml_set_output(updated_k[li]); ggml_set_output(updated_v[li]);
        ggml_build_forward_expand(gf, updated_k[li]); ggml_build_forward_expand(gf, updated_v[li]);
    }

    ggml_backend_sched_reset(ctx->enc_sched);
    bool ok = ggml_backend_sched_alloc_graph(ctx->enc_sched, gf);
    if (ok) {
        ggml_backend_tensor_set(token_id, &token_value, 0, sizeof(token_value));
        ggml_backend_tensor_set(pos_id, &position_value, 0, sizeof(position_value));
        for (int li = 0; li < layers; ++li) {
            if (past > 0) {
                ggml_backend_tensor_set(cache_k_input[li], kv_k[li].data(), 0, kv_k[li].size() * sizeof(float));
                ggml_backend_tensor_set(cache_v_input[li], kv_v[li].data(), 0, kv_v[li].size() * sizeof(float));
            }
            ggml_backend_tensor_set(cross_k_input[li], ctx->cross_k_cache[li].data(), 0,
                                    ctx->cross_k_cache[li].size() * sizeof(float));
            ggml_backend_tensor_set(cross_v_input[li], ctx->cross_v_cache[li].data(), 0,
                                    ctx->cross_v_cache[li].size() * sizeof(float));
        }
        ok = ggml_backend_sched_graph_compute(ctx->enc_sched, gf) == GGML_STATUS_SUCCESS;
    }
    if (ok) {
        logits.resize(V);
        ggml_backend_tensor_get(result, logits.data(), 0, logits.size() * sizeof(float));
        ggml_backend_tensor_get(argmax, &ctx->decoder_step_argmax, 0, sizeof(ctx->decoder_step_argmax));
        ctx->decoder_step_vulkan_used = true;
        for (int li = 0; li < layers; ++li) {
            kv_k[li].resize((size_t)(past + 1) * D); kv_v[li].resize((size_t)(past + 1) * D);
            ggml_backend_tensor_get(updated_k[li], kv_k[li].data(), 0, kv_k[li].size() * sizeof(float));
            ggml_backend_tensor_get(updated_v[li], kv_v[li].data(), 0, kv_v[li].size() * sizeof(float));
        }
    }
    ggml_free(g);
    return ok;
#else
    (void)ctx; (void)tok; (void)step; (void)kv_k; (void)kv_v; (void)logits;
    return false;
#endif
}
struct PpfnDecoderScratch {
    std::vector<float> x;
    std::vector<float> residual;
    std::vector<float> q;
    std::vector<float> k;
    std::vector<float> v;
    std::vector<float> sa;
    std::vector<float> sa_proj;
    std::vector<float> cq;
    std::vector<float> ca;
    std::vector<float> ca_proj;
    std::vector<float> ff_up;
    std::vector<float> ff_down;

    void resize(int d_model, int ffn_dim) {
        x.resize(d_model);
        residual.resize(d_model);
        q.resize(d_model);
        k.resize(d_model);
        v.resize(d_model);
        sa.resize(d_model);
        sa_proj.resize(d_model);
        cq.resize(d_model);
        ca.resize(d_model);
        ca_proj.resize(d_model);
        ff_up.resize(ffn_dim);
        ff_down.resize(d_model);
    }
};

static std::vector<float> decoder_step(ppformulanet_ocr_context * ctx, int tok, int step,
                                       std::vector<std::vector<float>> & kv_k, std::vector<std::vector<float>> & kv_v,
                                       PpfnDecoderScratch & scratch) {
    ctx->decoder_step_vulkan_used = false;
#ifdef SNIPLITE_VULKAN_PREFERRED
    std::vector<float> gpu_logits;
    if (decoder_step_vulkan(ctx, tok, step, kv_k, kv_v, gpu_logits)) return gpu_logits;
    if (ctx->enc_sched_is_vulkan) {
        ctx->vulkan_failed = true;
        fprintf(stderr, "ppfn: Vulkan decoder failed at step %d; retrying on CPU\n", step);
    }
#endif
    const auto & hp = ctx->hparams;
    const int D = hp.dec_d_model;
    const int V = hp.vocab_size;
    const int n_enc = ctx->n_enc_tokens;

    auto & x = scratch.x;
    std::fill(x.begin(), x.end(), 0.0f);

    // Token embedding * sqrt(d_model) + position embedding
    const float * emb = ctx->weights.get(ctx->tok_embed);
    const float * pe = ctx->weights.get(ctx->pos_embed_dec);
    float scale = sqrtf((float)D);

    if (tok >= 0 && tok < V) {
        for (int i = 0; i < D; i++) x[i] = emb[tok * D + i] * scale;
    }

    // MBart position offset: position_ids = step + 2
    int pos = step + 2;
    if (pos < ggml_nelements(ctx->pos_embed_dec) / D) {
        for (int i = 0; i < D; i++) x[i] += pe[pos * D + i];
    }

    // Embedding LayerNorm
    static const bool dbg = core_env::on("PPFN_DEBUG");
    if (dbg && step == 0) {
        fprintf(stderr, "ppfn: [dbg] tok_emb+pos first 5: %.5f %.5f %.5f %.5f %.5f\n", x[0], x[1], x[2], x[3], x[4]);
    }
    layernorm_cached(ctx, x.data(), x.data(), D, ctx->embed_ln_w, ctx->embed_ln_b, 1e-5f);
    if (dbg && step == 0) {
        fprintf(stderr, "ppfn: [dbg] after embed_ln first 5: %.5f %.5f %.5f %.5f %.5f\n", x[0], x[1], x[2], x[3], x[4]);
    }

    // Decoder layers (MBart PRE-LN: LN before attention, residual skips LN)
    for (int li = 0; li < hp.dec_layers; li++) {
        const auto & l = ctx->dec_layers[li];

        // --- Self-attention (PRE-LN) ---
        auto & residual = scratch.residual;
        std::copy(x.begin(), x.end(), residual.begin());
        layernorm_cached(ctx, x.data(), x.data(), D, l.self_ln_w, l.self_ln_b, 1e-5f);

        auto & q = scratch.q;
        auto & k = scratch.k;
        auto & v = scratch.v;
        linear_cached(ctx, x.data(), q.data(), D, D, l.self_q_w, l.self_q_b);
        linear_cached(ctx, x.data(), k.data(), D, D, l.self_k_w, l.self_k_b);
        linear_cached(ctx, x.data(), v.data(), D, D, l.self_v_w, l.self_v_b);

        // Append to KV cache
        kv_k[li].insert(kv_k[li].end(), k.begin(), k.end());
        kv_v[li].insert(kv_v[li].end(), v.begin(), v.end());
        int n_kv = (int)(kv_k[li].size() / D);

        auto & sa = scratch.sa;
        mha_1q_cpu(q.data(), kv_k[li].data(), kv_v[li].data(), sa.data(), n_kv, D, hp.dec_heads);

        auto & sa_proj = scratch.sa_proj;
        linear_cached(ctx, sa.data(), sa_proj.data(), D, D, l.self_out_w, l.self_out_b);
        for (int i = 0; i < D; i++) x[i] = residual[i] + sa_proj[i]; // residual skips LN

        // --- Cross-attention (PRE-LN) ---
        residual.assign(x.begin(), x.end());
        layernorm_cached(ctx, x.data(), x.data(), D, l.cross_ln_w, l.cross_ln_b, 1e-5f);

        auto & cq = scratch.cq;
        linear_cached(ctx, x.data(), cq.data(), D, D, l.cross_q_w, l.cross_q_b);

        auto & ca = scratch.ca;
        mha_1q_cpu(cq.data(), ctx->cross_k_cache[li].data(), ctx->cross_v_cache[li].data(), ca.data(), n_enc, D,
                   hp.dec_heads);

        auto & ca_proj = scratch.ca_proj;
        linear_cached(ctx, ca.data(), ca_proj.data(), D, D, l.cross_out_w, l.cross_out_b);
        for (int i = 0; i < D; i++) x[i] = residual[i] + ca_proj[i]; // residual skips LN

        // --- FFN (PRE-LN) ---
        std::copy(x.begin(), x.end(), residual.begin());
        layernorm_cached(ctx, x.data(), x.data(), D, l.ff_ln_w, l.ff_ln_b, 1e-5f);

        auto & ff_up = scratch.ff_up;
        linear_cached(ctx, x.data(), ff_up.data(), D, hp.dec_ffn_dim, l.ff_up_w, l.ff_up_b);
        // GELU activation
        for (int i = 0; i < hp.dec_ffn_dim; i++) {
            float vi = ff_up[i];
            ff_up[i] = 0.5f * vi * (1.0f + tanhf(0.7978845608f * (vi + 0.044715f * vi * vi * vi)));
        }
        auto & ff_down = scratch.ff_down;
        linear_cached(ctx, ff_up.data(), ff_down.data(), hp.dec_ffn_dim, D, l.ff_down_w, l.ff_down_b);
        for (int i = 0; i < D; i++) x[i] = residual[i] + ff_down[i]; // residual skips LN
    }

    // Final LayerNorm
    if (dbg && step == 0) {
        fprintf(stderr, "ppfn: [dbg] before final_ln first 5: %.5f %.5f %.5f %.5f %.5f\n", x[0], x[1], x[2], x[3],
                x[4]);
    }
    layernorm_cached(ctx, x.data(), x.data(), D, ctx->final_ln_w, ctx->final_ln_b, 1e-5f);
    if (dbg && step == 0) {
        fprintf(stderr, "ppfn: [dbg] after final_ln first 5: %.5f %.5f %.5f %.5f %.5f\n", x[0], x[1], x[2], x[3], x[4]);
    }

    // LM head
    std::vector<float> logits(V);
    linear_cached(ctx, x.data(), logits.data(), D, V, ctx->lm_head_w, ctx->lm_head_b);
    if (dbg && step == 0) {
        fprintf(stderr, "ppfn: [dbg] logits[0:5]: %.4f %.4f %.4f %.4f %.4f\n", logits[0], logits[1], logits[2],
                logits[3], logits[4]);
        if (V > 224) fprintf(stderr, "ppfn: [dbg] logits[91]=%f logits[224]=%f\n", logits[91], logits[224]);
    }

    return logits;
}


static std::vector<int> greedy_decode(ppformulanet_ocr_context * ctx) {
    ctx->char_confidences.clear();
    const auto & hp = ctx->hparams;
    PpfnDecoderScratch scratch;
    scratch.resize(hp.dec_d_model, hp.dec_ffn_dim);
    const int max_steps = std::min(hp.max_seq_len, 512);

    std::vector<int> tokens;
    int tok = hp.decoder_start_token;

    // Self-attention KV caches (per decoder layer)
    std::vector<std::vector<float>> kv_k(hp.dec_layers);
    std::vector<std::vector<float>> kv_v(hp.dec_layers);

    for (int step = 0; step < max_steps; step++) {
        auto logits = decoder_step(ctx, tok, step, kv_k, kv_v, scratch);

        // Greedy argmax
#ifdef SNIPLITE_VULKAN_PREFERRED
        int best = ctx->decoder_step_vulkan_used ? ctx->decoder_step_argmax : 0;
        float best_s = logits[best];
        if (!ctx->decoder_step_vulkan_used) {
            for (int v = 1; v < hp.vocab_size; v++)
                if (logits[v] > best_s) { best_s = logits[v]; best = v; }
        }
#else
        int best = 0;
        float best_s = logits[0];
        for (int v = 1; v < hp.vocab_size; v++)
            if (logits[v] > best_s) { best_s = logits[v]; best = v; }
#endif

        if (step < 5) {
            fprintf(stderr, "ppfn: dec step %d: tok=%d best=%d (%.3f)\n", step, tok, best, best_s);
        }

        if (best == hp.eos_token || best == hp.pad_token) break;

        // Confidence: softmax of winning token
        {
            float max_l = logits[0];
            for (int v = 1; v < hp.vocab_size; v++)
                if (logits[v] > max_l) max_l = logits[v];
            float sum_e = 0;
            for (int v = 0; v < hp.vocab_size; v++) sum_e += expf(logits[v] - max_l);
            ctx->char_confidences.push_back(expf(logits[best] - max_l) / sum_e);
        }

        tokens.push_back(best);
        tok = best;
    }

    return tokens;
}

// ---------------------------------------------------------------------------
// Beam search decoder
// ---------------------------------------------------------------------------

struct PpfnBeam {
    std::vector<int> tokens;
    float score;
    int prev_token;
    bool finished;
    std::vector<std::vector<float>> kv_k;
    std::vector<std::vector<float>> kv_v;
};

static std::vector<int> beam_decode(ppformulanet_ocr_context * ctx, int beam_width) {
    const auto & hp = ctx->hparams;
    PpfnDecoderScratch scratch;
    scratch.resize(hp.dec_d_model, hp.dec_ffn_dim);
    const int V = hp.vocab_size;
    const int n_dec = hp.dec_layers;
    const int max_steps = std::min(hp.max_seq_len, 512);

    std::vector<PpfnBeam> beams(1);
    beams[0].score = 0.0f;
    beams[0].prev_token = hp.decoder_start_token;
    beams[0].finished = false;
    beams[0].kv_k.resize(n_dec);
    beams[0].kv_v.resize(n_dec);

    std::vector<PpfnBeam> completed;
    std::vector<PpfnBeam> next_beams;
    struct Cand {
        int bi;
        int tok;
        float score;
    };
    std::vector<Cand> cands;
    std::vector<int> selected_children;

    for (int step = 0; step < max_steps; step++) {
        cands.clear();

        for (int bi = 0; bi < (int)beams.size(); bi++) {
            if (beams[bi].finished) continue;
            auto logits = decoder_step(ctx, beams[bi].prev_token, step, beams[bi].kv_k, beams[bi].kv_v, scratch);
            float max_l = *std::max_element(logits.begin(), logits.end());
            float sum_e = 0.0f;
            for (int v = 0; v < V; v++) {
                logits[v] = expf(logits[v] - max_l);
                sum_e += logits[v];
            }
            float log_sum = logf(sum_e) + max_l;
            for (int v = 0; v < V; v++) {
                float log_p = logits[v] > 0 ? logf(logits[v]) + max_l - log_sum : -100.0f;
                cands.push_back({ bi, v, beams[bi].score + log_p });
            }
        }

        if (cands.empty()) break;

        int keep = std::min(beam_width, (int)cands.size());
        std::partial_sort(cands.begin(), cands.begin() + keep, cands.end(),
                          [](const Cand & a, const Cand & b) { return a.score > b.score; });

        selected_children.assign(beams.size(), 0);
        for (int i = 0; i < keep; i++) {
            selected_children[cands[i].bi]++;
        }

        next_beams.clear();
        next_beams.reserve(keep);
        for (int i = 0; i < keep; i++) {
            const auto & c = cands[i];
            PpfnBeam nb;
            nb.tokens = beams[c.bi].tokens;
            if (selected_children[c.bi] == 1) {
                nb.kv_k = std::move(beams[c.bi].kv_k);
                nb.kv_v = std::move(beams[c.bi].kv_v);
            } else {
                nb.kv_k = beams[c.bi].kv_k;
                nb.kv_v = beams[c.bi].kv_v;
            }
            selected_children[c.bi]--;
            nb.score = c.score;
            if (c.tok == hp.eos_token || c.tok == hp.pad_token) {
                nb.finished = true;
                nb.prev_token = c.tok;
                float len = (float)nb.tokens.size();
                nb.score /= len > 0 ? len : 1.0f;
                completed.push_back(std::move(nb));
            } else {
                nb.tokens.push_back(c.tok);
                nb.prev_token = c.tok;
                nb.finished = false;
                next_beams.push_back(std::move(nb));
            }
        }

        beams.swap(next_beams);
        if (beams.empty()) break;
    }

    for (auto & b : beams) {
        float len = (float)b.tokens.size();
        b.score /= len > 0 ? len : 1.0f;
        completed.push_back(std::move(b));
    }

    if (completed.empty()) return {};
    auto best = std::max_element(completed.begin(), completed.end(),
                                 [](const PpfnBeam & a, const PpfnBeam & b) { return a.score < b.score; });
    return best->tokens;
}

// ---------------------------------------------------------------------------
// Init / Free / API
// ---------------------------------------------------------------------------

ppformulanet_ocr_context * ppformulanet_ocr_init(const char * model_path, int n_threads) {
    auto ctx = std::make_unique<ppformulanet_ocr_context>();
    ctx->n_threads = n_threads > 0 ? n_threads : 1;

#ifdef SNIPLITE_VULKAN_PREFERRED
    static std::once_flag backend_plugins_once;
    std::call_once(backend_plugins_once, [model_path]() {
        std::string plugin_dir = model_path ? model_path : ".";
        const size_t sep = plugin_dir.find_last_of("/\\");
        if (sep == std::string::npos) plugin_dir = ".";
        else plugin_dir.resize(sep);
        ggml_backend_load_all_from_path(plugin_dir.c_str());
    });
#endif

    gguf_context * gctx = core_gguf::open_metadata(model_path);
    if (!gctx) {
        fprintf(stderr, "ppfn: can't open %s\n", model_path);
        return nullptr;
    }

    auto & hp = ctx->hparams;
    hp.image_size = core_gguf::kv_u32(gctx, "ppfn.encoder.image_size", 384);
    hp.enc_hidden = core_gguf::kv_u32(gctx, "ppfn.encoder.hidden_size", 2048);
    hp.dec_layers = core_gguf::kv_u32(gctx, "ppfn.decoder.decoder_layers", 2);
    hp.dec_heads = core_gguf::kv_u32(gctx, "ppfn.decoder.decoder_attention_heads", 16);
    hp.dec_d_model = core_gguf::kv_u32(gctx, "ppfn.decoder.d_model", 384);
    hp.dec_ffn_dim = core_gguf::kv_u32(gctx, "ppfn.decoder.decoder_ffn_dim", 1536);
    hp.vocab_size = core_gguf::kv_u32(gctx, "ppfn.decoder.vocab_size", 50000);
    hp.max_seq_len = core_gguf::kv_u32(gctx, "ppfn.decoder.max_position_embeddings", 1027);
    hp.cross_attn_dim = hp.dec_d_model;
    hp.bos_token = core_gguf::kv_u32(gctx, "ppfn.decoder.bos_token_id", 0);
    hp.eos_token = core_gguf::kv_u32(gctx, "ppfn.decoder.eos_token_id", 2);
    hp.pad_token = core_gguf::kv_u32(gctx, "ppfn.decoder.pad_token_id", 1);
    hp.decoder_start_token = core_gguf::kv_u32(gctx, "ppfn.decoder.decoder_start_token_id", 0);

    // Read stage configs
    for (int si = 0; si < 4; si++) {
        char buf[128];
        auto & st = ctx->stages[si];
        snprintf(buf, sizeof(buf), "ppfn.encoder.stage%d.in_channels", si);
        st.in_ch = core_gguf::kv_u32(gctx, buf, 48);
        snprintf(buf, sizeof(buf), "ppfn.encoder.stage%d.mid_channels", si);
        st.mid_ch = core_gguf::kv_u32(gctx, buf, 48);
        snprintf(buf, sizeof(buf), "ppfn.encoder.stage%d.out_channels", si);
        st.out_ch = core_gguf::kv_u32(gctx, buf, 128);
        snprintf(buf, sizeof(buf), "ppfn.encoder.stage%d.n_blocks", si);
        st.n_blocks = core_gguf::kv_u32(gctx, buf, 1);
        snprintf(buf, sizeof(buf), "ppfn.encoder.stage%d.n_layers", si);
        st.n_layers = core_gguf::kv_u32(gctx, buf, 6);
        snprintf(buf, sizeof(buf), "ppfn.encoder.stage%d.kernel_size", si);
        st.kernel_size = core_gguf::kv_u32(gctx, buf, 3);
        snprintf(buf, sizeof(buf), "ppfn.encoder.stage%d.light_block", si);
        st.light_block = core_gguf::kv_u32(gctx, buf, 0) != 0;
    }

    ctx->vocab = core_gguf::kv_str_array(gctx, "tokenizer.tokens");
    core_gguf::free_metadata(gctx);

    fprintf(stderr, "ppfn: enc_hidden=%d dec=%dL/%dH/%d vocab=%d(%zu)\n", hp.enc_hidden, hp.dec_layers, hp.dec_heads,
            hp.dec_d_model, hp.vocab_size, ctx->vocab.size());

    // Keep model weights in CPU memory so CPU fallback and Vulkan graph inputs
    // can both read them safely. Graph schedulers own compute placement.
    ctx->backend = ggml_backend_cpu_init();
    if (!ctx->backend) ctx->backend = ggml_backend_cpu_init();
    if (ggml_backend_is_cpu(ctx->backend)) ggml_backend_cpu_set_n_threads(ctx->backend, ctx->n_threads);

    if (!core_gguf::load_weights(model_path, ctx->backend, "ppfn", ctx->wl)) {
        ggml_backend_free(ctx->backend);
        return nullptr;
    }
    fprintf(stderr, "ppfn: %zu tensors loaded\n", ctx->wl.tensors.size());

    map_tensors(ctx.get());

    // Verify key tensors
    int n_mapped = 0;
    for (int si = 0; si < 4; si++)
        for (int bi = 0; bi < ctx->stages[si].n_blocks; bi++)
            if (ctx->stages[si].blocks[bi].agg_squeeze.w) n_mapped++;
    fprintf(stderr, "ppfn: %d blocks mapped, %d dec layers\n", n_mapped, hp.dec_layers);

    ctx->bench = core_env::on("CRISPEMBED_PPFN_BENCH");

#ifdef SNIPLITE_VULKAN_PREFERRED
    // Use a Vulkan device explicitly; never silently select another accelerator.
    for (size_t i = 0; i < ggml_backend_dev_count(); ++i) {
        ggml_backend_dev_t dev = ggml_backend_dev_get(i);
        ggml_backend_reg_t reg = dev ? ggml_backend_dev_backend_reg(dev) : nullptr;
        const char * reg_name = reg ? ggml_backend_reg_name(reg) : nullptr;
        if (!reg_name || std::strcmp(reg_name, "Vulkan") != 0) continue;
        ctx->enc_backend = ggml_backend_dev_init(dev, nullptr);
        if (ctx->enc_backend) break;
    }
    if (ctx->enc_backend) {
        ctx->enc_cpu_backend = ggml_backend_cpu_init();
        if (ctx->enc_cpu_backend) {
            ggml_backend_cpu_set_n_threads(ctx->enc_cpu_backend, ctx->n_threads);
            ggml_backend_t backends[] = { ctx->enc_backend, ctx->enc_cpu_backend };
            ctx->enc_sched = ggml_backend_sched_new(backends, nullptr, 2, 8192, false, false);
        }
        ctx->enc_sched_is_vulkan = ctx->enc_sched != nullptr;
    }
    if (!ctx->enc_sched) {
        if (ctx->enc_backend) ggml_backend_free(ctx->enc_backend);
        ctx->enc_backend = nullptr;
        if (ctx->enc_cpu_backend) ggml_backend_free(ctx->enc_cpu_backend);
        ctx->enc_cpu_backend = nullptr;
        fprintf(stderr, "ppfn: Vulkan encoder unavailable; using CPU fallback\n");
    }
#else
    ctx->enc_backend = ggml_backend_cpu_init();
    if (ctx->enc_backend) {
        ggml_backend_cpu_set_n_threads(ctx->enc_backend, ctx->n_threads);
        ggml_backend_t backends[] = { ctx->enc_backend };
        ctx->enc_sched = ggml_backend_sched_new(backends, nullptr, 1, 8192, false, false);
    }
#endif

    return ctx.release();
}

void ppformulanet_ocr_free(ppformulanet_ocr_context * ctx) {
    if (!ctx) return;
    for (auto * buf : ctx->vulkan_resident_bufs)
        if (buf) ggml_backend_buffer_free(buf);
    for (auto * resident_ctx : ctx->vulkan_resident_ctxs)
        if (resident_ctx) ggml_free(resident_ctx);
    if (ctx->enc_sched) ggml_backend_sched_free(ctx->enc_sched);
    if (ctx->enc_backend) ggml_backend_free(ctx->enc_backend);
    if (ctx->enc_cpu_backend) ggml_backend_free(ctx->enc_cpu_backend);
    if (ctx->backend) ggml_backend_free(ctx->backend);
    core_gguf::free_weights(ctx->wl);
    delete ctx;
}

const ppformulanet_ocr_hparams * ppformulanet_ocr_get_hparams(const ppformulanet_ocr_context * ctx) {
    return ctx ? &ctx->hparams : nullptr;
}

const char * ppformulanet_ocr_recognize(ppformulanet_ocr_context * ctx, const float * pixels, int width, int height,
                                        int * out_len) {
    if (!ctx || !pixels) return nullptr;
    const int S = ctx->hparams.image_size;

    const bool bench = ctx->bench;
    auto t_total = std::chrono::steady_clock::now();

    // UniMERNet preprocessing:
    // 1. Resize maintaining aspect ratio to fit in S×S
    // 2. Pad with black (0) to fill S×S
    // 3. Convert grayscale → 3ch (replicate)
    // 4. Normalize: mean=0.7931, std=0.1738
    auto t0 = std::chrono::steady_clock::now();
    const float MEAN = 0.7931f;
    const float STD = 0.1738f;

    // Compute scale to fit within S×S preserving aspect ratio
    float scale = std::min((float)S / width, (float)S / height);
    int new_w = std::min((int)(width * scale), S);
    int new_h = std::min((int)(height * scale), S);
    int pad_left = (S - new_w) / 2;
    int pad_top = (S - new_h) / 2;

    // Fill with normalized black: (0.0 - 0.7931) / 0.1738
    float black_norm = (0.0f - MEAN) / STD;
    std::vector<float> rgb(3 * S * S, black_norm);

    // Bilinear resize + normalize into the center of the padded image
    for (int y = 0; y < new_h; y++) {
        float fy = (float)y / scale;
        int y0 = (int)fy, y1 = std::min(y0 + 1, height - 1);
        float wy = fy - y0;
        int dy = pad_top + y;
        for (int x = 0; x < new_w; x++) {
            float fx = (float)x / scale;
            int x0 = (int)fx, x1 = std::min(x0 + 1, width - 1);
            float wx = fx - x0;
            float v = (1 - wy) * ((1 - wx) * pixels[y0 * width + x0] + wx * pixels[y0 * width + x1]) +
                      wy * ((1 - wx) * pixels[y1 * width + x0] + wx * pixels[y1 * width + x1]);
            float normed = (v - MEAN) / STD;
            int dx = pad_left + x;
            for (int c = 0; c < 3; c++) rgb[c * S * S + dy * S + dx] = normed;
        }
    }
    if (bench)
        fprintf(stderr, "[ppfn-bench] preprocess: %.1f ms\n",
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());

    // Run encoder
    t0 = std::chrono::steady_clock::now();
    run_encoder_preferred(ctx, rgb.data(), S, S);
    if (bench)
        fprintf(stderr, "[ppfn-bench] encoder: %.1f ms\n",
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());

    // Project to decoder dimension
    t0 = std::chrono::steady_clock::now();
    project_encoder(ctx);

    // Precompute cross-attention K/V
    precompute_cross_kv(ctx);

    // Decode
    auto tokens = greedy_decode(ctx);
    if (bench)
        fprintf(stderr, "[ppfn-bench] decoder: %.1f ms\n",
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());

    // Detokenize GPT-2 byte-level BPE pieces to text via the shared decoder
    // (inverse of byte_encoder(): "Ġ" -> space, "Ċ" -> newline, all bytes).
    ctx->result_buf.clear();
    for (int tok : tokens) {
        if (tok < 0 || tok >= (int)ctx->vocab.size()) continue;
        core_bpe::unicode_to_bytes(ctx->vocab[tok], ctx->result_buf);
    }

    if (bench)
        fprintf(stderr, "[ppfn-bench] total: %.1f ms\n",
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t_total).count());

    if (out_len) *out_len = (int)ctx->result_buf.size();
    return ctx->result_buf.c_str();
}

const char * ppformulanet_ocr_recognize_raw(ppformulanet_ocr_context * ctx, const uint8_t * pixel_bytes, int width,
                                            int height, int channels, int * out_len) {
    if (!ctx || !pixel_bytes) return nullptr;

    // Convert to grayscale float [0, 1]
    std::vector<float> gray(width * height);
    for (int i = 0; i < width * height; i++) {
        if (channels == 1) {
            gray[i] = pixel_bytes[i] / 255.0f;
        } else if (channels == 3) {
            gray[i] =
                (0.299f * pixel_bytes[i * 3] + 0.587f * pixel_bytes[i * 3 + 1] + 0.114f * pixel_bytes[i * 3 + 2]) /
                255.0f;
        } else if (channels == 4) {
            gray[i] =
                (0.299f * pixel_bytes[i * 4] + 0.587f * pixel_bytes[i * 4 + 1] + 0.114f * pixel_bytes[i * 4 + 2]) /
                255.0f;
        }
    }

    return ppformulanet_ocr_recognize(ctx, gray.data(), width, height, out_len);
}

const char * ppformulanet_ocr_recognize_chw(ppformulanet_ocr_context * ctx, const float * chw_data, int * out_len) {
    if (!ctx || !chw_data) return nullptr;
    const int S = ctx->hparams.image_size;

    // Input is already preprocessed CHW [3, S, S] normalized data
    run_encoder_preferred(ctx, chw_data, S, S);
    project_encoder(ctx);
    precompute_cross_kv(ctx);
    auto tokens = greedy_decode(ctx);

    // Detokenize GPT-2 byte-level BPE pieces to text via the shared decoder
    // (inverse of byte_encoder(): "Ġ" -> space, "Ċ" -> newline, all bytes).
    ctx->result_buf.clear();
    for (int tok : tokens) {
        if (tok < 0 || tok >= (int)ctx->vocab.size()) continue;
        core_bpe::unicode_to_bytes(ctx->vocab[tok], ctx->result_buf);
    }

    if (out_len) *out_len = (int)ctx->result_buf.size();
    return ctx->result_buf.c_str();
}

const char * ppformulanet_ocr_recognize_beam(ppformulanet_ocr_context * ctx, const float * pixels, int width,
                                             int height, int beam_width, int * out_len) {
    if (!ctx || !pixels) return nullptr;
    if (beam_width <= 1) return ppformulanet_ocr_recognize(ctx, pixels, width, height, out_len);

    const int S = ctx->hparams.image_size;
    const float MEAN = 0.7931f, STD = 0.1738f;
    float scale = std::min((float)S / width, (float)S / height);
    int new_w = std::min((int)(width * scale), S);
    int new_h = std::min((int)(height * scale), S);
    int pad_left = (S - new_w) / 2, pad_top = (S - new_h) / 2;
    float black_norm = (0.0f - MEAN) / STD;
    std::vector<float> rgb(3 * S * S, black_norm);
    for (int y = 0; y < new_h; y++) {
        float fy = (float)y / scale;
        int y0 = (int)fy, y1 = std::min(y0 + 1, height - 1);
        float wy = fy - y0;
        for (int x = 0; x < new_w; x++) {
            float fx = (float)x / scale;
            int x0 = (int)fx, x1 = std::min(x0 + 1, width - 1);
            float wx = fx - x0;
            float v = (1 - wy) * ((1 - wx) * pixels[y0 * width + x0] + wx * pixels[y0 * width + x1]) +
                      wy * ((1 - wx) * pixels[y1 * width + x0] + wx * pixels[y1 * width + x1]);
            float normed = (v - MEAN) / STD;
            int dx = pad_left + x;
            for (int c = 0; c < 3; c++) rgb[c * S * S + (pad_top + y) * S + dx] = normed;
        }
    }

    run_encoder_preferred(ctx, rgb.data(), S, S);
    project_encoder(ctx);
    precompute_cross_kv(ctx);

    auto tokens = beam_decode(ctx, beam_width);

    ctx->result_buf.clear();
    for (int tok : tokens) {
        if (tok < 0 || tok >= (int)ctx->vocab.size()) continue;
        core_bpe::unicode_to_bytes(ctx->vocab[tok], ctx->result_buf);
    }
    if (out_len) *out_len = (int)ctx->result_buf.size();
    return ctx->result_buf.c_str();
}

const char * ppformulanet_ocr_recognize_raw_beam(ppformulanet_ocr_context * ctx, const uint8_t * pixel_bytes, int width,
                                                 int height, int channels, int beam_width, int * out_len) {
    if (!ctx || !pixel_bytes) return nullptr;
    std::vector<float> gray(width * height);
    for (int i = 0; i < width * height; i++) {
        if (channels == 1)
            gray[i] = pixel_bytes[i] / 255.0f;
        else if (channels == 3)
            gray[i] =
                (0.299f * pixel_bytes[i * 3] + 0.587f * pixel_bytes[i * 3 + 1] + 0.114f * pixel_bytes[i * 3 + 2]) /
                255.0f;
        else if (channels == 4)
            gray[i] =
                (0.299f * pixel_bytes[i * 4] + 0.587f * pixel_bytes[i * 4 + 1] + 0.114f * pixel_bytes[i * 4 + 2]) /
                255.0f;
    }
    return ppformulanet_ocr_recognize_beam(ctx, gray.data(), width, height, beam_width, out_len);
}

const float * ppformulanet_ocr_confidences(const ppformulanet_ocr_context * ctx, int * n_chars) {
    if (!ctx || ctx->char_confidences.empty()) {
        if (n_chars) *n_chars = 0;
        return nullptr;
    }
    if (n_chars) *n_chars = (int)ctx->char_confidences.size();
    return ctx->char_confidences.data();
}

float ppformulanet_ocr_mean_confidence(const ppformulanet_ocr_context * ctx) {
    if (!ctx || ctx->char_confidences.empty()) return 0.0f;
    double sum = 0;
    for (float c : ctx->char_confidences) sum += c;
    return (float)(sum / ctx->char_confidences.size());
}

const float * ppformulanet_ocr_get_encoder_output(const ppformulanet_ocr_context * ctx, int * out_n_tokens,
                                                  int * out_hidden) {
    if (!ctx || ctx->enc_out.empty()) return nullptr;
    if (out_n_tokens) *out_n_tokens = ctx->n_enc_tokens;
    if (out_hidden) *out_hidden = ctx->hparams.enc_hidden;
    return ctx->enc_out.data();
}

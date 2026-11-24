#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ppocrv6_ocr_context ppocrv6_ocr_context;

ppocrv6_ocr_context * ppocrv6_ocr_init(const char * model_path, int n_threads);
void ppocrv6_ocr_free(ppocrv6_ocr_context * ctx);
// Override graph acceptance without mutating the process environment. Zero
// selects the scalar reference; -1 restores environment-controlled acceptance.
// NO_GRAPH and GRAPH_MAX_REGIONS=0 take precedence even over accept=1.
void ppocrv6_ocr_set_graph_accept(ppocrv6_ocr_context * ctx, int accept);
const char * ppocrv6_ocr_recognize_raw(ppocrv6_ocr_context * ctx, const uint8_t * pixels, int width, int height,
                                       int channels, int * out_len);
// Recognize all line crops while retaining caller order. Equal-width crops
// use bounded graph batches; output strings are copied into caller buffers.
// CRISPEMBED_PPOCRV6_GRAPH_MAX_REGIONS caps each batch (default 8, zero disables
// graphs); the legacy BATCH_MAX can further reduce that cap. Graph acceptance
// and backend opt-outs also apply to fused batches, with scalar CPU recovery
// if a fused graph fails. Preprocessed image storage is bounded by one batch.
// Each context shares one resident weight set across at most two execution
// shapes. The extra cached shape is capped at 32 MiB including scheduler
// backend buffers, graph metadata, retained CPU readbacks, and a 2 MiB reserve
// for private scheduler bookkeeping. An oversized active shape runs alone.
// CRISPEMBED_PPOCRV6_GRAPH_CACHE=0 retains only the active execution shape.
int ppocrv6_ocr_recognize_raw_batch(ppocrv6_ocr_context * ctx, const uint8_t * const * pixels, const int * widths,
                                    const int * heights, const int * channels, int count, char * const * outputs,
                                    const int * capacities, int * lengths);
const char * ppocrv6_ocr_recognize(ppocrv6_ocr_context * ctx, const float * pixels, int width, int height,
                                   int * out_len);

#ifdef __cplusplus
}
#endif

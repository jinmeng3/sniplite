#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Load a Marian/OPUS-MT GGUF and prepare an English-to-Chinese translator.
// runtime_dir selects the Vulkan plugin directory when this is the Vulkan build.
// Returns an owned context or nullptr on failure.
void * sniplite_opusmt_init(const char * model_path, const char * runtime_dir, int threads);
// Translate UTF-8 text. Returns the required UTF-8 byte count excluding NUL.
// If output_capacity is positive, output is NUL-terminated when it fits.
// Returns 0 on failure.
int sniplite_opusmt_translate(void * ctx, const char * input_utf8, char * output_utf8, int output_capacity);

// Releases the context returned by sniplite_opusmt_init.
void sniplite_opusmt_free(void * ctx);

#ifdef __cplusplus
}
#endif

#pragma once
#include "common.h"

/* The optional local Marian GGUF module lives beside the program in translation/. */
bool translate_installed(void);
bool translate_text(const WCHAR *text, Str *translated);
void translate_unload(void);

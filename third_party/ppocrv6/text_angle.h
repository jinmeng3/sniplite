#pragma once
#include <stdint.h>

int detect_text_angle(const uint8_t *gray, int width, int height, float *confidence);

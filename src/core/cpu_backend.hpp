#pragma once

#include "types.hpp"
#include <cstdint>

namespace upscaler::cpu {

// Resize functions - read from input, write to output (pre-allocated)
void resize_nearest(const uint8_t* input, uint8_t* output,
                    int in_w, int in_h, int out_w, int out_h, int channels);

void resize_bilinear(const uint8_t* input, uint8_t* output,
                     int in_w, int in_h, int out_w, int out_h, int channels);

void resize_bicubic(const uint8_t* input, uint8_t* output,
                    int in_w, int in_h, int out_w, int out_h, int channels);

// 3x3 convolution sharpening - modifies image in-place
void sharpen_3x3(uint8_t* image, int width, int height, int channels,
                 const float kernel[9]);

} // namespace upscaler::cpu

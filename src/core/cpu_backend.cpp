#include "cpu_backend.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace upscaler::cpu {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static inline int reflect(int v, int size) {
    if (v < 0) v = -v - 1;
    if (v >= size) v = 2 * size - v - 1;
    return std::clamp(v, 0, size - 1);
}

static inline uint8_t clamp_to_u8(float v) {
    return static_cast<uint8_t>(std::clamp(v + 0.5f, 0.0f, 255.0f));
}

// Catmull-Rom (a = -0.5)
static inline float cubic_weight(float t) {
    float at = std::abs(t);
    if (at <= 1.0f)
        return (1.5f * at - 2.5f) * at * at + 1.0f;
    if (at < 2.0f)
        return ((-0.5f * at + 2.5f) * at - 4.0f) * at + 2.0f;
    return 0.0f;
}

// ---------------------------------------------------------------------------
// Nearest-neighbour
// ---------------------------------------------------------------------------
void resize_nearest(const uint8_t* input, uint8_t* output,
                    int in_w, int in_h, int out_w, int out_h, int channels) {
#pragma omp parallel for schedule(static)
    for (int oy = 0; oy < out_h; ++oy) {
        for (int ox = 0; ox < out_w; ++ox) {
            int ix = static_cast<int>((static_cast<float>(ox) + 0.5f) *
                                      in_w / out_w);
            int iy = static_cast<int>((static_cast<float>(oy) + 0.5f) *
                                      in_h / out_h);
            ix = std::clamp(ix, 0, in_w - 1);
            iy = std::clamp(iy, 0, in_h - 1);

            const uint8_t* src =
                input + (static_cast<size_t>(iy) * in_w + ix) * channels;
            uint8_t* dst =
                output + (static_cast<size_t>(oy) * out_w + ox) * channels;
            std::memcpy(dst, src, channels);
        }
    }
}

// ---------------------------------------------------------------------------
// Bilinear
// ---------------------------------------------------------------------------
void resize_bilinear(const uint8_t* input, uint8_t* output,
                     int in_w, int in_h, int out_w, int out_h, int channels) {
#pragma omp parallel for schedule(static)
    for (int oy = 0; oy < out_h; ++oy) {
        for (int ox = 0; ox < out_w; ++ox) {
            float sx = (static_cast<float>(ox) + 0.5f) * in_w / out_w - 0.5f;
            float sy = (static_cast<float>(oy) + 0.5f) * in_h / out_h - 0.5f;

            int x0 = static_cast<int>(std::floor(sx));
            int y0 = static_cast<int>(std::floor(sy));
            float fx = sx - x0;
            float fy = sy - y0;

            int x1 = x0 + 1;
            int y1 = y0 + 1;
            x0 = reflect(x0, in_w);
            x1 = reflect(x1, in_w);
            y0 = reflect(y0, in_h);
            y1 = reflect(y1, in_h);

            uint8_t* dst =
                output + (static_cast<size_t>(oy) * out_w + ox) * channels;
            for (int c = 0; c < channels; ++c) {
                float v00 =
                    input[(static_cast<size_t>(y0) * in_w + x0) * channels + c];
                float v10 =
                    input[(static_cast<size_t>(y0) * in_w + x1) * channels + c];
                float v01 =
                    input[(static_cast<size_t>(y1) * in_w + x0) * channels + c];
                float v11 =
                    input[(static_cast<size_t>(y1) * in_w + x1) * channels + c];

                float val = v00 * (1 - fx) * (1 - fy) +
                            v10 * fx * (1 - fy) +
                            v01 * (1 - fx) * fy +
                            v11 * fx * fy;
                dst[c] = clamp_to_u8(val);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Bicubic (Catmull-Rom)
// ---------------------------------------------------------------------------
void resize_bicubic(const uint8_t* input, uint8_t* output,
                    int in_w, int in_h, int out_w, int out_h, int channels) {
#pragma omp parallel for schedule(static)
    for (int oy = 0; oy < out_h; ++oy) {
        for (int ox = 0; ox < out_w; ++ox) {
            float sx = (static_cast<float>(ox) + 0.5f) * in_w / out_w - 0.5f;
            float sy = (static_cast<float>(oy) + 0.5f) * in_h / out_h - 0.5f;

            int ix = static_cast<int>(std::floor(sx));
            int iy = static_cast<int>(std::floor(sy));
            float fx = sx - ix;
            float fy = sy - iy;

            uint8_t* dst =
                output + (static_cast<size_t>(oy) * out_w + ox) * channels;
            for (int c = 0; c < channels; ++c) {
                float sum = 0.0f;
                for (int dy = -1; dy <= 2; ++dy) {
                    float wy = cubic_weight(fy - dy);
                    int py = reflect(iy + dy, in_h);
                    for (int dx = -1; dx <= 2; ++dx) {
                        float wx = cubic_weight(fx - dx);
                        int px = reflect(ix + dx, in_w);
                        float pixel = input[(static_cast<size_t>(py) * in_w +
                                             px) *
                                                channels +
                                            c];
                        sum += pixel * wx * wy;
                    }
                }
                dst[c] = clamp_to_u8(sum);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// 3x3 Sharpening convolution (in-place via temp buffer)
// ---------------------------------------------------------------------------
void sharpen_3x3(uint8_t* image, int width, int height, int channels,
                 const float kernel[9]) {
    size_t total = static_cast<size_t>(width) * height * channels;
    std::vector<uint8_t> tmp(image, image + total); // copy source

#pragma omp parallel for schedule(static)
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            for (int c = 0; c < channels; ++c) {
                float sum = 0.0f;
                for (int ky = -1; ky <= 1; ++ky) {
                    int py = reflect(y + ky, height);
                    for (int kx = -1; kx <= 1; ++kx) {
                        int px = reflect(x + kx, width);
                        float pixel =
                            tmp[(static_cast<size_t>(py) * width + px) *
                                    channels +
                                c];
                        float w = kernel[(ky + 1) * 3 + (kx + 1)];
                        sum += pixel * w;
                    }
                }
                image[(static_cast<size_t>(y) * width + x) * channels + c] =
                    clamp_to_u8(sum);
            }
        }
    }
}

} // namespace upscaler::cpu

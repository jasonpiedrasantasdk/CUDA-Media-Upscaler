#pragma once
#ifdef HAS_CUDA

#include <cuda_runtime.h>
#include <cstdint>

namespace upscaler::cuda {

// Resize kernels - operate on device pointers
void resize_nearest(const uint8_t* d_input, uint8_t* d_output,
                    int in_w, int in_h, int out_w, int out_h, int channels,
                    cudaStream_t stream = nullptr);

void resize_bilinear(const uint8_t* d_input, uint8_t* d_output,
                     int in_w, int in_h, int out_w, int out_h, int channels,
                     cudaStream_t stream = nullptr);

void resize_bicubic(const uint8_t* d_input, uint8_t* d_output,
                    int in_w, int in_h, int out_w, int out_h, int channels,
                    cudaStream_t stream = nullptr);

// Sharpen: applies 3x3 convolution kernel
// d_kernel_3x3 must be a device pointer to 9 floats
void sharpen(uint8_t* d_image, int width, int height, int channels,
             const float* d_kernel_3x3, cudaStream_t stream = nullptr);

} // namespace upscaler::cuda

#endif // HAS_CUDA

#ifdef HAS_CUDA

#include "cuda_kernels.cuh"

#include <cstdio>
#include <stdexcept>
#include <string>

// ---------------------------------------------------------------------------
// Error-checking macro
// ---------------------------------------------------------------------------
#define CUDA_CHECK(call)                                                       \
    do {                                                                        \
        cudaError_t err_ = (call);                                             \
        if (err_ != cudaSuccess) {                                             \
            throw std::runtime_error(                                          \
                std::string("CUDA error at ") + __FILE__ + ":" +               \
                std::to_string(__LINE__) + " — " +                             \
                cudaGetErrorString(err_));                                      \
        }                                                                      \
    } while (0)

namespace upscaler::cuda {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static constexpr int kBlockDim = 16;

static inline dim3 grid_for(int w, int h) {
    return dim3(static_cast<unsigned>((w + kBlockDim - 1) / kBlockDim),
                static_cast<unsigned>((h + kBlockDim - 1) / kBlockDim));
}

static inline dim3 block_dim() { return dim3(kBlockDim, kBlockDim); }

// Reflect boundary: maps coord into [0, size-1]
__device__ __forceinline__ int reflect(int v, int size) {
    if (v < 0) v = -v - 1;
    if (v >= size) v = 2 * size - v - 1;
    // Clamp for safety after double reflection on very small images
    if (v < 0) v = 0;
    if (v >= size) v = size - 1;
    return v;
}

// Catmull-Rom basis (a = -0.5)
__device__ __forceinline__ float cubic_weight(float t) {
    float at = fabsf(t);
    if (at <= 1.0f)
        return (1.5f * at - 2.5f) * at * at + 1.0f;
    if (at < 2.0f)
        return ((-0.5f * at + 2.5f) * at - 4.0f) * at + 2.0f;
    return 0.0f;
}

// ---------------------------------------------------------------------------
// Nearest-neighbour resize
// ---------------------------------------------------------------------------
__global__ void resize_nearest_kernel(const uint8_t* __restrict__ input,
                                      uint8_t* __restrict__ output,
                                      int in_w, int in_h,
                                      int out_w, int out_h, int channels) {
    int ox = blockIdx.x * blockDim.x + threadIdx.x;
    int oy = blockIdx.y * blockDim.y + threadIdx.y;
    if (ox >= out_w || oy >= out_h) return;

    int ix = static_cast<int>(((float)ox + 0.5f) * in_w / out_w);
    int iy = static_cast<int>(((float)oy + 0.5f) * in_h / out_h);
    ix = min(max(ix, 0), in_w - 1);
    iy = min(max(iy, 0), in_h - 1);

    size_t src_idx = ((size_t)iy * in_w + ix) * channels;
    size_t dst_idx = ((size_t)oy * out_w + ox) * channels;
    for (int c = 0; c < channels; ++c)
        output[dst_idx + c] = input[src_idx + c];
}

void resize_nearest(const uint8_t* d_input, uint8_t* d_output,
                    int in_w, int in_h, int out_w, int out_h, int channels,
                    cudaStream_t stream) {
    resize_nearest_kernel<<<grid_for(out_w, out_h), block_dim(), 0, stream>>>(
        d_input, d_output, in_w, in_h, out_w, out_h, channels);
    CUDA_CHECK(cudaGetLastError());
}

// ---------------------------------------------------------------------------
// Bilinear resize
// ---------------------------------------------------------------------------
__global__ void resize_bilinear_kernel(const uint8_t* __restrict__ input,
                                       uint8_t* __restrict__ output,
                                       int in_w, int in_h,
                                       int out_w, int out_h, int channels) {
    int ox = blockIdx.x * blockDim.x + threadIdx.x;
    int oy = blockIdx.y * blockDim.y + threadIdx.y;
    if (ox >= out_w || oy >= out_h) return;

    float sx = ((float)ox + 0.5f) * in_w / out_w - 0.5f;
    float sy = ((float)oy + 0.5f) * in_h / out_h - 0.5f;

    int x0 = (int)floorf(sx);
    int y0 = (int)floorf(sy);
    float fx = sx - x0;
    float fy = sy - y0;

    int x1 = x0 + 1;
    int y1 = y0 + 1;

    x0 = reflect(x0, in_w);
    x1 = reflect(x1, in_w);
    y0 = reflect(y0, in_h);
    y1 = reflect(y1, in_h);

    size_t dst_idx = ((size_t)oy * out_w + ox) * channels;
    for (int c = 0; c < channels; ++c) {
        float v00 = input[((size_t)y0 * in_w + x0) * channels + c];
        float v10 = input[((size_t)y0 * in_w + x1) * channels + c];
        float v01 = input[((size_t)y1 * in_w + x0) * channels + c];
        float v11 = input[((size_t)y1 * in_w + x1) * channels + c];

        float val = v00 * (1 - fx) * (1 - fy) +
                    v10 * fx * (1 - fy) +
                    v01 * (1 - fx) * fy +
                    v11 * fx * fy;
        output[dst_idx + c] = static_cast<uint8_t>(
            fminf(fmaxf(val + 0.5f, 0.0f), 255.0f));
    }
}

void resize_bilinear(const uint8_t* d_input, uint8_t* d_output,
                     int in_w, int in_h, int out_w, int out_h, int channels,
                     cudaStream_t stream) {
    resize_bilinear_kernel<<<grid_for(out_w, out_h), block_dim(), 0, stream>>>(
        d_input, d_output, in_w, in_h, out_w, out_h, channels);
    CUDA_CHECK(cudaGetLastError());
}

// ---------------------------------------------------------------------------
// Bicubic resize (Catmull-Rom)
// ---------------------------------------------------------------------------
__global__ void resize_bicubic_kernel(const uint8_t* __restrict__ input,
                                      uint8_t* __restrict__ output,
                                      int in_w, int in_h,
                                      int out_w, int out_h, int channels) {
    int ox = blockIdx.x * blockDim.x + threadIdx.x;
    int oy = blockIdx.y * blockDim.y + threadIdx.y;
    if (ox >= out_w || oy >= out_h) return;

    float sx = ((float)ox + 0.5f) * in_w / out_w - 0.5f;
    float sy = ((float)oy + 0.5f) * in_h / out_h - 0.5f;

    int ix = (int)floorf(sx);
    int iy = (int)floorf(sy);
    float fx = sx - ix;
    float fy = sy - iy;

    size_t dst_idx = ((size_t)oy * out_w + ox) * channels;
    for (int c = 0; c < channels; ++c) {
        float sum = 0.0f;
        for (int dy = -1; dy <= 2; ++dy) {
            float wy = cubic_weight(fy - dy);
            int py = reflect(iy + dy, in_h);
            for (int dx = -1; dx <= 2; ++dx) {
                float wx = cubic_weight(fx - dx);
                int px = reflect(ix + dx, in_w);
                float pixel = input[((size_t)py * in_w + px) * channels + c];
                sum += pixel * wx * wy;
            }
        }
        output[dst_idx + c] = static_cast<uint8_t>(
            fminf(fmaxf(sum + 0.5f, 0.0f), 255.0f));
    }
}

void resize_bicubic(const uint8_t* d_input, uint8_t* d_output,
                    int in_w, int in_h, int out_w, int out_h, int channels,
                    cudaStream_t stream) {
    resize_bicubic_kernel<<<grid_for(out_w, out_h), block_dim(), 0, stream>>>(
        d_input, d_output, in_w, in_h, out_w, out_h, channels);
    CUDA_CHECK(cudaGetLastError());
}

// ---------------------------------------------------------------------------
// 3x3 Sharpening convolution
// ---------------------------------------------------------------------------
__global__ void sharpen_kernel(const uint8_t* __restrict__ input,
                               uint8_t* __restrict__ output,
                               int width, int height, int channels,
                               const float* __restrict__ kernel_3x3) {
    int ox = blockIdx.x * blockDim.x + threadIdx.x;
    int oy = blockIdx.y * blockDim.y + threadIdx.y;
    if (ox >= width || oy >= height) return;

    size_t dst_idx = ((size_t)oy * width + ox) * channels;
    for (int c = 0; c < channels; ++c) {
        float sum = 0.0f;
        for (int ky = -1; ky <= 1; ++ky) {
            int py = reflect(oy + ky, height);
            for (int kx = -1; kx <= 1; ++kx) {
                int px = reflect(ox + kx, width);
                float pixel = input[((size_t)py * width + px) * channels + c];
                float w = kernel_3x3[(ky + 1) * 3 + (kx + 1)];
                sum += pixel * w;
            }
        }
        output[dst_idx + c] = static_cast<uint8_t>(
            fminf(fmaxf(sum + 0.5f, 0.0f), 255.0f));
    }
}

void sharpen(uint8_t* d_image, int width, int height, int channels,
             const float* d_kernel_3x3, cudaStream_t stream) {
    // We need a scratch buffer because the kernel reads from the original
    size_t bytes = static_cast<size_t>(width) * height * channels;
    uint8_t* d_tmp = nullptr;
    CUDA_CHECK(cudaMalloc(&d_tmp, bytes));

    // Copy current image to tmp as source
    CUDA_CHECK(cudaMemcpyAsync(d_tmp, d_image, bytes,
                                cudaMemcpyDeviceToDevice, stream));

    sharpen_kernel<<<grid_for(width, height), block_dim(), 0, stream>>>(
        d_tmp, d_image, width, height, channels, d_kernel_3x3);
    CUDA_CHECK(cudaGetLastError());

    // Synchronise before freeing tmp
    CUDA_CHECK(cudaStreamSynchronize(stream));
    CUDA_CHECK(cudaFree(d_tmp));
}

} // namespace upscaler::cuda

#endif // HAS_CUDA

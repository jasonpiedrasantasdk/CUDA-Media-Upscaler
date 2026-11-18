#include "upscaler.hpp"
#include "cpu_backend.hpp"

#ifdef HAS_CUDA
#include "cuda_kernels.cuh"
#include <cuda_runtime.h>
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace upscaler {

// ---------------------------------------------------------------------------
// Sharpen kernel data (row-major 3x3)
// ---------------------------------------------------------------------------
static constexpr float kSharpenLight[9] = {
    0.0f, -0.5f, 0.0f,
    -0.5f, 3.0f, -0.5f,
    0.0f, -0.5f, 0.0f};

static constexpr float kSharpenMedium[9] = {
    0.0f, -1.0f, 0.0f,
    -1.0f, 5.0f, -1.0f,
    0.0f, -1.0f, 0.0f};

static constexpr float kSharpenStrong[9] = {
    -1.0f, -1.0f, -1.0f,
    -1.0f, 9.0f, -1.0f,
    -1.0f, -1.0f, -1.0f};

static const float* sharpen_kernel_for(SharpenLevel level) {
    switch (level) {
    case SharpenLevel::Light: return kSharpenLight;
    case SharpenLevel::Medium: return kSharpenMedium;
    case SharpenLevel::Strong: return kSharpenStrong;
    default: return nullptr;
    }
}

static const char* interpolation_name(Interpolation interp) {
    switch (interp) {
    case Interpolation::Nearest: return "Nearest";
    case Interpolation::Bilinear: return "Bilinear";
    case Interpolation::Bicubic: return "Bicubic";
    default: return "Unknown";
    }
}

static const char* sharpen_name(SharpenLevel level) {
    switch (level) {
    case SharpenLevel::None: return "None";
    case SharpenLevel::Light: return "Light";
    case SharpenLevel::Medium: return "Medium";
    case SharpenLevel::Strong: return "Strong";
    default: return "Unknown";
    }
}

// ---------------------------------------------------------------------------
// CUDA helpers (only when HAS_CUDA is defined)
// ---------------------------------------------------------------------------
#ifdef HAS_CUDA

#define CUDA_CHECK(call)                                                       \
    do {                                                                        \
        cudaError_t err_ = (call);                                             \
        if (err_ != cudaSuccess) {                                             \
            throw std::runtime_error(                                          \
                std::string("CUDA error: ") + cudaGetErrorString(err_));       \
        }                                                                      \
    } while (0)

// RAII wrapper for device memory
struct CudaDeleter {
    void operator()(void* p) const noexcept {
        if (p) cudaFree(p);
    }
};

template <typename T>
using CudaPtr = std::unique_ptr<T, CudaDeleter>;

template <typename T>
[[nodiscard]] CudaPtr<T> cuda_alloc(size_t bytes) {
    T* ptr = nullptr;
    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&ptr), bytes));
    return CudaPtr<T>(ptr);
}

#endif // HAS_CUDA

// ---------------------------------------------------------------------------
// Construction / GPU init
// ---------------------------------------------------------------------------
Upscaler::Upscaler() { init_cuda(); }

Upscaler::~Upscaler() {
#ifdef HAS_CUDA
    if (m_cuda_available) {
        cudaDeviceReset();
    }
#endif
}

void Upscaler::init_cuda() {
#ifdef HAS_CUDA
    int device_count = 0;
    cudaError_t err = cudaGetDeviceCount(&device_count);
    if (err == cudaSuccess && device_count > 0) {
        err = cudaSetDevice(0);
        if (err == cudaSuccess) {
            m_cuda_available = true;
            auto info = get_gpu_info();
            std::cerr << "[upscaler] CUDA GPU detected: " << info.name
                      << " (" << info.free_gb << " / " << info.total_gb
                      << " GB)\n";
        }
    }
    if (!m_cuda_available) {
        std::cerr << "[upscaler] No CUDA GPU available, using CPU backend\n";
    }
#else
    std::cerr << "[upscaler] Built without CUDA support, using CPU backend\n";
#endif
}

bool Upscaler::cuda_available() const noexcept { return m_cuda_available; }

GpuInfo Upscaler::get_gpu_info() const {
    GpuInfo info;
#ifdef HAS_CUDA
    if (m_cuda_available) {
        cudaDeviceProp prop{};
        if (cudaGetDeviceProperties(&prop, 0) == cudaSuccess) {
            info.name = prop.name;
            info.available = true;
        }
        size_t free_bytes = 0, total_bytes = 0;
        if (cudaMemGetInfo(&free_bytes, &total_bytes) == cudaSuccess) {
            info.free_gb =
                static_cast<float>(free_bytes) / (1024.0f * 1024.0f * 1024.0f);
            info.total_gb = static_cast<float>(total_bytes) /
                            (1024.0f * 1024.0f * 1024.0f);
        }
    }
#endif
    return info;
}

// ---------------------------------------------------------------------------
// GPU upscale path
// ---------------------------------------------------------------------------
#ifdef HAS_CUDA

static ImageBuffer upscale_gpu(const ImageBuffer& input, int out_w, int out_h,
                               Interpolation interp, SharpenLevel sharpen_lvl) {
    const size_t in_bytes = input.total_bytes();
    const size_t out_bytes =
        static_cast<size_t>(out_w) * out_h * input.channels;

    // Allocate device memory (RAII)
    auto d_input = cuda_alloc<uint8_t>(in_bytes);
    auto d_output = cuda_alloc<uint8_t>(out_bytes);

    // Upload input
    CUDA_CHECK(cudaMemcpy(d_input.get(), input.data.data(), in_bytes,
                           cudaMemcpyHostToDevice));

    // Dispatch resize
    switch (interp) {
    case Interpolation::Nearest:
        cuda::resize_nearest(d_input.get(), d_output.get(), input.width,
                             input.height, out_w, out_h, input.channels);
        break;
    case Interpolation::Bilinear:
        cuda::resize_bilinear(d_input.get(), d_output.get(), input.width,
                              input.height, out_w, out_h, input.channels);
        break;
    case Interpolation::Bicubic:
        cuda::resize_bicubic(d_input.get(), d_output.get(), input.width,
                             input.height, out_w, out_h, input.channels);
        break;
    }

    CUDA_CHECK(cudaDeviceSynchronize());

    // Apply sharpening on device if requested
    const float* h_kernel = sharpen_kernel_for(sharpen_lvl);
    if (h_kernel) {
        auto d_kernel = cuda_alloc<float>(9 * sizeof(float));
        CUDA_CHECK(cudaMemcpy(d_kernel.get(), h_kernel, 9 * sizeof(float),
                               cudaMemcpyHostToDevice));
        cuda::sharpen(d_output.get(), out_w, out_h, input.channels,
                      d_kernel.get());
        CUDA_CHECK(cudaDeviceSynchronize());
        // d_kernel freed by RAII
    }

    // Download result
    ImageBuffer result = ImageBuffer::create(out_w, out_h, input.channels);
    CUDA_CHECK(cudaMemcpy(result.data.data(), d_output.get(), out_bytes,
                           cudaMemcpyDeviceToHost));

    // d_input, d_output freed by RAII destructors here
    return result;
}

#endif // HAS_CUDA

// ---------------------------------------------------------------------------
// CPU upscale path
// ---------------------------------------------------------------------------
static ImageBuffer upscale_cpu(const ImageBuffer& input, int out_w, int out_h,
                               Interpolation interp,
                               SharpenLevel sharpen_lvl) {
    ImageBuffer result = ImageBuffer::create(out_w, out_h, input.channels);

    switch (interp) {
    case Interpolation::Nearest:
        cpu::resize_nearest(input.data.data(), result.data.data(), input.width,
                            input.height, out_w, out_h, input.channels);
        break;
    case Interpolation::Bilinear:
        cpu::resize_bilinear(input.data.data(), result.data.data(), input.width,
                             input.height, out_w, out_h, input.channels);
        break;
    case Interpolation::Bicubic:
        cpu::resize_bicubic(input.data.data(), result.data.data(), input.width,
                            input.height, out_w, out_h, input.channels);
        break;
    }

    const float* kernel = sharpen_kernel_for(sharpen_lvl);
    if (kernel) {
        cpu::sharpen_3x3(result.data.data(), out_w, out_h, input.channels,
                         kernel);
    }

    return result;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
ImageBuffer Upscaler::upscale(const ImageBuffer& input,
                              const UpscaleSettings& settings) {
    if (input.empty()) {
        throw std::invalid_argument("upscale(): input image is empty");
    }
    if (settings.scale < 0.01f || settings.scale > 8.0f) {
        throw std::invalid_argument(
            "upscale(): scale must be in [0.01, 8.0], got " +
            std::to_string(settings.scale));
    }

    int out_w =
        std::max(1, static_cast<int>(std::round(input.width * settings.scale)));
    int out_h = std::max(
        1, static_cast<int>(std::round(input.height * settings.scale)));

    std::cerr << "[upscaler] " << input.width << "x" << input.height << " -> "
              << out_w << "x" << out_h << " (scale=" << settings.scale
              << ", interp=" << interpolation_name(settings.interpolation)
              << ", sharpen=" << sharpen_name(settings.sharpen) << ")\n";

    auto t0 = std::chrono::steady_clock::now();

#ifdef HAS_CUDA
    if (m_cuda_available) {
        try {
            auto result = upscale_gpu(input, out_w, out_h,
                                      settings.interpolation, settings.sharpen);
            auto t1 = std::chrono::steady_clock::now();
            double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
            std::cerr << "[upscaler] GPU upscale completed in " << ms
                      << " ms\n";

            // Clean up GPU memory pool
            cudaDeviceSynchronize();

            return result;
        } catch (const std::runtime_error& e) {
            std::cerr << "[upscaler] GPU failed (" << e.what()
                      << "), falling back to CPU\n";
            // Reset device state after error
            cudaDeviceReset();
            m_cuda_available = false;
        }
    }
#endif

    auto result = upscale_cpu(input, out_w, out_h, settings.interpolation,
                              settings.sharpen);
    auto t1 = std::chrono::steady_clock::now();
    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    std::cerr << "[upscaler] CPU upscale completed in " << ms << " ms\n";

    return result;
}

} // namespace upscaler

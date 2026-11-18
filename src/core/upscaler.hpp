#pragma once

#include "types.hpp"

namespace upscaler {

class Upscaler {
public:
    Upscaler();
    ~Upscaler();

    // Non-copyable, movable
    Upscaler(const Upscaler&) = delete;
    Upscaler& operator=(const Upscaler&) = delete;
    Upscaler(Upscaler&&) noexcept = default;
    Upscaler& operator=(Upscaler&&) noexcept = default;

    /// Check if CUDA GPU is usable
    [[nodiscard]] bool cuda_available() const noexcept;

    /// Query GPU name, free/total VRAM
    [[nodiscard]] GpuInfo get_gpu_info() const;

    /// Scale an image according to settings. Returns a new ImageBuffer.
    /// Tries GPU first; falls back to CPU on failure.
    [[nodiscard]] ImageBuffer upscale(const ImageBuffer& input,
                                      const UpscaleSettings& settings);

private:
    bool m_cuda_available = false;
    void init_cuda();
};

} // namespace upscaler

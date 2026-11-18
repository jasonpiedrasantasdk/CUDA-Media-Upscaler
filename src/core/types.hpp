#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace upscaler {

enum class Interpolation { Nearest = 0, Bilinear = 1, Bicubic = 3 };
enum class SharpenLevel { None, Light, Medium, Strong };

struct UpscaleSettings {
    float scale = 2.0f;
    Interpolation interpolation = Interpolation::Bicubic;
    SharpenLevel sharpen = SharpenLevel::None;
};

struct GpuInfo {
    std::string name;
    float free_gb = 0.0f;
    float total_gb = 0.0f;
    bool available = false;
};

// Simple image buffer - owns its data, row-major RGBA/RGB
struct ImageBuffer {
    std::vector<uint8_t> data;
    int width = 0;
    int height = 0;
    int channels = 0; // 3=RGB, 4=RGBA

    [[nodiscard]] size_t row_stride() const noexcept {
        return static_cast<size_t>(width) * channels;
    }

    [[nodiscard]] size_t total_bytes() const noexcept { return data.size(); }

    [[nodiscard]] bool empty() const noexcept { return data.empty(); }

    [[nodiscard]] static ImageBuffer create(int w, int h, int ch) {
        ImageBuffer buf;
        buf.width = w;
        buf.height = h;
        buf.channels = ch;
        buf.data.resize(static_cast<size_t>(w) * h * ch);
        return buf;
    }
};

using ProgressCallback =
    std::function<void(const std::string& status, int percent)>;

} // namespace upscaler

#pragma once

#include "../core/types.hpp"

#include <atomic>
#include <filesystem>
#include <string>
#include <vector>

namespace upscaler {

/// Settings for GIF output quality and optimization.
struct GifSettings {
    int fps            = 15;
    int colors         = 256;
    std::string dither = "sierra2_4a";
    int lossy          = 0;           // gifsicle --lossy 0-200
    int optimize_level = 3;           // gifsicle -O level 1-3
};

/// A single decoded GIF frame with its display duration.
struct GifFrame {
    ImageBuffer image;
    int duration_ms = 100;
};

/// GIF processing layer that shells out to ffmpeg and (optionally) gifsicle.
class GifProcessor {
public:
    /// Return true if gifsicle is reachable on PATH.
    static bool check_gifsicle();

    /// Extract every frame from a GIF, returning them as RGBA ImageBuffers
    /// together with per-frame duration.
    static std::vector<GifFrame> extract_frames(const std::filesystem::path& path);

    /// Assemble a GIF from numbered PNG frames already written to frames_dir.
    /// Uses ffmpeg two-pass palettegen, then optionally gifsicle for optimization.
    static void assemble_gif_from_frames(
        const std::filesystem::path& frames_dir,
        const std::filesystem::path& output_path,
        int frame_duration_ms,
        const GifSettings& settings);

    /// Convert a video file to a GIF using ffmpeg two-pass palettegen.
    static void video_to_gif(
        const std::filesystem::path& video_path,
        const std::filesystem::path& output_path,
        const GifSettings& settings,
        int width = -1,
        ProgressCallback progress = nullptr,
        const std::atomic<bool>* cancel_flag = nullptr);

    /// Optimize an existing GIF with gifsicle.
    static void optimize_with_gifsicle(
        const std::filesystem::path& input,
        const std::filesystem::path& output,
        const GifSettings& settings);
};

} // namespace upscaler

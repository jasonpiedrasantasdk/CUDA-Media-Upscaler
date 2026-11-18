#pragma once

#include "../core/types.hpp"

#include <atomic>
#include <filesystem>
#include <functional>
#include <string>
#include <utility>

namespace upscaler {

/// Metadata extracted from a video file via ffprobe.
struct VideoInfo {
    int width          = 0;
    int height         = 0;
    double fps         = 30.0;
    int64_t frame_count = 0;
    bool has_audio     = false;
    std::string codec;
};

/// Video processing layer that shells out to ffmpeg / ffprobe.
class VideoProcessor {
public:
    /// Return true if both ffmpeg and ffprobe are reachable.
    static bool check_ffmpeg();

    /// Probe a video file and return its metadata.
    static VideoInfo probe(const std::filesystem::path& path);

    /// Extract every frame as a numbered PNG into output_dir.
    /// @returns (frame_count, fps).
    static std::pair<int, double> extract_frames(
        const std::filesystem::path& video_path,
        const std::filesystem::path& output_dir,
        const std::atomic<bool>* cancel_flag = nullptr);

    /// Extract frames with a custom video filter string (e.g. "fps=15").
    /// @returns the number of extracted frames.
    static int extract_frames_filtered(
        const std::filesystem::path& video_path,
        const std::filesystem::path& output_dir,
        const std::string& vf_filter,
        const std::atomic<bool>* cancel_flag = nullptr);

    /// Assemble numbered PNG frames back into a video file.
    /// If the original video contains audio, it is copied into the output.
    static void assemble_video(
        const std::filesystem::path& frames_dir,
        const std::filesystem::path& output_path,
        double fps,
        const std::filesystem::path& original_video,
        const std::string& codec = "libx264",
        int crf = 18);
};

} // namespace upscaler

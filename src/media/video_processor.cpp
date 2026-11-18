#include "video_processor.hpp"
#include "subprocess.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <filesystem>
#include <ranges>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace upscaler {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Count .png files in a directory.
static int count_png_files(const fs::path& dir) {
    int count = 0;
    if (!fs::exists(dir)) return 0;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".png") {
            ++count;
        }
    }
    return count;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool VideoProcessor::check_ffmpeg() {
    return is_executable_available("ffmpeg") &&
           is_executable_available("ffprobe");
}

VideoInfo VideoProcessor::probe(const fs::path& path) {
    if (!fs::exists(path)) {
        throw std::runtime_error("VideoProcessor::probe: file not found: " + path.string());
    }

    auto result = run_command({
        "ffprobe",
        "-v", "quiet",
        "-print_format", "json",
        "-show_format",
        "-show_streams",
        path.string()
    }, 30000);

    if (result.exit_code != 0) {
        throw std::runtime_error(
            "VideoProcessor::probe: ffprobe failed: " + result.stderr_str);
    }

    // Parse the JSON output.
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(
        QByteArray::fromStdString(result.stdout_str), &err);
    if (doc.isNull()) {
        throw std::runtime_error(
            "VideoProcessor::probe: JSON parse error: " +
            err.errorString().toStdString());
    }

    QJsonObject root = doc.object();
    QJsonArray streams = root["streams"].toArray();

    VideoInfo info{};

    // Walk streams looking for video and audio.
    for (const auto& s : streams) {
        QJsonObject stream = s.toObject();
        std::string codec_type = stream["codec_type"].toString().toStdString();

        if (codec_type == "video" && info.width == 0) {
            info.width  = stream["width"].toInt();
            info.height = stream["height"].toInt();
            info.codec  = stream["codec_name"].toString().toStdString();

            // Frame count: try nb_frames, then estimate from duration * fps.
            if (stream.contains("nb_frames")) {
                info.frame_count = stream["nb_frames"].toString().toLongLong();
            }

            // FPS from r_frame_rate (e.g. "30000/1001").
            std::string rfr = stream["r_frame_rate"].toString().toStdString();
            if (!rfr.empty()) {
                auto slash = rfr.find('/');
                if (slash != std::string::npos) {
                    double num = std::stod(rfr.substr(0, slash));
                    double den = std::stod(rfr.substr(slash + 1));
                    if (den > 0) info.fps = num / den;
                } else {
                    info.fps = std::stod(rfr);
                }
            }

            // Fallback frame count from duration.
            if (info.frame_count == 0) {
                double duration = 0.0;
                if (stream.contains("duration")) {
                    duration = stream["duration"].toString().toDouble();
                } else {
                    QJsonObject fmt = root["format"].toObject();
                    if (fmt.contains("duration")) {
                        duration = fmt["duration"].toString().toDouble();
                    }
                }
                if (duration > 0 && info.fps > 0) {
                    info.frame_count = static_cast<int64_t>(duration * info.fps);
                }
            }
        } else if (codec_type == "audio") {
            info.has_audio = true;
        }
    }

    return info;
}

std::pair<int, double> VideoProcessor::extract_frames(
        const fs::path& video_path,
        const fs::path& output_dir,
        const std::atomic<bool>* cancel_flag) {
    if (!fs::exists(video_path)) {
        throw std::runtime_error(
            "VideoProcessor::extract_frames: file not found: " + video_path.string());
    }

    fs::create_directories(output_dir);

    if (cancel_flag && cancel_flag->load()) {
        throw std::runtime_error("Operation cancelled");
    }

    VideoInfo info = probe(video_path);

    log_info("Extracting frames from: " + video_path.filename().string() +
             " (" + std::to_string(info.width) + "x" +
             std::to_string(info.height) + " @ " +
             std::to_string(info.fps) + " fps)");

    std::string output_pattern = (output_dir / "%06d.png").string();

    auto result = run_command({
        "ffmpeg",
        "-i", video_path.string(),
        "-qscale:v", "2",
        "-vsync", "0",
        output_pattern,
        "-y"
    }, 600000); // 10-minute timeout for long videos

    if (cancel_flag && cancel_flag->load()) {
        throw std::runtime_error("Operation cancelled");
    }

    if (result.exit_code != 0) {
        throw std::runtime_error(
            "VideoProcessor::extract_frames: ffmpeg failed: " + result.stderr_str);
    }

    int frame_count = count_png_files(output_dir);
    log_info("Extracted " + std::to_string(frame_count) + " frames");

    return {frame_count, info.fps};
}

int VideoProcessor::extract_frames_filtered(
        const fs::path& video_path,
        const fs::path& output_dir,
        const std::string& vf_filter,
        const std::atomic<bool>* cancel_flag) {
    if (!fs::exists(video_path)) {
        throw std::runtime_error(
            "VideoProcessor::extract_frames_filtered: file not found: " +
            video_path.string());
    }

    fs::create_directories(output_dir);

    if (cancel_flag && cancel_flag->load()) {
        throw std::runtime_error("Operation cancelled");
    }

    std::string output_pattern = (output_dir / "%06d.png").string();

    std::vector<std::string> cmd = {
        "ffmpeg",
        "-i", video_path.string(),
        "-vf", vf_filter,
        "-qscale:v", "2",
        "-vsync", "0",
        output_pattern,
        "-y"
    };

    auto result = run_command(cmd, 600000);

    if (cancel_flag && cancel_flag->load()) {
        throw std::runtime_error("Operation cancelled");
    }

    if (result.exit_code != 0) {
        throw std::runtime_error(
            "VideoProcessor::extract_frames_filtered: ffmpeg failed: " +
            result.stderr_str);
    }

    int frame_count = count_png_files(output_dir);
    log_info("Extracted " + std::to_string(frame_count) +
             " frames (filter: " + vf_filter + ")");
    return frame_count;
}

void VideoProcessor::assemble_video(
        const fs::path& frames_dir,
        const fs::path& output_path,
        double fps,
        const fs::path& original_video,
        const std::string& codec,
        int crf) {
    if (count_png_files(frames_dir) == 0) {
        throw std::runtime_error(
            "VideoProcessor::assemble_video: no PNG frames in " + frames_dir.string());
    }

    // Ensure output directory exists.
    if (output_path.has_parent_path()) {
        fs::create_directories(output_path.parent_path());
    }

    std::string fps_str = std::to_string(fps);
    std::string crf_str = std::to_string(crf);
    std::string input_pattern = (frames_dir / "%06d.png").string();

    // Determine output extension to pick encoder settings.
    std::string ext = output_path.extension().string();

    // Check for audio in original.
    bool has_audio = false;
    if (!original_video.empty() && fs::exists(original_video)) {
        try {
            VideoInfo orig_info = probe(original_video);
            has_audio = orig_info.has_audio;
        } catch (...) {
            // Non-fatal; proceed without audio.
        }
    }

    std::vector<std::string> cmd;

    if (ext == ".webm") {
        // VP9 encoding.
        cmd = {
            "ffmpeg",
            "-y",
            "-framerate", fps_str,
            "-i", input_pattern
        };
        if (has_audio) {
            cmd.insert(cmd.end(), {"-i", original_video.string()});
        }
        cmd.insert(cmd.end(), {
            "-c:v", "libvpx-vp9",
            "-crf", crf_str,
            "-b:v", "0",
            "-pix_fmt", "yuva420p"
        });
        if (has_audio) {
            cmd.insert(cmd.end(), {"-c:a", "libopus", "-map", "0:v", "-map", "1:a"});
        }
        cmd.push_back(output_path.string());
    } else {
        // MP4 / MKV with libx264 (or user-specified codec).
        cmd = {
            "ffmpeg",
            "-y",
            "-framerate", fps_str,
            "-i", input_pattern
        };
        if (has_audio) {
            cmd.insert(cmd.end(), {"-i", original_video.string()});
        }
        cmd.insert(cmd.end(), {
            "-c:v", codec,
            "-crf", crf_str,
            "-pix_fmt", "yuv420p"
        });
        if (has_audio) {
            cmd.insert(cmd.end(), {"-c:a", "aac", "-map", "0:v", "-map", "1:a"});
        }
        cmd.push_back(output_path.string());
    }

    log_info("Assembling video: " + output_path.filename().string() +
             " (codec=" + codec + ", crf=" + crf_str + ", fps=" + fps_str + ")");

    auto result = run_command(cmd, 600000);

    if (result.exit_code != 0) {
        throw std::runtime_error(
            "VideoProcessor::assemble_video: ffmpeg failed: " + result.stderr_str);
    }

    if (!fs::exists(output_path)) {
        throw std::runtime_error(
            "VideoProcessor::assemble_video: output file was not created");
    }

    log_info("Video assembled successfully: " + output_path.string());
}

} // namespace upscaler

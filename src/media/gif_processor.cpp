#include "gif_processor.hpp"
#include "image_io.hpp"
#include "subprocess.hpp"
#include "temp_dir.hpp"

#include <QImage>
#include <QMovie>
#include <QBuffer>
#include <QByteArray>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <ranges>
#include <stdexcept>

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

bool GifProcessor::check_gifsicle() {
    return is_executable_available("gifsicle");
}

std::vector<GifFrame> GifProcessor::extract_frames(const fs::path& path) {
    if (!fs::exists(path)) {
        throw std::runtime_error(
            "GifProcessor::extract_frames: file not found: " + path.string());
    }

    std::vector<GifFrame> frames;

    QMovie movie(QString::fromStdString(path.string()));
    if (!movie.isValid()) {
        throw std::runtime_error(
            "GifProcessor::extract_frames: failed to open GIF: " + path.string());
    }

    // Jump to the first frame.
    movie.jumpToFrame(0);

    int total_frames = movie.frameCount();
    if (total_frames <= 0) {
        // Some GIFs don't report a frame count; iterate until we run out.
        total_frames = 10000; // upper safety limit
    }

    for (int i = 0; i < total_frames; ++i) {
        QImage img = movie.currentImage();
        if (img.isNull()) break;

        GifFrame gf;
        gf.image       = ImageIO::from_qimage(img);
        gf.duration_ms = movie.nextFrameDelay();
        if (gf.duration_ms <= 0) gf.duration_ms = 100;

        frames.push_back(std::move(gf));

        if (!movie.jumpToNextFrame()) break;
    }

    log_info("Extracted " + std::to_string(frames.size()) +
             " frames from GIF: " + path.filename().string());
    return frames;
}

void GifProcessor::assemble_gif_from_frames(
        const fs::path& frames_dir,
        const fs::path& output_path,
        int frame_duration_ms,
        const GifSettings& settings) {
    int n_frames = count_png_files(frames_dir);
    if (n_frames == 0) {
        throw std::runtime_error(
            "GifProcessor::assemble_gif_from_frames: no PNG frames in " +
            frames_dir.string());
    }

    // Derive FPS from frame duration if the caller provided one;
    // otherwise fall back to settings.fps.
    double fps = (frame_duration_ms > 0)
        ? (1000.0 / frame_duration_ms)
        : static_cast<double>(settings.fps);

    std::string fps_str       = std::to_string(fps);
    std::string colors_str    = std::to_string(settings.colors);
    std::string input_pattern = (frames_dir / "%06d.png").string();

    // Ensure output directory exists.
    if (output_path.has_parent_path()) {
        fs::create_directories(output_path.parent_path());
    }

    // We need a temporary palette file.
    TempDir tmp("gifpal_");
    fs::path palette = tmp.path() / "palette.png";

    // Pass 1: generate palette.
    {
        std::string vf = "palettegen=max_colors=" + colors_str + ":stats_mode=diff";
        auto r = run_command({
            "ffmpeg", "-y",
            "-framerate", fps_str,
            "-i", input_pattern,
            "-vf", vf,
            "-update", "1",
            palette.string()
        }, 300000);

        if (r.exit_code != 0) {
            throw std::runtime_error(
                "GifProcessor::assemble_gif_from_frames: palettegen failed: " +
                r.stderr_str);
        }
    }

    // Temporary unoptimized output (in case gifsicle will post-process).
    fs::path raw_output = output_path;
    bool will_optimize = check_gifsicle() && settings.optimize_level > 0;
    fs::path opt_input;
    if (will_optimize) {
        opt_input  = tmp.path() / "raw.gif";
        raw_output = opt_input;
    }

    // Pass 2: apply palette.
    {
        std::string lavfi = "[0:v][1:v] paletteuse=dither=" + settings.dither;
        auto r = run_command({
            "ffmpeg", "-y",
            "-framerate", fps_str,
            "-i", input_pattern,
            "-i", palette.string(),
            "-lavfi", lavfi,
            raw_output.string()
        }, 300000);

        if (r.exit_code != 0) {
            throw std::runtime_error(
                "GifProcessor::assemble_gif_from_frames: paletteuse failed: " +
                r.stderr_str);
        }
    }

    // Gifsicle optimization.
    if (will_optimize) {
        optimize_with_gifsicle(opt_input, output_path, settings);
    }

    log_info("GIF assembled: " + output_path.string() +
             " (" + std::to_string(n_frames) + " frames)");
}

void GifProcessor::video_to_gif(
        const fs::path& video_path,
        const fs::path& output_path,
        const GifSettings& settings,
        int width,
        ProgressCallback progress,
        const std::atomic<bool>* cancel_flag) {
    if (!fs::exists(video_path)) {
        throw std::runtime_error(
            "GifProcessor::video_to_gif: file not found: " + video_path.string());
    }

    if (cancel_flag && cancel_flag->load()) {
        throw std::runtime_error("Operation cancelled");
    }

    if (progress) progress("Generating palette...", 10);

    std::string fps_str    = std::to_string(settings.fps);
    std::string colors_str = std::to_string(settings.colors);

    // Build the video filter chain for scaling + fps.
    std::string vf_base;
    if (width > 0) {
        vf_base = "fps=" + fps_str + ",scale=" + std::to_string(width) + ":-1:flags=lanczos";
    } else {
        vf_base = "fps=" + fps_str;
    }

    // Ensure output parent exists.
    if (output_path.has_parent_path()) {
        fs::create_directories(output_path.parent_path());
    }

    TempDir tmp("v2gif_");
    fs::path palette = tmp.path() / "palette.png";

    // Pass 1: palettegen.
    {
        std::string vf = vf_base + ",palettegen=max_colors=" + colors_str + ":stats_mode=diff";
        auto r = run_command({
            "ffmpeg", "-y",
            "-i", video_path.string(),
            "-vf", vf,
            "-update", "1",
            palette.string()
        }, 300000);

        if (r.exit_code != 0) {
            throw std::runtime_error(
                "GifProcessor::video_to_gif: palettegen failed: " + r.stderr_str);
        }
    }

    if (cancel_flag && cancel_flag->load()) {
        throw std::runtime_error("Operation cancelled");
    }

    if (progress) progress("Encoding GIF...", 50);

    // Determine whether we'll gifsicle post-process.
    bool will_optimize = check_gifsicle() && settings.optimize_level > 0;
    fs::path raw_output = output_path;
    fs::path opt_input;
    if (will_optimize) {
        opt_input  = tmp.path() / "raw.gif";
        raw_output = opt_input;
    }

    // Pass 2: paletteuse.
    {
        std::string lavfi =
            vf_base + " [v]; [v][1:v] paletteuse=dither=" + settings.dither;
        auto r = run_command({
            "ffmpeg", "-y",
            "-i", video_path.string(),
            "-i", palette.string(),
            "-lavfi", lavfi,
            raw_output.string()
        }, 600000);

        if (r.exit_code != 0) {
            throw std::runtime_error(
                "GifProcessor::video_to_gif: paletteuse failed: " + r.stderr_str);
        }
    }

    if (cancel_flag && cancel_flag->load()) {
        throw std::runtime_error("Operation cancelled");
    }

    if (will_optimize) {
        if (progress) progress("Optimizing with gifsicle...", 80);
        optimize_with_gifsicle(opt_input, output_path, settings);
    }

    if (progress) progress("Done", 100);

    log_info("Video converted to GIF: " + output_path.string());
}

void GifProcessor::optimize_with_gifsicle(
        const fs::path& input,
        const fs::path& output,
        const GifSettings& settings) {
    if (!check_gifsicle()) {
        // If gifsicle is not available, just copy the file.
        if (input != output) {
            fs::copy_file(input, output, fs::copy_options::overwrite_existing);
        }
        return;
    }

    std::string opt_flag = "-O" + std::to_string(settings.optimize_level);

    std::vector<std::string> cmd = {
        "gifsicle",
        opt_flag
    };

    if (settings.lossy > 0) {
        cmd.push_back("--lossy=" + std::to_string(settings.lossy));
    }

    if (settings.colors > 0 && settings.colors < 256) {
        cmd.push_back("--colors=" + std::to_string(settings.colors));
    }

    cmd.insert(cmd.end(), {"-o", output.string(), input.string()});

    auto r = run_command(cmd, 120000);

    if (r.exit_code != 0) {
        log_warn("gifsicle optimization failed (non-fatal): " + r.stderr_str);
        // Fall back to copying the unoptimized file.
        if (input != output) {
            fs::copy_file(input, output, fs::copy_options::overwrite_existing);
        }
    } else {
        log_info("gifsicle optimization complete: " + output.string());
    }
}

} // namespace upscaler

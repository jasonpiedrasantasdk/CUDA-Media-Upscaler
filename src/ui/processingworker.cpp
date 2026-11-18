#include "processingworker.hpp"

#include "../core/upscaler.hpp"
#include "../media/image_io.hpp"
#include "../media/video_processor.hpp"
#include "../media/gif_processor.hpp"
#include "../media/temp_dir.hpp"

#include <QImage>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <format>
#include <ranges>

namespace fs = std::filesystem;

namespace upscaler {

ProcessingWorker::ProcessingWorker(Upscaler* engine, QObject* parent)
    : QObject(parent)
    , m_engine(engine)
{
}

void ProcessingWorker::emitLog(const std::string& msg, const char* level)
{
    emit logMessage(QString::fromStdString(msg), QString::fromLatin1(level));
}

// ---------------------------------------------------------------------------
// Image processing
// ---------------------------------------------------------------------------

void ProcessingWorker::processImage(const QString& inputPath,
                                    const QString& outputPath,
                                    UpscaleSettings settings)
{
    try {
        auto t0 = std::chrono::steady_clock::now();

        emitLog("Loading image: " + inputPath.toStdString());
        emit progressUpdated("Loading image...", 10, "");

        auto image = ImageIO::load(fs::path(inputPath.toStdString()));
        emitLog(std::format("Loaded {}x{} ({}ch)", image.width, image.height, image.channels));

        if (is_cancelled()) {
            emit finished(false, "Cancelled");
            return;
        }

        emit progressUpdated("Upscaling...", 30, "");
        emitLog(std::format("Upscaling with scale={:.1f}, interp={}, sharpen={}",
                            settings.scale,
                            static_cast<int>(settings.interpolation),
                            static_cast<int>(settings.sharpen)));

        auto result = m_engine->upscale(image, settings);
        emitLog(std::format("Result: {}x{}", result.width, result.height));

        if (is_cancelled()) {
            emit finished(false, "Cancelled");
            return;
        }

        emit progressUpdated("Saving...", 80, "");
        ImageIO::save(result, fs::path(outputPath.toStdString()));

        auto dt = std::chrono::steady_clock::now() - t0;
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(dt).count();
        emitLog(std::format("Image saved to {} ({:.2f}s)", outputPath.toStdString(), ms / 1000.0));

        emit progressUpdated("Complete", 100, "");
        emit finished(true, QString("Image saved to %1").arg(outputPath));

    } catch (const std::exception& e) {
        emitLog(std::string("Error: ") + e.what(), "ERROR");
        emit finished(false, QString::fromStdString(e.what()));
    }
}

// ---------------------------------------------------------------------------
// Video processing
// ---------------------------------------------------------------------------

void ProcessingWorker::processVideo(const QString& inputPath,
                                    const QString& outputPath,
                                    UpscaleSettings settings,
                                    const QString& codec,
                                    int crf)
{
    try {
        auto t0 = std::chrono::steady_clock::now();

        emitLog("Checking ffmpeg availability...");
        if (!VideoProcessor::check_ffmpeg()) {
            emit finished(false, "ffmpeg/ffprobe not found. Please install ffmpeg.");
            return;
        }

        fs::path inPath(inputPath.toStdString());
        fs::path outPath(outputPath.toStdString());

        // Probe video
        emitLog("Probing video: " + inputPath.toStdString());
        emit progressUpdated("Probing video...", 5, "");
        auto info = VideoProcessor::probe(inPath);
        emitLog(std::format("Video: {}x{} @ {:.2f} fps, ~{} frames, codec={}",
                            info.width, info.height, info.fps,
                            info.frame_count, info.codec));

        if (is_cancelled()) { emit finished(false, "Cancelled"); return; }

        // Create temp directory for frames
        TempDir tmpDir("upscaler_vid_");
        auto extractDir = tmpDir.subdir("extracted");
        auto scaledDir = tmpDir.subdir("scaled");

        // Extract frames
        emitLog("Extracting frames...");
        emit progressUpdated("Extracting frames...", 10, "");

        auto [frameCount, fps] = VideoProcessor::extract_frames(inPath, extractDir, &m_cancelled);

        if (is_cancelled()) { emit finished(false, "Cancelled"); return; }

        emitLog(std::format("Extracted {} frames at {:.2f} fps", frameCount, fps));

        // Upscale each frame
        emitLog(std::format("Upscaling {} frames...", frameCount), "HEADER");

        for (int i = 1; i <= frameCount; ++i) {
            if (is_cancelled()) { emit finished(false, "Cancelled"); return; }

            auto frameName = std::format("frame_{:06d}.png", i);
            auto srcFrame = extractDir / frameName;
            auto dstFrame = scaledDir / frameName;

            if (!fs::exists(srcFrame)) continue;

            auto frameImg = ImageIO::load(srcFrame);
            auto scaled = m_engine->upscale(frameImg, settings);
            ImageIO::save(scaled, dstFrame);

            int pct = 10 + static_cast<int>(80.0 * i / frameCount);
            auto frameInfo = std::format("{}/{}", i, frameCount);
            emit progressUpdated("Upscaling frames...", pct, QString::fromStdString(frameInfo));

            if (i % 10 == 0 || i == frameCount) {
                emitLog(std::format("Frame {}/{} ({}x{} -> {}x{})",
                                    i, frameCount,
                                    frameImg.width, frameImg.height,
                                    scaled.width, scaled.height));
            }
        }

        if (is_cancelled()) { emit finished(false, "Cancelled"); return; }

        // Reassemble video
        emitLog("Assembling output video...");
        emit progressUpdated("Assembling video...", 92, "");

        VideoProcessor::assemble_video(scaledDir, outPath, fps, inPath,
                                       codec.toStdString(), crf);

        auto dt = std::chrono::steady_clock::now() - t0;
        auto secs = std::chrono::duration_cast<std::chrono::milliseconds>(dt).count() / 1000.0;
        emitLog(std::format("Video saved to {} ({:.1f}s)", outPath.string(), secs));

        emit progressUpdated("Complete", 100, "");
        emit finished(true, QString("Video saved to %1").arg(outputPath));

    } catch (const std::exception& e) {
        emitLog(std::string("Error: ") + e.what(), "ERROR");
        emit finished(false, QString::fromStdString(e.what()));
    }
}

// ---------------------------------------------------------------------------
// GIF processing
// ---------------------------------------------------------------------------

void ProcessingWorker::processGif(const QString& inputPath,
                                  const QString& outputPath,
                                  UpscaleSettings settings,
                                  GifSettings gifSettings)
{
    try {
        auto t0 = std::chrono::steady_clock::now();

        fs::path inPath(inputPath.toStdString());
        fs::path outPath(outputPath.toStdString());

        // Extract GIF frames
        emitLog("Extracting GIF frames...");
        emit progressUpdated("Extracting frames...", 5, "");

        auto frames = GifProcessor::extract_frames(inPath);
        int frameCount = static_cast<int>(frames.size());
        emitLog(std::format("Extracted {} frames from GIF", frameCount));

        if (is_cancelled()) { emit finished(false, "Cancelled"); return; }

        // Create temp directory for upscaled frames
        TempDir tmpDir("upscaler_gif_");
        auto scaledDir = tmpDir.subdir("scaled");

        int avgDuration = 100;
        if (!frames.empty()) {
            int totalDuration = 0;
            for (const auto& f : frames) totalDuration += f.duration_ms;
            avgDuration = totalDuration / frameCount;
        }

        // Upscale each frame
        emitLog(std::format("Upscaling {} GIF frames...", frameCount), "HEADER");

        for (int i = 0; i < frameCount; ++i) {
            if (is_cancelled()) { emit finished(false, "Cancelled"); return; }

            auto scaled = m_engine->upscale(frames[i].image, settings);

            auto framePath = scaledDir / std::format("frame_{:06d}.png", i + 1);
            ImageIO::save(scaled, framePath);

            int pct = 5 + static_cast<int>(80.0 * (i + 1) / frameCount);
            auto frameInfo = std::format("{}/{}", i + 1, frameCount);
            emit progressUpdated("Upscaling frames...", pct, QString::fromStdString(frameInfo));

            if ((i + 1) % 10 == 0 || i + 1 == frameCount) {
                emitLog(std::format("Frame {}/{}", i + 1, frameCount));
            }
        }

        if (is_cancelled()) { emit finished(false, "Cancelled"); return; }

        // Assemble GIF
        emitLog("Assembling GIF...");
        emit progressUpdated("Assembling GIF...", 90, "");

        GifProcessor::assemble_gif_from_frames(scaledDir, outPath, avgDuration, gifSettings);

        // Optionally optimize with gifsicle
        if (GifProcessor::check_gifsicle() && (gifSettings.lossy > 0 || gifSettings.optimize_level > 0)) {
            emitLog("Optimizing with gifsicle...");
            emit progressUpdated("Optimizing GIF...", 95, "");

            auto optimized = outPath;
            optimized.replace_extension(".opt.gif");
            GifProcessor::optimize_with_gifsicle(outPath, optimized, gifSettings);

            // Replace original with optimized
            fs::rename(optimized, outPath);
        }

        auto dt = std::chrono::steady_clock::now() - t0;
        auto secs = std::chrono::duration_cast<std::chrono::milliseconds>(dt).count() / 1000.0;
        emitLog(std::format("GIF saved to {} ({:.1f}s)", outPath.string(), secs));

        emit progressUpdated("Complete", 100, "");
        emit finished(true, QString("GIF saved to %1").arg(outputPath));

    } catch (const std::exception& e) {
        emitLog(std::string("Error: ") + e.what(), "ERROR");
        emit finished(false, QString::fromStdString(e.what()));
    }
}

// ---------------------------------------------------------------------------
// Video to GIF
// ---------------------------------------------------------------------------

void ProcessingWorker::processVideoToGif(const QString& inputPath,
                                         const QString& outputPath,
                                         UpscaleSettings settings,
                                         GifSettings gifSettings,
                                         int fps,
                                         int width [[maybe_unused]])
{
    try {
        auto t0 = std::chrono::steady_clock::now();

        fs::path inPath(inputPath.toStdString());
        fs::path outPath(outputPath.toStdString());

        emitLog("Checking ffmpeg availability...");
        if (!VideoProcessor::check_ffmpeg()) {
            emit finished(false, "ffmpeg/ffprobe not found. Please install ffmpeg.");
            return;
        }

        // Probe video for info
        emitLog("Probing video...");
        emit progressUpdated("Probing video...", 5, "");
        auto info = VideoProcessor::probe(inPath);
        emitLog(std::format("Source: {}x{} @ {:.2f} fps", info.width, info.height, info.fps));

        if (is_cancelled()) { emit finished(false, "Cancelled"); return; }

        // Create temp dir
        TempDir tmpDir("upscaler_v2g_");
        auto extractDir = tmpDir.subdir("extracted");
        auto scaledDir = tmpDir.subdir("scaled");

        // Extract frames with FPS filter
        auto vfFilter = std::format("fps={}", fps);
        emitLog(std::format("Extracting frames with filter: {}", vfFilter));
        emit progressUpdated("Extracting frames...", 10, "");

        int frameCount = VideoProcessor::extract_frames_filtered(
            inPath, extractDir, vfFilter, &m_cancelled);

        if (is_cancelled()) { emit finished(false, "Cancelled"); return; }

        emitLog(std::format("Extracted {} frames at {} fps", frameCount, fps));

        // Upscale each frame
        emitLog(std::format("Upscaling {} frames...", frameCount), "HEADER");

        for (int i = 1; i <= frameCount; ++i) {
            if (is_cancelled()) { emit finished(false, "Cancelled"); return; }

            auto frameName = std::format("frame_{:06d}.png", i);
            auto srcFrame = extractDir / frameName;
            auto dstFrame = scaledDir / frameName;

            if (!fs::exists(srcFrame)) continue;

            auto frameImg = ImageIO::load(srcFrame);
            auto scaled = m_engine->upscale(frameImg, settings);
            ImageIO::save(scaled, dstFrame);

            int pct = 10 + static_cast<int>(75.0 * i / frameCount);
            auto frameInfo = std::format("{}/{}", i, frameCount);
            emit progressUpdated("Upscaling frames...", pct, QString::fromStdString(frameInfo));

            if (i % 10 == 0 || i == frameCount) {
                emitLog(std::format("Frame {}/{}", i, frameCount));
            }
        }

        if (is_cancelled()) { emit finished(false, "Cancelled"); return; }

        // Assemble GIF from scaled frames
        int frameDurationMs = 1000 / fps;
        emitLog("Assembling GIF...");
        emit progressUpdated("Assembling GIF...", 88, "");

        GifProcessor::assemble_gif_from_frames(scaledDir, outPath, frameDurationMs, gifSettings);

        // Optimize with gifsicle if available
        if (GifProcessor::check_gifsicle() && (gifSettings.lossy > 0 || gifSettings.optimize_level > 0)) {
            emitLog("Optimizing with gifsicle...");
            emit progressUpdated("Optimizing GIF...", 95, "");

            auto optimized = outPath;
            optimized.replace_extension(".opt.gif");
            GifProcessor::optimize_with_gifsicle(outPath, optimized, gifSettings);
            fs::rename(optimized, outPath);
        }

        auto dt = std::chrono::steady_clock::now() - t0;
        auto secs = std::chrono::duration_cast<std::chrono::milliseconds>(dt).count() / 1000.0;
        emitLog(std::format("GIF saved to {} ({:.1f}s)", outPath.string(), secs));

        emit progressUpdated("Complete", 100, "");
        emit finished(true, QString("GIF saved to %1").arg(outputPath));

    } catch (const std::exception& e) {
        emitLog(std::string("Error: ") + e.what(), "ERROR");
        emit finished(false, QString::fromStdString(e.what()));
    }
}

// ---------------------------------------------------------------------------
// GIF to Video
// ---------------------------------------------------------------------------

void ProcessingWorker::processGifToVideo(const QString& inputPath,
                                         const QString& outputPath,
                                         UpscaleSettings settings,
                                         const QString& codec,
                                         int crf,
                                         int fps)
{
    try {
        auto t0 = std::chrono::steady_clock::now();

        emitLog("Checking ffmpeg availability...");
        if (!VideoProcessor::check_ffmpeg()) {
            emit finished(false, "ffmpeg/ffprobe not found. Please install ffmpeg.");
            return;
        }

        fs::path inPath(inputPath.toStdString());
        fs::path outPath(outputPath.toStdString());

        // Extract GIF frames
        emitLog("Extracting GIF frames...");
        emit progressUpdated("Extracting frames...", 5, "");

        auto frames = GifProcessor::extract_frames(inPath);
        int frameCount = static_cast<int>(frames.size());
        emitLog(std::format("Extracted {} frames from GIF", frameCount));

        if (is_cancelled()) { emit finished(false, "Cancelled"); return; }

        // Create temp directory for upscaled frames
        TempDir tmpDir("upscaler_g2v_");
        auto scaledDir = tmpDir.subdir("scaled");

        // Upscale each frame
        emitLog(std::format("Upscaling {} GIF frames...", frameCount), "HEADER");

        for (int i = 0; i < frameCount; ++i) {
            if (is_cancelled()) { emit finished(false, "Cancelled"); return; }

            auto scaled = m_engine->upscale(frames[i].image, settings);

            auto framePath = scaledDir / std::format("frame_{:06d}.png", i + 1);
            ImageIO::save(scaled, framePath);

            int pct = 5 + static_cast<int>(80.0 * (i + 1) / frameCount);
            auto frameInfo = std::format("{}/{}", i + 1, frameCount);
            emit progressUpdated("Upscaling frames...", pct, QString::fromStdString(frameInfo));

            if ((i + 1) % 10 == 0 || i + 1 == frameCount) {
                emitLog(std::format("Frame {}/{}", i + 1, frameCount));
            }
        }

        if (is_cancelled()) { emit finished(false, "Cancelled"); return; }

        // Assemble video from upscaled frames
        emitLog("Assembling video...");
        emit progressUpdated("Assembling video...", 90, "");

        // Use caller-specified FPS, or derive from GIF frame duration
        double outputFps = static_cast<double>(fps);
        if (outputFps <= 0 && !frames.empty()) {
            int totalDuration = 0;
            for (const auto& f : frames) totalDuration += f.duration_ms;
            outputFps = 1000.0 * frameCount / totalDuration;
        }
        if (outputFps <= 0) outputFps = 15.0;

        // Rename frames to sequential numbering ffmpeg expects (%06d.png starting at 000001)
        for (int i = 0; i < frameCount; ++i) {
            auto src = scaledDir / std::format("frame_{:06d}.png", i + 1);
            auto dst = scaledDir / std::format("{:06d}.png", i + 1);
            if (fs::exists(src)) fs::rename(src, dst);
        }

        VideoProcessor::assemble_video(scaledDir, outPath, outputFps, fs::path{},
                                       codec.toStdString(), crf);

        auto dt = std::chrono::steady_clock::now() - t0;
        auto secs = std::chrono::duration_cast<std::chrono::milliseconds>(dt).count() / 1000.0;
        emitLog(std::format("Video saved to {} ({:.1f}s)", outPath.string(), secs));

        emit progressUpdated("Complete", 100, "");
        emit finished(true, QString("Video saved to %1").arg(outputPath));

    } catch (const std::exception& e) {
        emitLog(std::string("Error: ") + e.what(), "ERROR");
        emit finished(false, QString::fromStdString(e.what()));
    }
}

} // namespace upscaler

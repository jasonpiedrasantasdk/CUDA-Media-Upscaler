#pragma once

#include <QObject>
#include <QString>
#include <atomic>

#include "../core/types.hpp"
#include "../media/gif_processor.hpp"

namespace upscaler {

class Upscaler;

/// Worker that performs all heavy processing on a dedicated QThread.
/// Communicates results back to the UI via signals.
class ProcessingWorker : public QObject {
    Q_OBJECT

public:
    explicit ProcessingWorker(Upscaler* engine, QObject* parent = nullptr);

    void cancel() { m_cancelled.store(true, std::memory_order_release); }
    [[nodiscard]] bool is_cancelled() const { return m_cancelled.load(std::memory_order_acquire); }
    [[nodiscard]] const std::atomic<bool>& cancel_flag() const { return m_cancelled; }

    /// Reset cancel flag before starting new work.
    void reset() { m_cancelled.store(false, std::memory_order_release); }

public slots:
    /// Upscale/downscale a single image file.
    void processImage(const QString& inputPath, const QString& outputPath,
                      UpscaleSettings settings);

    /// Extract frames from video, upscale each, and reassemble.
    void processVideo(const QString& inputPath, const QString& outputPath,
                      UpscaleSettings settings, const QString& codec, int crf);

    /// Extract GIF frames, upscale each, and reassemble as GIF.
    void processGif(const QString& inputPath, const QString& outputPath,
                    UpscaleSettings settings, GifSettings gifSettings);

    /// Convert a video file to an upscaled GIF.
    void processVideoToGif(const QString& inputPath, const QString& outputPath,
                           UpscaleSettings settings, GifSettings gifSettings,
                           int fps, int width);

    /// Convert a GIF to an upscaled video file.
    void processGifToVideo(const QString& inputPath, const QString& outputPath,
                           UpscaleSettings settings, const QString& codec,
                           int crf, int fps);

signals:
    void progressUpdated(const QString& status, int percent, const QString& frameInfo);
    void logMessage(const QString& message, const QString& level);
    void finished(bool success, const QString& message);

private:
    Upscaler* m_engine;
    std::atomic<bool> m_cancelled{false};

    void emitLog(const std::string& msg, const char* level = "INFO");
};

} // namespace upscaler

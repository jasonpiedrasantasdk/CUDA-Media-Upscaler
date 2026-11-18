#pragma once

#include "../core/types.hpp"

#include <filesystem>
#include <string>

class QImage;

namespace upscaler {

/// Image loading and saving utilities backed by Qt6's QImage.
class ImageIO {
public:
    /// Load an image from disk into an RGBA ImageBuffer.
    static ImageBuffer load(const std::filesystem::path& path);

    /// Save an ImageBuffer to disk. Format is auto-detected from the file extension.
    static void save(const ImageBuffer& image,
                     const std::filesystem::path& path,
                     int quality = 95);

    /// Convert a QImage into an RGBA ImageBuffer.
    static ImageBuffer from_qimage(const QImage& img);

    /// Convert an ImageBuffer into a QImage (RGBA or RGB depending on channels).
    static QImage to_qimage(const ImageBuffer& buf);
};

} // namespace upscaler

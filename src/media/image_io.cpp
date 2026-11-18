#include "image_io.hpp"

#include <QImage>

#include <iostream>
#include <stdexcept>

namespace upscaler {

ImageBuffer ImageIO::load(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        throw std::runtime_error("ImageIO::load: file not found: " + path.string());
    }

    QImage img(QString::fromStdString(path.string()));
    if (img.isNull()) {
        throw std::runtime_error("ImageIO::load: failed to load image: " + path.string());
    }

    return from_qimage(img);
}

void ImageIO::save(const ImageBuffer& image,
                   const std::filesystem::path& path,
                   int quality) {
    if (image.empty()) {
        throw std::runtime_error("ImageIO::save: empty image buffer");
    }

    QImage qimg = to_qimage(image);

    // Determine format from extension (Qt auto-detects from filename, but we
    // can be explicit for clarity).
    std::string ext = path.extension().string();
    const char* format = nullptr;
    if (ext == ".png")       format = "PNG";
    else if (ext == ".jpg" || ext == ".jpeg") format = "JPEG";
    else if (ext == ".bmp")  format = "BMP";
    else if (ext == ".tiff" || ext == ".tif") format = "TIFF";
    else if (ext == ".webp") format = "WEBP";
    // nullptr lets Qt guess from the filename

    // Ensure parent directory exists.
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }

    if (!qimg.save(QString::fromStdString(path.string()), format, quality)) {
        throw std::runtime_error("ImageIO::save: failed to write image: " + path.string());
    }
}

ImageBuffer ImageIO::from_qimage(const QImage& img) {
    // Convert to RGBA8888 so the pixel layout is always predictable.
    QImage rgba = img.convertToFormat(QImage::Format_RGBA8888);

    ImageBuffer buf;
    buf.width    = rgba.width();
    buf.height   = rgba.height();
    buf.channels = 4; // RGBA

    const size_t row_bytes = static_cast<size_t>(buf.width) * buf.channels;
    const size_t total     = row_bytes * buf.height;
    buf.data.resize(total);

    // QImage rows may be padded (bytesPerLine >= row_bytes), so copy row by row.
    for (int y = 0; y < buf.height; ++y) {
        const uint8_t* src = rgba.constScanLine(y);
        uint8_t* dst       = buf.data.data() + y * row_bytes;
        std::memcpy(dst, src, row_bytes);
    }

    return buf;
}

QImage ImageIO::to_qimage(const ImageBuffer& buf) {
    if (buf.empty()) {
        return {};
    }

    QImage::Format fmt;
    switch (buf.channels) {
        case 4:  fmt = QImage::Format_RGBA8888; break;
        case 3:  fmt = QImage::Format_RGB888;   break;
        default:
            throw std::runtime_error(
                "ImageIO::to_qimage: unsupported channel count: " +
                std::to_string(buf.channels));
    }

    const int stride = buf.width * buf.channels;

    // Construct a QImage that references the buffer data, then deep-copy it so
    // the returned QImage owns its memory independently.
    QImage ref(buf.data.data(), buf.width, buf.height, stride, fmt);
    return ref.copy(); // deep copy
}

} // namespace upscaler

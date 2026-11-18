#include <QApplication>

#include "ui/mainwindow.hpp"
#include "ui/theme.hpp"
#include "core/upscaler.hpp"
#include "core/cpu_backend.hpp"

#include <cstdint>
#include <iostream>
#include <string>

namespace {

constexpr const char* kVersion = "1.0.0";

/// Build a small deterministic RGBA test image.
upscaler::ImageBuffer make_test_image(int w, int h) {
    auto img = upscaler::ImageBuffer::create(w, h, 4);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const size_t i = (static_cast<size_t>(y) * w + x) * 4;
            img.data[i + 0] = static_cast<uint8_t>(x * 32);
            img.data[i + 1] = static_cast<uint8_t>(y * 32);
            img.data[i + 2] = static_cast<uint8_t>((x + y) * 16);
            img.data[i + 3] = 255;
        }
    }
    return img;
}

/// CPU-only smoke test used by CI (`cuda-media-upscaler --self-test`).
/// Does not require a GPU, a display, or any external tools.
int run_self_test() {
    using namespace upscaler;

    int failures = 0;
    auto check = [&failures](bool ok, const char* what) {
        std::cout << (ok ? "[PASS] " : "[FAIL] ") << what << '\n';
        if (!ok) ++failures;
    };

    auto input = make_test_image(8, 8);
    check(input.width == 8 && input.height == 8 && input.channels == 4,
          "test image allocation");

    // --- Nearest-neighbour: output pixel (0,0) must equal input pixel (0,0).
    auto nearest = ImageBuffer::create(16, 16, 4);
    cpu::resize_nearest(input.data.data(), nearest.data.data(),
                        8, 8, 16, 16, 4);
    check(nearest.data[0] == input.data[0] &&
          nearest.data[1] == input.data[1] &&
          nearest.data[2] == input.data[2] &&
          nearest.data[3] == input.data[3],
          "cpu nearest 2x corner pixel");

    // --- Bilinear: alpha must stay opaque, dimensions must match.
    auto bilinear = ImageBuffer::create(16, 16, 4);
    cpu::resize_bilinear(input.data.data(), bilinear.data.data(),
                         8, 8, 16, 16, 4);
    check(bilinear.data[3] == 255 && bilinear.data[bilinear.data.size() - 1] == 255,
          "cpu bilinear alpha preserved");

    // --- Bicubic: dimensions and opaque alpha.
    auto bicubic = ImageBuffer::create(24, 24, 4);
    cpu::resize_bicubic(input.data.data(), bicubic.data.data(),
                        8, 8, 24, 24, 4);
    check(bicubic.width == 24 && bicubic.height == 24 &&
          bicubic.data[3] == 255,
          "cpu bicubic 3x");

    // --- Sharpening: in-place 3x3 convolution keeps the buffer size.
    static constexpr float kLight[9] = {
        0.0f, -0.5f, 0.0f,
        -0.5f, 3.0f, -0.5f,
        0.0f, -0.5f, 0.0f};
    auto sharpened = ImageBuffer::create(8, 8, 4);
    sharpened.data = input.data;
    cpu::sharpen_3x3(sharpened.data.data(), 8, 8, 4, kLight);
    check(sharpened.data.size() == input.data.size(),
          "cpu sharpen keeps buffer size");

    // --- Engine round-trip (GPU if present, CPU fallback otherwise).
    Upscaler engine;
    UpscaleSettings settings;
    settings.interpolation = Interpolation::Bilinear;

    settings.scale = 2.0f;
    auto up = engine.upscale(input, settings);
    check(up.width == 16 && up.height == 16, "engine 2x upscale");

    settings.scale = 0.5f;
    auto down = engine.upscale(input, settings);
    check(down.width == 4 && down.height == 4, "engine 0.5x downscale");

    std::cout << (failures == 0 ? "SELF-TEST PASSED" : "SELF-TEST FAILED")
              << " (" << failures << " failure(s))\n";
    return failures == 0 ? 0 : 1;
}

void print_usage(const char* argv0) {
    std::cout
        << "Usage: " << argv0 << " [options]\n\n"
        << "GUI application for upscaling images, videos and GIFs.\n\n"
        << "Options:\n"
        << "  --self-test   Run the CPU smoke test and exit (no display needed)\n"
        << "  --version     Print the version and exit\n"
        << "  --help        Show this help and exit\n";
}

} // namespace

int main(int argc, char* argv[])
{
    // Handle CLI flags before creating QApplication so that --self-test works
    // on headless CI machines without a display server.
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--self-test") {
            return run_self_test();
        }
        if (arg == "--version") {
            std::cout << "cuda-media-upscaler " << kVersion << '\n';
            return 0;
        }
        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        }
    }

    QApplication app(argc, argv);
    app.setApplicationName("CUDA Media Upscaler");
    app.setApplicationVersion(kVersion);

    // Apply dark theme globally
    app.setStyleSheet(upscaler::Theme::stylesheet());

    // Log startup info
    std::cerr << std::string(60, '=') << std::endl;
    std::cerr << "CUDA Media Upscaler (C++ Native) starting..." << std::endl;

    upscaler::Upscaler engine;
    auto gpu = engine.get_gpu_info();
    if (gpu.available) {
        std::cerr << "GPU: " << gpu.name
                  << " (" << gpu.total_gb << " GB total, "
                  << gpu.free_gb << " GB free)" << std::endl;
    } else {
        std::cerr << "GPU: Not available - CPU fallback mode" << std::endl;
    }
    std::cerr << std::string(60, '=') << std::endl;

    upscaler::MainWindow window(&engine);
    window.show();

    return app.exec();
}

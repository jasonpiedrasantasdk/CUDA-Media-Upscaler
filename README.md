# CUDA Media Upscaler

[![Build](https://github.com/jasonpiedrasantasdk/cuda-media-upscaler/actions/workflows/build.yml/badge.svg)](https://github.com/jasonpiedrasantasdk/cuda-media-upscaler/actions/workflows/build.yml)

A native desktop application for upscaling images, videos and GIFs. Built with
C++20 and Qt 6, with CUDA-accelerated kernels and a multi-threaded CPU fallback.

The GPU path is optional: the application builds and runs without a CUDA
toolkit, and automatically falls back to the CPU backend when no GPU is
available.

## Features

- **Image** — upscale/downscale PNG, JPEG, BMP, WebP and TIFF with nearest,
  bilinear or bicubic (Catmull-Rom) resampling, plus optional light/medium/strong
  sharpening.
- **Video** — extract frames with `ffmpeg`, upscale each frame, reassemble the
  video and copy the original audio track.
- **GIF** — decode frames, upscale each one, then rebuild the GIF with an
  `ffmpeg` two-pass palette and optional `gifsicle` optimization.
- **Video → GIF** — resample to a chosen FPS, upscale, generate a palette and
  write an optimized GIF.
- **GIF → Video** — decode the GIF, upscale its frames and encode a video
  (MP4/MKV/WebM/AVI/MOV).
- Target-resolution entry, invert-to-downscale mode, per-tab settings,
  drag-free file selection, live preview, progress/ETA and a cancellation
  button that stops long jobs.

## Requirements

| Dependency | Required | Notes |
|---|---|---|
| CMake | yes | 3.25 or newer |
| C++ compiler | yes | C++20 with `std::format` (GCC 13+, Clang 17+) |
| Qt 6 | yes | Core, Gui, Widgets (developed against 6.9.2) |
| CUDA Toolkit | optional | 12.x or 13.x; detected automatically |
| `ffmpeg` / `ffprobe` | for video and GIF | `sudo apt install ffmpeg` |
| `gifsicle` | optional | GIF size optimization |
| OpenMP | optional | parallel CPU fallback |

The Qt GIF decoder comes from the Qt image-format plugins; on Debian/Ubuntu
install `qt6-image-formats-plugins` if GIF import fails.

## Build

```bash
# Automated (detects CUDA and a local Qt install)
./scripts/build.sh Release

# CPU-only build, no CUDA toolkit required
./scripts/build.sh Release --no-cuda

# Manual CMake
cmake -S . -B build -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DENABLE_CUDA=ON
cmake --build build --parallel
```

Dependencies can be installed with `./scripts/install_deps.sh` (supports
Debian/Ubuntu, Fedora/RHEL, Arch and macOS).

## Run

```bash
./launch.sh
# or directly
./build/cuda-media-upscaler
```

Install system-wide:

```bash
sudo cmake --install build --prefix /usr/local
```

This installs the binary to `bin/` and the desktop entry to
`share/applications/`.

## Command-line options

| Flag | Description |
|---|---|
| `--self-test` | Run the CPU smoke test and exit. Requires no GPU or display. |
| `--version` | Print the version and exit. |
| `--help` | Show usage and exit. |

## Architecture

```
src/
├── core/       # Upscaling engine
│   ├── types.hpp           Image buffers, settings, progress callbacks
│   ├── upscaler.{hpp,cpp}  GPU-first dispatcher with CPU fallback
│   ├── cpu_backend.*       Nearest/bilinear/bicubic + sharpen (OpenMP)
│   └── cuda_kernels.{cu,cuh} CUDA resize and convolution kernels
├── media/      # I/O and external tool wrappers
│   ├── image_io.*          QImage load/save
│   ├── video_processor.*   ffmpeg/ffprobe pipelines
│   ├── gif_processor.*     GIF decode/encode + gifsicle
│   ├── subprocess.hpp      QProcess wrapper (no shell interpolation)
│   └── temp_dir.hpp        RAII temporary directories
└── ui/         # Qt Widgets interface
    ├── mainwindow.*        Five-tab window, preview, log
    ├── processingworker.*  Worker object moved to a QThread
    └── theme.*             Dark stylesheet
```

Heavy work runs on a dedicated `QThread`; the UI stays responsive and
cancellation is handled with an atomic flag.

## Notes

- Without `nvcc`, CMake prints a warning and configures a CPU-only build.
- CUDA kernels are compiled for `sm_75;80;86;89;90`. Adjust
  `CUDA_ARCHITECTURES` in `CMakeLists.txt` for other GPUs.
- The checked-in build directory is not tracked; always build from source.

## License

MIT — see [LICENSE](LICENSE).

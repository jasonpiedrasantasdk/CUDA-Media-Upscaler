#!/usr/bin/env bash
# Install dependencies for CUDA Media Upscaler (C++ version)
# Supports: Ubuntu/Debian, Fedora/RHEL, Arch Linux, macOS
set -euo pipefail

echo "=== CUDA Media Upscaler - Dependency Installer ==="

detect_os() {
    if [[ -f /etc/os-release ]]; then
        . /etc/os-release
        echo "$ID"
    elif [[ "$(uname)" == "Darwin" ]]; then
        echo "macos"
    else
        echo "unknown"
    fi
}

OS=$(detect_os)
echo "Detected OS: $OS"

case "$OS" in
    ubuntu|debian|linuxmint|pop)
        echo "Installing via apt..."
        sudo apt-get update
        sudo apt-get install -y \
            build-essential \
            cmake \
            ninja-build \
            qt6-base-dev \
            libgl1-mesa-dev \
            libvulkan-dev \
            ffmpeg \
            gifsicle
        echo ""
        echo "For CUDA support, install CUDA Toolkit from:"
        echo "  https://developer.nvidia.com/cuda-downloads"
        echo "  Or: sudo apt install nvidia-cuda-toolkit"
        ;;
    fedora|rhel|centos|rocky|alma)
        echo "Installing via dnf..."
        sudo dnf install -y \
            gcc-c++ \
            cmake \
            ninja-build \
            qt6-qtbase-devel \
            mesa-libGL-devel \
            vulkan-headers \
            ffmpeg \
            gifsicle
        ;;
    arch|manjaro|endeavouros)
        echo "Installing via pacman..."
        sudo pacman -S --needed \
            base-devel \
            cmake \
            ninja \
            qt6-base \
            mesa \
            vulkan-headers \
            ffmpeg \
            gifsicle
        ;;
    macos)
        echo "Installing via Homebrew..."
        brew install cmake ninja qt@6 ffmpeg gifsicle
        echo "For CUDA: macOS does not support CUDA. CPU-only mode will be used."
        ;;
    *)
        echo "Unknown OS. Please install manually:"
        echo "  - CMake >= 3.25"
        echo "  - Qt6 (base dev)"
        echo "  - CUDA Toolkit 12.x (optional)"
        echo "  - ffmpeg"
        echo "  - gifsicle (optional)"
        echo "  - ninja-build"
        exit 1
        ;;
esac

echo ""
echo "=== Dependency check ==="
echo -n "CMake: "   && cmake --version 2>/dev/null | head -1 || echo "NOT FOUND"
echo -n "Ninja: "   && ninja --version 2>/dev/null || echo "NOT FOUND"
echo -n "Qt6: "     && pkg-config --modversion Qt6Core 2>/dev/null || echo "(check cmake find_package)"
echo -n "CUDA: "    && nvcc --version 2>/dev/null | tail -1 || echo "NOT FOUND (CPU-only mode)"
echo -n "ffmpeg: "  && ffmpeg -version 2>/dev/null | head -1 || echo "NOT FOUND"
echo -n "gifsicle: " && gifsicle --version 2>/dev/null | head -1 || echo "NOT FOUND (optional)"
echo ""
echo "Done. Run ./scripts/build.sh to build the project."

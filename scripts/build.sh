#!/usr/bin/env bash
# Build script for CUDA Media Upscaler (C++ version)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="${PROJECT_DIR}/build"

# Parse arguments
BUILD_TYPE="${1:-Release}"
ENABLE_CUDA="ON"
QT6_DIR=""

usage() {
    echo "Usage: $0 [Debug|Release|RelWithDebInfo] [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  --no-cuda       Build without CUDA support (CPU-only)"
    echo "  --qt6-dir PATH  Set Qt6 CMake directory"
    echo "  --clean         Clean build directory first"
    echo "  --jobs N        Parallel build jobs (default: nproc)"
    echo "  --help          Show this help"
    echo ""
    echo "Examples:"
    echo "  $0                          # Release build with CUDA"
    echo "  $0 Debug                    # Debug build with CUDA"
    echo "  $0 Release --no-cuda        # Release build, CPU only"
    echo "  $0 Release --qt6-dir ~/Qt/6.9.2/gcc_64/lib/cmake"
    exit "${1:-1}"
}

CLEAN=0
JOBS=$(nproc 2>/dev/null || echo 4)

# Parse remaining args
shift || true
while [[ $# -gt 0 ]]; do
    case "$1" in
        --no-cuda)
            ENABLE_CUDA="OFF"
            shift
            ;;
        --qt6-dir)
            QT6_DIR="$2"
            shift 2
            ;;
        --clean)
            CLEAN=1
            shift
            ;;
        --jobs)
            JOBS="$2"
            shift 2
            ;;
        --help|-h)
            usage 0
            ;;
        *)
            echo "Unknown option: $1"
            usage
            ;;
    esac
done

echo "=== CUDA Media Upscaler - Build ==="
echo "Build type:  ${BUILD_TYPE}"
echo "CUDA:        ${ENABLE_CUDA}"
echo "Jobs:        ${JOBS}"
echo "Build dir:   ${BUILD_DIR}"

# Auto-detect Qt6 if not specified
if [[ -z "$QT6_DIR" ]]; then
    # Check common locations, including the official Qt online installer layout.
    candidates=(
        "${HOME}"/Qt6/*/gcc_64/lib/cmake
        "${HOME}"/Qt/*/gcc_64/lib/cmake
        /usr/lib/x86_64-linux-gnu/cmake
        /usr/lib/cmake
        /opt/Qt6/lib/cmake
    )
    for candidate in "${candidates[@]}"; do
        if [[ -f "${candidate}/Qt6/Qt6Config.cmake" ]]; then
            QT6_DIR="$candidate"
            break
        fi
    done
fi

if [[ -n "$QT6_DIR" ]]; then
    echo "Qt6 dir:     ${QT6_DIR}"
fi

# Clean if requested
if [[ $CLEAN -eq 1 ]] && [[ -d "$BUILD_DIR" ]]; then
    echo "Cleaning build directory..."
    rm -rf "$BUILD_DIR"
fi

mkdir -p "$BUILD_DIR"

# Configure
CMAKE_ARGS=(
    -G Ninja
    -S "$PROJECT_DIR"
    -B "$BUILD_DIR"
    "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}"
    "-DENABLE_CUDA=${ENABLE_CUDA}"
)

if [[ -n "$QT6_DIR" ]]; then
    CMAKE_ARGS+=("-DCMAKE_PREFIX_PATH=${QT6_DIR}")
fi

# Auto-detect CUDA toolkit and compiler
if [[ "$ENABLE_CUDA" == "ON" ]]; then
    if [[ -d "/usr/local/cuda" ]]; then
        CMAKE_ARGS+=("-DCUDAToolkit_ROOT=/usr/local/cuda")
        if [[ -x "/usr/local/cuda/bin/nvcc" ]]; then
            CMAKE_ARGS+=("-DCMAKE_CUDA_COMPILER=/usr/local/cuda/bin/nvcc")
        fi
    fi
fi

echo ""
echo "--- CMake Configure ---"
cmake "${CMAKE_ARGS[@]}"

echo ""
echo "--- CMake Build ---"
cmake --build "$BUILD_DIR" --parallel "$JOBS"

echo ""
echo "=== Build Complete ==="
BINARY="${BUILD_DIR}/cuda-media-upscaler"
if [[ -f "$BINARY" ]]; then
    echo "Binary: ${BINARY}"
    echo "Size:   $(du -h "$BINARY" | cut -f1)"
    echo ""
    echo "Run with: ${BINARY}"
else
    echo "WARNING: Binary not found at expected path"
    echo "Check build output above for errors"
fi

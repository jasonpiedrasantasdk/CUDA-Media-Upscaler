#!/usr/bin/env bash
# Launch CUDA Media Upscaler (C++ Native)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BINARY="${SCRIPT_DIR}/build/cuda-media-upscaler"

# ---------------------------------------------------------------------------
# Qt discovery
# ---------------------------------------------------------------------------
# If Qt was installed outside the system paths (e.g. the official Qt online
# installer under ~/Qt), point the loader and the platform plugin path at it.
if command -v qmake6 >/dev/null 2>&1; then
    QT_PLUGINS="$(qmake6 -query QT_INSTALL_PLUGINS 2>/dev/null || true)"
    QT_LIBS="$(qmake6 -query QT_INSTALL_LIBS 2>/dev/null || true)"
    if [[ -n "${QT_PLUGINS}" ]]; then
        export QT_QPA_PLATFORM_PLUGIN_PATH="${QT_PLUGINS}/platforms"
    fi
    if [[ -n "${QT_LIBS}" ]]; then
        export LD_LIBRARY_PATH="${QT_LIBS}${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
    fi
else
    # Fall back to a Qt online-installer layout (~/Qt6/<version>/gcc_64).
    for prefix in "${HOME}"/Qt6/*/gcc_64; do
        if [[ -d "${prefix}/lib" ]]; then
            export QT_QPA_PLATFORM_PLUGIN_PATH="${prefix}/plugins/platforms"
            export LD_LIBRARY_PATH="${prefix}/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
            break
        fi
    done
fi

# ---------------------------------------------------------------------------
# Runtime checks
# ---------------------------------------------------------------------------
if [[ ! -x "${BINARY}" ]]; then
    echo "Error: Binary not found at ${BINARY}"
    echo "Run ./scripts/build.sh first to build the project."
    exit 1
fi

if command -v nvidia-smi &>/dev/null; then
    echo "GPU: $(nvidia-smi --query-gpu=name,memory.total --format=csv,noheader,nounits 2>/dev/null | head -1)"
else
    echo "GPU: not detected (CPU fallback mode)"
fi

echo -n "ffmpeg: " && ffmpeg -version 2>/dev/null | head -1 || echo "NOT FOUND (video processing disabled)"
echo -n "gifsicle: " && gifsicle --version 2>/dev/null | head -1 || echo "NOT FOUND (GIF optimization disabled)"

echo ""
echo "Launching CUDA Media Upscaler (C++ Native)..."
exec "${BINARY}" "$@"

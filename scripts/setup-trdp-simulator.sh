#!/usr/bin/env bash
#
# Setup + build script for TRDP Simulator
#
# Usage examples:
#   ./scripts/setup-trdp-simulator.sh --use-stubs               # build with stubbed TRDP (no external dependency)
#   ./scripts/setup-trdp-simulator.sh --trdp-root ~/git/trdp    # build against local TRDP checkout
#   ./scripts/setup-trdp-simulator.sh --trdp-lib /path/libtrdp.a --trdp-include /path/trdp/include
#
# Environment overrides:
#   TRDP_ROOT        (directory where TRDP source/build lives)
#   TRDP_LIB_PATH    (full path to libtrdp.a)
#   TRDP_INCLUDE_DIR (path to TRDP headers)
#   USE_STUBS        ("1" to force TRDP stubs)

set -euo pipefail

# -----------------------------
# Defaults (you can change)
# -----------------------------

BUILD_TYPE="Debug"

# Try to guess a sane default for TRDP root (you can override with env or args)
DEFAULT_TRDP_ROOT="${HOME}/git/trdp"

TRDP_ROOT="${TRDP_ROOT:-$DEFAULT_TRDP_ROOT}"
TRDP_LIB_PATH="${TRDP_LIB_PATH:-}"
TRDP_INCLUDE_DIR="${TRDP_INCLUDE_DIR:-}"
USE_STUBS="${USE_STUBS:-0}"

# -----------------------------
# Parse arguments
# -----------------------------
usage() {
    cat <<'USAGE'
Usage: $0 [options]

Options:
  --trdp-root DIR        Root of TRDP project (default: ${DEFAULT_TRDP_ROOT})
  --trdp-lib FILE        Path to libtrdp.a
  --trdp-include DIR     Path to TRDP headers (tau_* etc.)
  --use-stubs            Build with built-in TRDP stub implementation (skips TRDP detection)
  --build-type TYPE      CMake build type (Debug/Release; default: ${BUILD_TYPE})
  --no-apt               Skip apt-get dependency installation
  -h, --help             Show this help
USAGE
}

DO_APT=1

while [[ $# -gt 0 ]]; do
    case "$1" in
        --trdp-root)
            TRDP_ROOT="$2"
            shift 2
            ;;
        --trdp-lib)
            TRDP_LIB_PATH="$2"
            shift 2
            ;;
        --trdp-include)
            TRDP_INCLUDE_DIR="$2"
            shift 2
            ;;
        --use-stubs)
            USE_STUBS=1
            shift 1
            ;;
        --build-type)
            BUILD_TYPE="$2"
            shift 2
            ;;
        --no-apt)
            DO_APT=0
            shift 1
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            usage
            exit 1
            ;;
    esac
done

# Normalize TRDP_ROOT (no trailing slash)
TRDP_ROOT="${TRDP_ROOT%/}"

if [[ "${USE_STUBS}" -eq 1 ]]; then
    echo "=== TRDP Simulator setup (stubbed TRDP) ==="
else
    echo "=== TRDP Simulator setup (linking to TRDP) ==="
fi
echo "Build type        : ${BUILD_TYPE}"
echo "TRDP root         : ${TRDP_ROOT}"
echo "TRDP lib (input)  : ${TRDP_LIB_PATH:-<auto-detect>}"
echo "TRDP include (in) : ${TRDP_INCLUDE_DIR:-<auto-detect>}"
echo "Run apt install   : ${DO_APT}"

# -----------------------------
# Install dependencies (Debian/Ubuntu)
# -----------------------------

if [[ "${DO_APT}" -eq 1 ]]; then
    echo "=== Installing build dependencies via apt (requires sudo) ==="
    sudo apt update
    sudo apt install -y \
        build-essential \
        cmake \
        libjsoncpp-dev \
        nlohmann-json3-dev \
        libdrogon-dev || {
            echo "Warning: libdrogon-dev install failed. You may need to build Drogon from source." >&2
        }
fi

# -----------------------------
# Detect TRDP include / lib if not provided (only when not using stubs)
# -----------------------------

if [[ "${USE_STUBS}" -ne 1 ]]; then
    if [[ -z "${TRDP_LIB_PATH}" ]]; then
        echo "=== Auto-detecting libtrdp.a under ${TRDP_ROOT} ==="
        # Try a few common patterns
        CANDIDATES=(
            "${TRDP_ROOT}/build/libtrdp.a"
            "${TRDP_ROOT}/lib/libtrdp.a"
            "${TRDP_ROOT}/trdp/build/libtrdp.a"
            "${TRDP_ROOT}/trdp/lib/libtrdp.a"
        )

        for c in "${CANDIDATES[@]}"; do
            if [[ -f "${c}" ]]; then
                TRDP_LIB_PATH="${c}"
                break
            fi
        done
    fi

    if [[ -z "${TRDP_LIB_PATH}" ]]; then
        echo "ERROR: Could not auto-detect libtrdp.a. Please build TRDP and/or pass --trdp-lib /path/to/libtrdp.a" >&2
        exit 1
    fi

    if [[ -z "${TRDP_INCLUDE_DIR}" ]]; then
        echo "=== Auto-detecting TRDP include dir under ${TRDP_ROOT} ==="
        # Try a few common include directories
        CANDIDATES_INC=(
            "${TRDP_ROOT}/include"
            "${TRDP_ROOT}/trdp/include"
            "${TRDP_ROOT}/src/api"   # adjust to your real TRDP layout if needed
        )

        for c in "${CANDIDATES_INC[@]}"; do
            if [[ -d "${c}" ]]; then
                TRDP_INCLUDE_DIR="${c}"
                break
            fi
        done
    fi

    if [[ -z "${TRDP_INCLUDE_DIR}" ]]; then
        echo "ERROR: Could not auto-detect TRDP include dir. Please pass --trdp-include /path/to/trdp/includes" >&2
        exit 1
    fi
fi

if [[ "${USE_STUBS}" -ne 1 ]]; then
    echo "Using TRDP_LIB_PATH   = ${TRDP_LIB_PATH}"
    echo "Using TRDP_INCLUDE_DIR= ${TRDP_INCLUDE_DIR}"
else
    echo "Using TRDP stub implementation (no external TRDP library required)"
fi

# -----------------------------
# Configure + build with CMake
# -----------------------------

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

echo "Project root      : ${PROJECT_ROOT}"
echo "Build directory   : ${PROJECT_ROOT}/build"

mkdir -p "${PROJECT_ROOT}/build"

CMAKE_ARGS=(
    -B "${PROJECT_ROOT}/build" -S "${PROJECT_ROOT}"
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"
)

if [[ "${USE_STUBS}" -eq 1 ]]; then
    CMAKE_ARGS+=("-DTRDP_USE_STUBS=ON")
else
    CMAKE_ARGS+=(
        "-DTRDP_USE_STUBS=OFF"
        "-DTRDP_LIB_PATH=${TRDP_LIB_PATH}"
        "-DTRDP_INCLUDE_DIR=${TRDP_INCLUDE_DIR}"
    )
fi

cmake "${CMAKE_ARGS[@]}"
cmake --build "${PROJECT_ROOT}/build" --target trdp-simulator -j"$(nproc)"

echo "=== Build finished ==="
echo "Binary should be at: ${PROJECT_ROOT}/build/trdp-simulator"

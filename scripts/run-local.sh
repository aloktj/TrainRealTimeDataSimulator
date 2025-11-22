#!/usr/bin/env bash
#
# Build (if needed) and run the TRDP simulator locally.
# Defaults to the stubbed TRDP implementation so contributors can get started
# without the proprietary library.

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
CONFIG_OVERRIDE=""
USE_STUBS=1
TRDP_LIB_PATH=""
TRDP_INCLUDE_DIR=""

usage() {
    cat <<'USAGE'
Usage: $0 [options]

Options:
  --config PATH        Path to device XML (will be copied to config/trdp.xml)
  --build-dir DIR      CMake build directory (default: build)
  --use-trdp           Build against a real TRDP library instead of stubs
  --trdp-lib FILE      Path to libtrdp.a (required with --use-trdp)
  --trdp-include DIR   Path to TRDP headers (required with --use-trdp)
  --no-build           Assume the project is already configured/built
  -h, --help           Show this help

Examples:
  ./scripts/run-local.sh
  ./scripts/run-local.sh --config config/sample_pd_only.xml
  ./scripts/run-local.sh --use-trdp --trdp-lib /path/libtrdp.a --trdp-include /path/include
USAGE
}

DO_BUILD=1

while [[ $# -gt 0 ]]; do
    case "$1" in
        --config)
            CONFIG_OVERRIDE="$2"
            shift 2
            ;;
        --build-dir)
            BUILD_DIR="$2"
            shift 2
            ;;
        --use-trdp)
            USE_STUBS=0
            shift 1
            ;;
        --trdp-lib)
            TRDP_LIB_PATH="$2"
            shift 2
            ;;
        --trdp-include)
            TRDP_INCLUDE_DIR="$2"
            shift 2
            ;;
        --no-build)
            DO_BUILD=0
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

TARGET_CONFIG="${PROJECT_ROOT}/config/trdp.xml"

if [[ -n "${CONFIG_OVERRIDE}" ]]; then
    if [[ ! -f "${CONFIG_OVERRIDE}" ]]; then
        echo "Config file not found: ${CONFIG_OVERRIDE}" >&2
        exit 1
    fi
    echo "Copying ${CONFIG_OVERRIDE} -> ${TARGET_CONFIG}"
    cp "${CONFIG_OVERRIDE}" "${TARGET_CONFIG}"
fi

if [[ ! -f "${TARGET_CONFIG}" ]]; then
    echo "Config file not found: ${TARGET_CONFIG}" >&2
    exit 1
fi

if [[ "${USE_STUBS}" -ne 1 ]]; then
    if [[ -z "${TRDP_LIB_PATH}" || -z "${TRDP_INCLUDE_DIR}" ]]; then
        echo "--use-trdp requires --trdp-lib and --trdp-include" >&2
        exit 1
    fi
fi

if [[ "${DO_BUILD}" -eq 1 ]]; then
    mkdir -p "${BUILD_DIR}"
    CMAKE_ARGS=(
        -B "${BUILD_DIR}" -S "${PROJECT_ROOT}"
        -DTRDP_USE_STUBS=$( [[ ${USE_STUBS} -eq 1 ]] && echo ON || echo OFF )
    )
    if [[ "${USE_STUBS}" -ne 1 ]]; then
        CMAKE_ARGS+=("-DTRDP_LIB_PATH=${TRDP_LIB_PATH}" "-DTRDP_INCLUDE_DIR=${TRDP_INCLUDE_DIR}")
    fi
    cmake "${CMAKE_ARGS[@]}"
    cmake --build "${BUILD_DIR}" --target trdp-simulator -j"$(nproc)"
fi

pushd "${PROJECT_ROOT}" >/dev/null

if [[ ! -x "${BUILD_DIR}/trdp-simulator" ]]; then
    echo "Binary not found at ${BUILD_DIR}/trdp-simulator" >&2
    exit 1
fi

echo "Starting simulator from ${BUILD_DIR}/trdp-simulator using config ${TARGET_CONFIG}"
"${BUILD_DIR}/trdp-simulator" 2>&1 | tee "${BUILD_DIR}/trdp-simulator.log"

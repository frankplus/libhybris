#!/bin/bash
# -*- coding: utf-8 -*-

# Wrapper script for libhybris OpenHarmony cross-compilation
# Provides common build configurations

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CROSS_BUILD_SCRIPT="$SCRIPT_DIR/libhybris_cross_build.py"

# Default values
OPENHARMONY_DIR=""
PRODUCT=""
ANDROID_HEADERS=""
ARCH="arm64"
BUILD_TYPE="release"
JOBS=$(nproc)
INSTALL_PREFIX="/usr/local"

usage() {
    cat << EOF
libhybris OpenHarmony Cross-compilation Wrapper

Usage: $0 [OPTIONS] OPENHARMONY_DIR PRODUCT

Common configurations:
  --rk3568           Build for RK3568 device (arm64)
  --hispark          Build for HiSpark Taurus (arm64)
  --debug            Debug build with tracing enabled
  --release          Release build (default)
  --quick            Quick setup-only (no compilation)

Options:
  --android-headers PATH    Path to Android headers
  --arch ARCH              Target architecture (arm, arm64, x86, x86-64)
  --jobs N                 Number of parallel jobs (default: $(nproc))
  --install-prefix PATH    Installation prefix (default: /usr/local)
  --install                Install after build
  --help                   Show this help

Examples:
  $0 --rk3568 /path/to/openharmony rk3568
  $0 --debug --install /path/to/openharmony hispark_taurus_standard
  $0 --android-headers /opt/android-headers /path/to/openharmony rk3568

EOF
}

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --rk3568)
            ARCH="arm64"
            PRODUCT="rk3568"
            shift
            ;;
        --hispark)
            ARCH="arm64"
            PRODUCT="hispark_taurus_standard"
            shift
            ;;
        --debug)
            BUILD_TYPE="debug"
            shift
            ;;
        --release)
            BUILD_TYPE="release"
            shift
            ;;
        --quick)
            QUICK_MODE="true"
            shift
            ;;
        --android-headers)
            ANDROID_HEADERS="$2"
            shift 2
            ;;
        --arch)
            ARCH="$2"
            shift 2
            ;;
        --jobs)
            JOBS="$2"
            shift 2
            ;;
        --install-prefix)
            INSTALL_PREFIX="$2"
            shift 2
            ;;
        --install)
            DO_INSTALL="true"
            shift
            ;;
        --help)
            usage
            exit 0
            ;;
        -*)
            echo "Unknown option: $1" >&2
            usage >&2
            exit 1
            ;;
        *)
            if [[ -z "$OPENHARMONY_DIR" ]]; then
                OPENHARMONY_DIR="$1"
            elif [[ -z "$PRODUCT" ]]; then
                PRODUCT="$1"
            else
                echo "Too many positional arguments" >&2
                usage >&2
                exit 1
            fi
            shift
            ;;
    esac
done

# Validate required arguments
if [[ -z "$OPENHARMONY_DIR" ]]; then
    echo "Error: OpenHarmony directory is required" >&2
    usage >&2
    exit 1
fi

if [[ -z "$PRODUCT" ]]; then
    echo "Error: Product name is required" >&2
    usage >&2
    exit 1
fi

# Build command arguments
ARGS=("$OPENHARMONY_DIR" "$PRODUCT")
ARGS+=(--arch "$ARCH")
ARGS+=(--jobs "$JOBS")
ARGS+=(--install-prefix "$INSTALL_PREFIX")

if [[ -n "$ANDROID_HEADERS" ]]; then
    ARGS+=(--android-headers "$ANDROID_HEADERS")
fi

if [[ "$BUILD_TYPE" == "debug" ]]; then
    ARGS+=(--debug --enable-trace)
fi

if [[ "$QUICK_MODE" == "true" ]]; then
    ARGS+=(--setup-only)
fi

if [[ "$DO_INSTALL" == "true" ]]; then
    ARGS+=(--install)
fi

# Print configuration
echo "=== libhybris OpenHarmony Cross-compilation ==="
echo "OpenHarmony Dir: $OPENHARMONY_DIR"
echo "Product:         $PRODUCT"
echo "Architecture:    $ARCH"
echo "Build Type:      $BUILD_TYPE"
echo "Jobs:            $JOBS"
echo "Install Prefix:  $INSTALL_PREFIX"
if [[ -n "$ANDROID_HEADERS" ]]; then
    echo "Android Headers: $ANDROID_HEADERS"
fi
echo "=============================================="
echo

# Execute the cross-build script
exec python3 "$CROSS_BUILD_SCRIPT" "${ARGS[@]}"

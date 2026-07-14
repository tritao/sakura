#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${BUILD_DIR:-"$SCRIPT_DIR/build"}"
BUILD_TYPE="${BUILD_TYPE:-Debug}"
INSTALL_PREFIX="${INSTALL_PREFIX:-/usr/local}"
BUILD_TARGET="${BUILD_TARGET:-sakura}"

# Set VTE_PREFIX explicitly only when VTE was installed outside the system
# paths. With no prefix, CMake/pkg-config uses the system VTE package.

EXTRA_CFLAGS="${CFLAGS:-}"
EXTRA_LDFLAGS="${LDFLAGS:-}"
PKG_CONFIG_PATH_VALUE="${PKG_CONFIG_PATH:-}"

if [[ -n "${VTE_PREFIX:-}" && -d "$VTE_PREFIX/usr" ]]; then
	VTE_PKGCONFIG_DIR="$VTE_PREFIX/usr/lib/x86_64-linux-gnu/pkgconfig"
	if [[ -d "$VTE_PKGCONFIG_DIR" ]]; then
		if [[ -n "$PKG_CONFIG_PATH_VALUE" ]]; then
			PKG_CONFIG_PATH_VALUE="$VTE_PKGCONFIG_DIR:$PKG_CONFIG_PATH_VALUE"
		else
			PKG_CONFIG_PATH_VALUE="$VTE_PKGCONFIG_DIR"
		fi
	fi
	EXTRA_CFLAGS="$EXTRA_CFLAGS -I$VTE_PREFIX/usr/include/vte-2.91"
	EXTRA_LDFLAGS="$EXTRA_LDFLAGS -L$VTE_PREFIX/usr/lib/x86_64-linux-gnu"
fi

cmake_args=(
	-S "$SCRIPT_DIR"
	-B "$BUILD_DIR"
	-DCMAKE_BUILD_TYPE="$BUILD_TYPE"
	-DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX"
	-DCMAKE_C_FLAGS="$EXTRA_CFLAGS"
	-DCMAKE_EXE_LINKER_FLAGS="$EXTRA_LDFLAGS"
	# VTE is discovered through pkg-config; don't retain paths from a removed
	# custom installation in an existing CMake build directory.
	-U "VTE_*"
	-U "pkgcfg_lib_VTE_*"
	-U "__pkg_config_checked_VTE"
)

if [[ -n "$PKG_CONFIG_PATH_VALUE" ]]; then
	export PKG_CONFIG_PATH="$PKG_CONFIG_PATH_VALUE"
fi

cmake "${cmake_args[@]}"
if [[ -n "${JOBS:-}" ]]; then
	cmake --build "$BUILD_DIR" --target "$BUILD_TARGET" --parallel "$JOBS"
else
	cmake --build "$BUILD_DIR" --target "$BUILD_TARGET" --parallel
fi

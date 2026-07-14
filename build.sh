#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${BUILD_DIR:-"$SCRIPT_DIR/build"}"
BUILD_TYPE="${BUILD_TYPE:-Debug}"

# Set VTE_PREFIX explicitly when VTE was installed outside the system paths.
# The /tmp prefix is the unprivileged fallback used on this development machine.
if [[ -z "${VTE_PREFIX:-}" ]]; then
	if [[ -f /tmp/sakura-vte/root/usr/include/vte-2.91/vte/vte.h ]]; then
		VTE_PREFIX="/tmp/sakura-vte/root"
	fi
fi

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
	-DCMAKE_C_FLAGS="$EXTRA_CFLAGS"
	-DCMAKE_EXE_LINKER_FLAGS="$EXTRA_LDFLAGS"
)

if [[ -n "$PKG_CONFIG_PATH_VALUE" ]]; then
	export PKG_CONFIG_PATH="$PKG_CONFIG_PATH_VALUE"
fi

cmake "${cmake_args[@]}"
if [[ -n "${JOBS:-}" ]]; then
	cmake --build "$BUILD_DIR" --target sakura --parallel "$JOBS"
else
	cmake --build "$BUILD_DIR" --target sakura --parallel
fi

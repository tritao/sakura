#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${BUILD_DIR:-"$SCRIPT_DIR/build"}"

if [[ ! -x "$BUILD_DIR/src/sakura" ]]; then
	"$SCRIPT_DIR/build.sh"
fi

if [[ -z "${VTE_PREFIX:-}" && -f /tmp/sakura-vte/root/usr/lib/x86_64-linux-gnu/libvte-2.91.so ]]; then
	VTE_PREFIX="/tmp/sakura-vte/root"
fi

if [[ -n "${VTE_PREFIX:-}" && -d "$VTE_PREFIX/usr/lib/x86_64-linux-gnu" ]]; then
	export LD_LIBRARY_PATH="$VTE_PREFIX/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
fi

exec "$BUILD_DIR/src/sakura" "$@"

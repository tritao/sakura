#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${BUILD_DIR:-"$SCRIPT_DIR/build"}"
BUILD_TYPE="${BUILD_TYPE:-Release}"
PREFIX="${PREFIX:-/usr/local}"

usage() {
	cat <<EOF
Usage: $(basename "$0")

Build and install Sakura, including its Mint application launcher.

Environment variables:
  PREFIX       Installation prefix (default: /usr/local)
  BUILD_DIR    CMake build directory (default: $SCRIPT_DIR/build)
  BUILD_TYPE   CMake build type (default: Release)
  JOBS         Number of parallel build jobs

Examples:
  ./install.sh
  PREFIX="\$HOME/.local" ./install.sh
EOF
}

case "${1:-}" in
	"") ;;
	-h|--help)
		usage
		exit 0
		;;
	*)
		echo "Unknown option: $1" >&2
		usage >&2
		exit 2
		;;
esac

if [[ "$PREFIX" != /* ]]; then
	echo "PREFIX must be an absolute path: $PREFIX" >&2
	exit 2
fi

if [[ "$PREFIX" == /usr/local || "$PREFIX" == /usr || "$PREFIX" == /opt/* ]]; then
	if [[ "$(id -u)" -eq 0 ]]; then
		INSTALL_AS=()
	else
		if ! command -v sudo >/dev/null 2>&1; then
			echo "Installing to $PREFIX requires root privileges, but sudo was not found." >&2
			exit 1
		fi
		INSTALL_AS=(sudo)
	fi
else
	INSTALL_AS=()
fi

echo "Building Sakura ($BUILD_TYPE)..."
BUILD_DIR="$BUILD_DIR" \
BUILD_TYPE="$BUILD_TYPE" \
INSTALL_PREFIX="$PREFIX" \
BUILD_TARGET=all \
"$SCRIPT_DIR/build.sh"

echo "Installing Sakura to $PREFIX..."
"${INSTALL_AS[@]}" cmake --install "$BUILD_DIR" --prefix "$PREFIX"

APPLICATIONS_DIR="$PREFIX/share/applications"
if command -v update-desktop-database >/dev/null 2>&1 && [[ -d "$APPLICATIONS_DIR" ]]; then
	echo "Updating desktop application database..."
	"${INSTALL_AS[@]}" update-desktop-database "$APPLICATIONS_DIR"
fi

HOOK_PATH="$PREFIX/bin/sakura-codex-session-hook"
if command -v codex >/dev/null 2>&1; then
	echo "Codex detected; enabling Sakura Codex session tracking..."
	if "$HOOK_PATH" --install; then
		echo "Codex session tracking is enabled."
	else
		echo "Warning: could not enable Codex session tracking automatically." >&2
		echo "Run this command after fixing the problem:" >&2
		echo "  $HOOK_PATH --install" >&2
	fi
else
	echo "Codex was not detected; session tracking was not enabled."
	echo "If Codex is installed later, enable it with:"
	echo "  $HOOK_PATH --install"
fi

echo
echo "Sakura is installed. Search for 'Sakura' in the Mint application menu."

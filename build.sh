#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${BUILD_DIR:-"$SCRIPT_DIR/build"}"
BUILD_TYPE="${BUILD_TYPE:-Debug}"
INSTALL_PREFIX="${INSTALL_PREFIX:-/usr/local}"
BUILD_TARGET="${BUILD_TARGET:-all}"
DEPS_DIR="${DEPS_DIR:-"$SCRIPT_DIR/.deps"}"
PROTOBUF_VERSION="${PROTOBUF_VERSION:-27.1}"
PROTOBUF_C_VERSION="${PROTOBUF_C_VERSION:-1.5.2}"
ABSEIL_REF="${ABSEIL_REF:-lts_2023_08_02}"
PROTOBUF_PREFIX="${PROTOBUF_PREFIX:-"$DEPS_DIR/install"}"

# Set VTE_PREFIX explicitly only when VTE was installed outside the system
# paths. With no prefix, CMake/pkg-config uses the system VTE package.

EXTRA_CFLAGS="${CFLAGS:-}"
EXTRA_LDFLAGS="${LDFLAGS:-}"
PKG_CONFIG_PATH_VALUE="${PKG_CONFIG_PATH:-}"

for required_command in cmake pkg-config tar; do
	if ! command -v "$required_command" >/dev/null 2>&1; then
		echo "$required_command is required to build Sakura." >&2
		exit 1
	fi
done

download_file() {
	local url="$1"
	local destination="$2"

	if command -v curl >/dev/null 2>&1; then
		curl --fail --location --silent --show-error --retry 3 \
			--output "$destination" "$url"
	elif command -v wget >/dev/null 2>&1; then
		wget --quiet --tries=3 --output-document="$destination" "$url"
	else
		echo "curl or wget is required to fetch local build dependencies." >&2
		exit 1
	fi
}

build_parallel() {
	local build_directory="$1"
	local build_target="${2:-all}"

	if [[ -n "${JOBS:-}" ]]; then
		cmake --build "$build_directory" --target "$build_target" --parallel "$JOBS"
	else
		cmake --build "$build_directory" --target "$build_target" --parallel
	fi
}

extract_source() {
	local archive="$1"
	local destination="$2"

	rm -rf "$destination"
	mkdir -p "$destination"
	tar -xzf "$archive" --strip-components=1 -C "$destination"
}

ensure_protobuf() {
	local downloads="$DEPS_DIR/downloads"
	local sources="$DEPS_DIR/sources"
	local protobuf_archive="$downloads/protobuf-$PROTOBUF_VERSION.tar.gz"
	local protobuf_source="$sources/protobuf-$PROTOBUF_VERSION"
	local protobuf_build="$DEPS_DIR/build/protobuf-$PROTOBUF_VERSION"
	local abseil_archive="$downloads/abseil-$ABSEIL_REF.tar.gz"
	local protobuf_c_archive="$downloads/protobuf-c-$PROTOBUF_C_VERSION.tar.gz"
	local protobuf_c_source="$sources/protobuf-c-$PROTOBUF_C_VERSION"
	local pkg_config_dirs=()

	if [[ -x "$PROTOBUF_PREFIX/bin/protoc" &&
	      -x "$PROTOBUF_PREFIX/bin/protoc-c" &&
	      -f "$PROTOBUF_PREFIX/lib/pkgconfig/libprotobuf-c.pc" ]]; then
		echo "Using project-local protobuf tools from $PROTOBUF_PREFIX"
	else
		mkdir -p "$downloads" "$sources" "$DEPS_DIR/build" "$PROTOBUF_PREFIX"

		if [[ ! -f "$protobuf_archive" ]]; then
			echo "Fetching protobuf $PROTOBUF_VERSION..."
			download_file \
				"https://github.com/protocolbuffers/protobuf/releases/download/v$PROTOBUF_VERSION/protobuf-$PROTOBUF_VERSION.tar.gz" \
				"$protobuf_archive"
		fi
		if [[ ! -f "$protobuf_source/CMakeLists.txt" ]]; then
			echo "Preparing protoc $PROTOBUF_VERSION..."
			extract_source "$protobuf_archive" "$protobuf_source"
		fi
		if [[ ! -f "$protobuf_source/third_party/abseil-cpp/CMakeLists.txt" ]]; then
			if [[ ! -f "$abseil_archive" ]]; then
				echo "Fetching protobuf's Abseil dependency $ABSEIL_REF..."
				download_file \
					"https://github.com/abseil/abseil-cpp/archive/refs/heads/$ABSEIL_REF.tar.gz" \
					"$abseil_archive"
			fi
			extract_source "$abseil_archive" \
				"$protobuf_source/third_party/abseil-cpp"
		fi
		cmake -S "$protobuf_source" -B "$protobuf_build" \
			-DCMAKE_BUILD_TYPE=Release \
			-DCMAKE_INSTALL_PREFIX="$PROTOBUF_PREFIX" \
			-Dprotobuf_BUILD_TESTS=OFF \
			-Dprotobuf_BUILD_CONFORMANCE=OFF \
			-Dprotobuf_BUILD_EXAMPLES=OFF \
			-Dprotobuf_BUILD_SHARED_LIBS=OFF \
			-Dprotobuf_BUILD_PROTOC_BINARIES=ON \
			-Dprotobuf_BUILD_LIBPROTOC=OFF
		# Build the install target so every static dependency that the generated
		# install manifests reference (including Abseil) exists.
		build_parallel "$protobuf_build" install

		if [[ ! -f "$protobuf_c_archive" ]]; then
			echo "Fetching protobuf-c $PROTOBUF_C_VERSION..."
			download_file \
				"https://github.com/protobuf-c/protobuf-c/releases/download/v$PROTOBUF_C_VERSION/protobuf-c-$PROTOBUF_C_VERSION.tar.gz" \
				"$protobuf_c_archive"
		fi
		if [[ ! -f "$protobuf_c_source/configure" ]]; then
			echo "Preparing protobuf-c $PROTOBUF_C_VERSION..."
			extract_source "$protobuf_c_archive" "$protobuf_c_source"
		fi
		pushd "$protobuf_c_source" >/dev/null
		PATH="$PROTOBUF_PREFIX/bin:$PATH" \
			PKG_CONFIG_PATH="$PROTOBUF_PREFIX/lib/pkgconfig:${PKG_CONFIG_PATH_VALUE}" \
			./configure --prefix="$PROTOBUF_PREFIX"
		if [[ -n "${JOBS:-}" ]]; then
			make --jobs "$JOBS"
		else
			make --jobs
		fi
		make install
		popd >/dev/null
	fi

	for pkg_config_dir in \
		"$PROTOBUF_PREFIX/lib/pkgconfig" \
		"$PROTOBUF_PREFIX/lib64/pkgconfig" \
		"$PROTOBUF_PREFIX/lib/x86_64-linux-gnu/pkgconfig"; do
		if [[ -d "$pkg_config_dir" ]]; then
			pkg_config_dirs+=("$pkg_config_dir")
		fi
	done
	if [[ "${#pkg_config_dirs[@]}" -gt 0 ]]; then
		local joined_pkg_config_path
		joined_pkg_config_path="$(IFS=:; echo "${pkg_config_dirs[*]}")"
		if [[ -n "$PKG_CONFIG_PATH_VALUE" ]]; then
			PKG_CONFIG_PATH_VALUE="$joined_pkg_config_path:$PKG_CONFIG_PATH_VALUE"
		else
			PKG_CONFIG_PATH_VALUE="$joined_pkg_config_path"
		fi
	fi
}

ensure_protobuf

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
	-DSAKURA_PROTOBUF_PREFIX="$PROTOBUF_PREFIX"
)

if [[ -n "$PKG_CONFIG_PATH_VALUE" ]]; then
	export PKG_CONFIG_PATH="$PKG_CONFIG_PATH_VALUE"
fi

cmake "${cmake_args[@]}"
build_parallel "$BUILD_DIR" "$BUILD_TARGET"

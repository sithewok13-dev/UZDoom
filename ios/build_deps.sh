#!/bin/bash
# Builds the static iOS (arm64) libraries UZDoom needs into $1:
# SDL2 (+ SDL2main), OpenAL Soft, libsndfile (with Ogg Vorbis, FLAC and
# Opus) and libvpx. (ZMusic is built with the game, from libraries/ZMusic.)
# From the owner's Raze port (ios/build_deps.sh).
set -ex
DEPS="$1"
: "${IOS_MIN:=15.0}"
: "${SDL_TAG:=release-2.32.8}"
: "${OPENAL_TAG:=1.24.3}"
: "${VPX_TAG:=v1.15.2}"
: "${OGG_TAG:=v1.3.5}" "${VORBIS_TAG:=v1.3.7}" "${FLAC_TAG:=1.4.3}" "${OPUS_TAG:=v1.5.2}" "${SNDFILE_TAG:=1.2.2}"
WORK="${RUNNER_TEMP:-/tmp}/ios-deps-src"
rm -rf "$WORK"; mkdir -p "$WORK" "$DEPS"
IOSFLAGS=(-G Ninja -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_ARCHITECTURES=arm64
          -DCMAKE_OSX_DEPLOYMENT_TARGET="$IOS_MIN" -DCMAKE_BUILD_TYPE=Release
          -DCMAKE_INSTALL_PREFIX="$DEPS" -DBUILD_SHARED_LIBS=OFF
          -DCMAKE_PREFIX_PATH="$DEPS" -DCMAKE_POLICY_VERSION_MINIMUM=3.5)
# (IOSFLAGS can be swapped for a native build to try this script on Linux)
[ -n "$DEPS_NATIVE" ] && IOSFLAGS=(-G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$DEPS"
          -DBUILD_SHARED_LIBS=OFF -DCMAKE_PREFIX_PATH="$DEPS" -DCMAKE_POLICY_VERSION_MINIMUM=3.5
          -DCMAKE_POSITION_INDEPENDENT_CODE=ON)
# Homebrew's pkg-config would hand the builds Mac libraries: hide them
export PKG_CONFIG_LIBDIR=/nonexistent PKG_CONFIG_PATH=

lib() {  # lib <name> <git url> <tag> [cmake options...]
  local n=$1 url=$2 tag=$3; shift 3
  git clone -q --depth 1 -b "$tag" "$url" "$WORK/$n"
  # (an iOS build looks for libraries in the iOS SDK only: let these find
  # the ones built here in $DEPS too, but never Homebrew's Mac ones)
  cmake -S "$WORK/$n" -B "$WORK/$n/build" "${IOSFLAGS[@]}" \
    -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=BOTH -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=BOTH \
    -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=BOTH "-DCMAKE_IGNORE_PREFIX_PATH=/opt/homebrew;/usr/local" "$@"
  cmake --build "$WORK/$n/build"
  cmake --install "$WORK/$n/build"
}

# --- SDL2
git clone -q --depth 1 -b "$SDL_TAG" https://github.com/libsdl-org/SDL.git "$WORK/SDL"
cmake -S "$WORK/SDL" -B "$WORK/SDL/build" "${IOSFLAGS[@]}" \
  -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST=OFF -DSDL_HIDAPI=OFF
cmake --build "$WORK/SDL/build"
cmake --install "$WORK/SDL/build"

# --- OpenAL Soft (CoreAudio backend)
git clone -q --depth 1 -b "$OPENAL_TAG" https://github.com/kcat/openal-soft.git "$WORK/openal"
cmake -S "$WORK/openal" -B "$WORK/openal/build" "${IOSFLAGS[@]}" \
  -DLIBTYPE=STATIC -DALSOFT_UTILS=OFF -DALSOFT_EXAMPLES=OFF -DALSOFT_TESTS=OFF \
  -DALSOFT_INSTALL_CONFIG=OFF -DALSOFT_INSTALL_HRTF_DATA=OFF -DALSOFT_INSTALL_AMBDEC_PRESETS=OFF \
  -DALSOFT_BACKEND_COREAUDIO=ON -DALSOFT_REQUIRE_COREAUDIO=ON
cmake --build "$WORK/openal/build"
cmake --install "$WORK/openal/build"

# --- libsndfile with Ogg Vorbis, FLAC and Opus: the sound effects and music
# in .ogg/.flac files (PWADs and the re-release's extras use .ogg/.flac music). On
# the desktop ZMusic loads libsndfile at run time; iOS has none, so it is
# built in. (No MP3: mpg123 isn't built.)
# (each told exactly where the ones before it are)
OGGV=(-DOGG_INCLUDE_DIR="$DEPS/include" -DOGG_LIBRARY="$DEPS/lib/libogg.a")
lib ogg https://github.com/xiph/ogg.git "$OGG_TAG" -DINSTALL_DOCS=OFF -DBUILD_TESTING=OFF
lib vorbis https://github.com/xiph/vorbis.git "$VORBIS_TAG" -DBUILD_TESTING=OFF "${OGGV[@]}"
lib flac https://github.com/xiph/flac.git "$FLAC_TAG" -DBUILD_CXXLIBS=OFF -DBUILD_PROGRAMS=OFF \
  -DBUILD_EXAMPLES=OFF -DBUILD_TESTING=OFF -DBUILD_DOCS=OFF -DINSTALL_MANPAGES=OFF -DWITH_OGG=ON "${OGGV[@]}"
lib opus https://github.com/xiph/opus.git "$OPUS_TAG" -DOPUS_BUILD_PROGRAMS=OFF -DOPUS_BUILD_TESTING=OFF
lib sndfile https://github.com/libsndfile/libsndfile.git "$SNDFILE_TAG" -DBUILD_PROGRAMS=OFF \
  -DBUILD_EXAMPLES=OFF -DBUILD_TESTING=OFF -DBUILD_REGTEST=OFF -DENABLE_CPACK=OFF \
  -DENABLE_MPEG=OFF -DINSTALL_MANPAGES=OFF -DENABLE_EXTERNAL_LIBS=ON "${OGGV[@]}" \
  -DVorbis_Vorbis_INCLUDE_DIR="$DEPS/include" -DVorbis_Enc_INCLUDE_DIR="$DEPS/include" \
  -DVorbis_File_INCLUDE_DIR="$DEPS/include" -DVorbis_Vorbis_LIBRARY="$DEPS/lib/libvorbis.a" \
  -DVorbis_Enc_LIBRARY="$DEPS/lib/libvorbisenc.a" -DVorbis_File_LIBRARY="$DEPS/lib/libvorbisfile.a" \
  -DFLAC_INCLUDE_DIR="$DEPS/include" -DFLAC_LIBRARY="$DEPS/lib/libFLAC.a" \
  -DOPUS_INCLUDE_DIR="$DEPS/include" -DOPUS_LIBRARY="$DEPS/lib/libopus.a"
# (it quietly leaves the codecs out if one isn't found: make sure)
grep -q "define HAVE_EXTERNAL_XIPH_LIBS 1" "$WORK/sndfile/build/src/config.h"

# --- libvpx (VP8 decoder for cutscene videos; its own configure, iOS = arm64-darwin-gcc)
git clone -q --depth 1 -b "$VPX_TAG" https://github.com/webmproject/libvpx.git "$WORK/libvpx"
(cd "$WORK/libvpx" && ./configure --target=arm64-darwin-gcc --prefix="$DEPS" \
  --extra-cflags="-miphoneos-version-min=$IOS_MIN" --extra-cxxflags="-miphoneos-version-min=$IOS_MIN" \
  --disable-examples --disable-tools --disable-docs --disable-unit-tests \
  --disable-vp8-encoder --disable-vp9-encoder --disable-shared --enable-static \
  && make -j"$(sysctl -n hw.ncpu)" && make install)

test -f "$DEPS/lib/libSDL2.a"
test -f "$DEPS/lib/libSDL2main.a"
test -f "$DEPS/lib/libopenal.a"
for l in sndfile vorbisenc vorbis FLAC opus ogg; do test -f "$DEPS/lib/lib$l.a"; done
test -f "$DEPS/lib/libvpx.a"
ls -la "$DEPS/lib"

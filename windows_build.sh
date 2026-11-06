#!/usr/bin/env bash
# Build the OnAir app (no tests) on Windows, in the "MSYS2 UCRT64" shell, with MSYS2's own FFmpeg: a full GPLv3 build
# (x264, x265, SVT-AV1, AOM, VP9 ... about 40 codec libraries, NVENC / AMF / QSV / Media Foundation, DirectShow cameras).
#   1) install the packages  2) check that FFmpeg is the full GPL build  3) configure  4) build build/dect2.exe
# Run from anywhere:   bash ~/OnAir/windows_build.sh      (it builds the branch that is checked out in the folder of this script)
# First, on a new or old MSYS2, by hand: pacman -Syu   (it may close the shell; open UCRT64 again and repeat until nothing is left)
# Environment (optional):
#   JOBS=8      parallel jobs
#   NATIVE=0    a build for any x86-64 PC (no AVX2, no -march=native), e.g. to give to others
# The result is GPLv3 (x264, x265 ... are GPL): fine for your own use; it is not the LGPL build the OnAir releases ship.
set -Eeuo pipefail

ONAIR_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
JOBS="${JOBS:-8}"
BUILD_DIR="$ONAIR_DIR/build"

GREEN=$'\e[1;32m'; RED=$'\e[1;31m'; RESET=$'\e[0m'
STEP="start"
step() { STEP="$1"; echo; echo "${GREEN}==> $1${RESET}"; }
info() { echo "${GREEN}    $1${RESET}"; }
trap 'echo; echo "${RED}FAILED in step: ${STEP}${RESET}"; echo "${RED}command: ${BASH_COMMAND}${RESET}"' ERR

step "0/4 Checking the shell (MSYS2 UCRT64) and the OnAir source folder"
if [ "${MSYSTEM:-}" != "UCRT64" ]; then
  echo "${RED}MSYSTEM is '${MSYSTEM:-not set}': open the 'MSYS2 UCRT64' shell and run this script there${RESET}"; exit 1
fi
[ -f "$ONAIR_DIR/CMakeLists.txt" ] || { echo "${RED}no CMakeLists.txt in $ONAIR_DIR: put this script in the OnAir checkout${RESET}"; false; }
MP="${MSYSTEM_PREFIX:-/ucrt64}"
info "OnAir source: $ONAIR_DIR (branch: $(git -C "$ONAIR_DIR" branch --show-current 2>/dev/null || echo '?'))"
info "build folder: $BUILD_DIR"

# ---------------------------------------------------------------------------------------------------------------------------
step "1/4 Installing the packages with pacman: compiler and tools, OnAir's libraries, FFmpeg with all its codec libraries"
info "build tools: gcc, CMake, Ninja, pkg-config, ccache, Rust (OnAir's rust/ workspace is mandatory), git"
pacman -S --needed --noconfirm git mingw-w64-ucrt-x86_64-{gcc,cmake,ninja,pkgconf,ccache,rust}
info "OnAir's libraries: GLFW, HackRF, SoapySDR, iconv"
pacman -S --needed --noconfirm mingw-w64-ucrt-x86_64-{glfw,hackrf,soapysdr,libiconv}
info "FFmpeg (MSYS2's GPL build; pacman installs x264, x265, SVT-AV1 ... with it)"
pacman -S --needed --noconfirm mingw-w64-ucrt-x86_64-ffmpeg

# ---------------------------------------------------------------------------------------------------------------------------
step "2/4 Checking FFmpeg: GPL, the codecs, the hardware encoders and the DirectShow camera input"
FFMPEG="$MP/bin/ffmpeg.exe"
info "$("$FFMPEG" -hide_banner -version | head -n1)"
BUILDCONF="$("$FFMPEG" -hide_banner -buildconf)"
ENCODERS="$("$FFMPEG" -hide_banner -encoders 2>/dev/null)"
DEVICES="$("$FFMPEG" -hide_banner -devices 2>/dev/null)"
for o in --enable-gpl --enable-version3; do
  grep -q -- "$o" <<<"$BUILDCONF" || { echo "${RED}this FFmpeg is not built with $o${RESET}"; false; }
done
for e in libx264 libx265 libsvtav1 h264_nvenc hevc_nvenc h264_amf h264_qsv h264_mf; do
  grep -qE " $e " <<<"$ENCODERS" || { echo "${RED}this FFmpeg has no encoder $e${RESET}"; false; }
done
grep -qE ' dshow ' <<<"$DEVICES" || { echo "${RED}this FFmpeg has no DirectShow camera input (dshow)${RESET}"; false; }
info "OK: GPLv3, $(grep -c -- '--enable-lib' <<<"$(tr -s ' ' '\n' <<<"$BUILDCONF")") codec libraries, x264/x265/SVT-AV1, NVENC/AMF/QSV/MF, dshow"

# ---------------------------------------------------------------------------------------------------------------------------
step "3/4 Configuring OnAir in $BUILD_DIR (Release, the app only, no tests, MSYS2's FFmpeg)"
CMAKE_ARGS=(-S "$ONAIR_DIR" -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release
            -DDECT2_BUILD_APP=ON -DDECT2_BUILD_TESTS=OFF -DDECT2_COPY_RUNTIME_DLLS=ON)
if [ "${NATIVE:-1}" = 1 ]; then
  info "fast build for THIS computer (AVX2, -march=native); NATIVE=0 for one that runs on any x86-64 PC"
  CMAKE_ARGS+=(-DDECT2_WIN_AVX2=ON -DCMAKE_CXX_FLAGS=-march=native -DCMAKE_C_FLAGS=-march=native)
fi
cmake --fresh "${CMAKE_ARGS[@]}"
#   --fresh: a clean configure every time (a changed CMakeLists.txt is picked up). If CMake says the generator differs,
#   the build folder was made with Makefiles: delete $BUILD_DIR and run again.

# ---------------------------------------------------------------------------------------------------------------------------
step "4/4 Building the app (target dect2) with $JOBS jobs"
info "the first build also compiles the Rust crates (cargo needs the internet the first time)"
cmake --build "$BUILD_DIR" -j"$JOBS" --target dect2

info "copying FFmpeg's DLLs and the DLLs they need (x264, x265, ...) next to dect2.exe, so it also starts outside MSYS2"
collect() {
  { ldd "$1" || true; } | awk -v p="$MP/" 'index($3, p) == 1 {print $3}' | while read -r dll; do
    [ -f "$BUILD_DIR/$(basename "$dll")" ] || { cp "$dll" "$BUILD_DIR/"; collect "$dll"; }
  done
}
for n in avutil swresample swscale avcodec avformat avdevice avfilter; do
  for f in "$MP"/bin/$n-*.dll; do
    [ -f "$f" ] || continue
    [ -f "$BUILD_DIR/$(basename "$f")" ] || cp "$f" "$BUILD_DIR/"
    collect "$f"
  done
done

STEP="done"
echo
echo "${GREEN}==> Done: $BUILD_DIR/dect2.exe (with MSYS2's GPL FFmpeg)${RESET}"
echo "${GREEN}    start it with: $BUILD_DIR/dect2.exe${RESET}"

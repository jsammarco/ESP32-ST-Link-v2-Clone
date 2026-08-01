#!/usr/bin/env bash
# Cross-compile the pinned OpenOCD remote_bitbang-SWD build for Windows.
#
# Run this once from WSL:
#   cd "/mnt/c/Users/Joe/Projects/Vaporware/esp32 ST-Link V2"
#   bash tools/build_openocd_windows.sh
#
# The result stays in this project at tools/openocd-windows/.  It is separate
# from both the system OpenOCD package and the WSL-native OpenOCD build.

set -euo pipefail

readonly OPENOCD_REV="0f70c6c325785517f35bbbb9316801bef7a79d8b"
readonly SOURCE_DIR="${HOME}/.cache/openocd-remote-swd"
# SOURCE_DIR is also used by the existing WSL-native build.  Autoconf refuses
# an out-of-tree configure when that source tree has already been configured,
# so make a separate linked worktree for the Windows cross-build.
readonly WINDOWS_SOURCE_DIR="${HOME}/.cache/openocd-remote-swd-windows"
readonly BUILD_DIR="${WINDOWS_SOURCE_DIR}/build-mingw-x64"
readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly OUTPUT_DIR="${SCRIPT_DIR}/openocd-windows"

sudo apt update
sudo apt install -y \
  autoconf automake build-essential git libtool pkg-config texinfo mingw-w64

mkdir -p "$(dirname "${SOURCE_DIR}")"
if [[ ! -d "${SOURCE_DIR}/.git" ]]; then
  git clone --recurse-submodules https://github.com/openocd-org/openocd.git "${SOURCE_DIR}"
fi

git -C "${SOURCE_DIR}" fetch --tags origin
git -C "${SOURCE_DIR}" checkout --detach "${OPENOCD_REV}"
git -C "${SOURCE_DIR}" submodule update --init --recursive

if [[ ! -e "${WINDOWS_SOURCE_DIR}" ]]; then
  git -C "${SOURCE_DIR}" worktree add --detach "${WINDOWS_SOURCE_DIR}" "${OPENOCD_REV}"
fi
git -C "${WINDOWS_SOURCE_DIR}" checkout --detach "${OPENOCD_REV}"
git -C "${WINDOWS_SOURCE_DIR}" submodule update --init --recursive

cd "${WINDOWS_SOURCE_DIR}"
./bootstrap

mkdir -p "${BUILD_DIR}"
# A prior failed configure can have detected Linux libusb through the host
# pkg-config.  Clean only this dedicated cross-build directory before asking
# configure again; the Linux-native build remains untouched.
if [[ -f "${BUILD_DIR}/Makefile" ]]; then
  make -C "${BUILD_DIR}" clean
fi
rm -f "${BUILD_DIR}/config.cache"
cd "${BUILD_DIR}"
# This adapter uses Winsock only.  Hiding host pkg-config packages prevents
# configure from linking Linux libusb into the Windows executable.
PKG_CONFIG_LIBDIR=/nonexistent PKG_CONFIG_PATH= \
  "${WINDOWS_SOURCE_DIR}/configure" \
  --host=x86_64-w64-mingw32 \
  --prefix=/openocd-remote-swd \
  --enable-remote-bitbang \
  --disable-werror
make -j"$(nproc)"

STAGE_DIR="$(mktemp -d)"
trap 'rm -rf "${STAGE_DIR}"' EXIT
make install DESTDIR="${STAGE_DIR}"

mkdir -p "${OUTPUT_DIR}"
cp -a "${STAGE_DIR}/openocd-remote-swd/." "${OUTPUT_DIR}/"

# Debian's MinGW package may choose the POSIX threading runtime.  Copy its
# runtime DLL when it is an actual dependency; ordinary Windows system DLLs
# are deliberately not copied.
if x86_64-w64-mingw32-objdump -p "${OUTPUT_DIR}/bin/openocd.exe" | grep -qi 'libwinpthread-1.dll'; then
  cp -f /usr/x86_64-w64-mingw32/bin/libwinpthread-1.dll "${OUTPUT_DIR}/bin/"
fi

echo
echo "Windows OpenOCD installed at:"
echo "  ${OUTPUT_DIR}/bin/openocd.exe"
echo
echo "Next, leave tools/serial_bridge.py running in Windows and run:"
echo "  powershell.exe -ExecutionPolicy Bypass -File '${SCRIPT_DIR}/flash_launcher_esp32.ps1' -ProbeOnly"

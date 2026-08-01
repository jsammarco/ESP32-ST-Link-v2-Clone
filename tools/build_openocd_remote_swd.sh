#!/usr/bin/env bash
# Build OpenOCD with remote_bitbang SWD support.
#
# OpenOCD 0.12.0 predates the remote_bitbang SWD implementation.  This script
# builds a pinned upstream commit that added it, separately from the system
# OpenOCD package.  Run it in WSL with: bash tools/build_openocd_remote_swd.sh

set -euo pipefail

readonly OPENOCD_REV="0f70c6c325785517f35bbbb9316801bef7a79d8b"
readonly PREFIX="${HOME}/.local/openocd-remote-swd"
readonly SOURCE_DIR="${HOME}/.cache/openocd-remote-swd"

sudo apt update
sudo apt install -y \
  autoconf automake build-essential git libtool pkg-config texinfo \
  libusb-1.0-0-dev

mkdir -p "$(dirname "${SOURCE_DIR}")"
if [[ ! -d "${SOURCE_DIR}/.git" ]]; then
  git clone --recurse-submodules https://github.com/openocd-org/openocd.git "${SOURCE_DIR}"
fi

git -C "${SOURCE_DIR}" fetch --tags origin
git -C "${SOURCE_DIR}" checkout --detach "${OPENOCD_REV}"
git -C "${SOURCE_DIR}" submodule update --init --recursive

cd "${SOURCE_DIR}"
./bootstrap
./configure --prefix="${PREFIX}" --enable-remote-bitbang
make -j"$(nproc)"
make install

echo
echo "Installed: ${PREFIX}/bin/openocd"
echo "Verify SWD appears in the transport list:"
echo "  ${PREFIX}/bin/openocd -c 'adapter driver remote_bitbang' -c 'transport list' -c exit"

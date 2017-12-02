# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"/vars.sh
fx-config-read

fx-machine-types() {
  echo "Available machine types:"
  echo "  ram"
  echo "  efi"
  echo "  cros"
  echo "Machine type aliases:"
  echo "  acer"
  echo "  nuc"
  echo "  pixel"
  echo "  vboot"
}

fx-ensure-vboot-tools() {
  # TODO(INTK-60): remove once futility is in //buildtools
  local vboot_reference="${FUCHSIA_DIR}/third_party/vboot_reference"
  local futility_bin="${vboot_reference}/build/futility/futility"
  local cgpt_bin="${vboot_reference}/build/cgpt/cgpt"
  export CGPT="${cgpt_bin}"

  local LDFLAGS=
  local HAVE_MACOS=0
  local OPENSSL_DIR=
  if [[ $(uname) == "Darwin" ]]; then
    HAVE_MACOS=1
    if which brew > /dev/null; then
      OPENSSL_DIR=$(dirname $(dirname $(dirname $(brew ls openssl | grep include/openssl | head -n 1))))
    fi
  else
    LDFLAGS="-lpthread"
  fi
  if [[ ! -x "${futility_bin}" ]]; then
    if [[ ! -f "/usr/include/uuid/uuid.h" ]]; then
      if [[ $(uname) == "Linux" ]]; then
        echo "Installing uuid-dev..."
        sudo apt-get install -y uuid-dev
      fi
    fi
    make -C "${vboot_reference}" LDFLAGS="${LDFLAGS}" HAVE_MACOS="${HAVE_MACOS}" OPENSSL_DIR="${OPENSSL_DIR}" "${futility_bin}"
  fi
  # TODO(raggi): patch cgpt to build on mac
  if [[ -z "$HAVE_MACOS" ]]; then
    if [[ ! -x "${cgpt_bin}" ]]; then
      make -C "${vboot_reference}" LDFLAGS="${LDFLAGS}" HAVE_MACOS="${HAVE_MACOS}" OPENSSL_DIR="${OPENSSL_DIR}" "${cgpt_bin}"
    fi
  fi
}

zircon_bin="zircon.bin"
ramdisk_bin="bootdata-blobstore-${ZIRCON_PROJECT}.bin"

images_dir="images"
cmdline_txt="${images_dir}/cmdline.txt"
efi_block="${images_dir}/efi-${ZIRCON_PROJECT}.blk"
fvm_block="${images_dir}/fvm.blk"
fvm_sparse_block="${images_dir}/fvm.sparse.blk"
kernc_vboot="${images_dir}/kernc.vboot"
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"/vars.sh
fx-config-read

fx-machine-types() {
  echo "Available machine types:"
  echo "  ram     - ramboot with /system and /pkgfs from blobs"
  echo "  netboot - alias for ram"
  echo "  bootfs  - ramboot with /system from bootfs (deprecated)"
  echo "  zedboot - ramboot zedboot into zedboot"
  echo "  efi     - pave an EFI device (e.g. nuc)"
  echo "  cros    - pave a cros device (e.g. pixelbook)"
  echo "Machine type aliases:"
  echo "  acer"
  echo "  nuc"
  echo "  pixel"
  echo "  vboot"
  echo "Available board types for ${FUCHSIA_ARCH}:"
  cat "${board_list_file}"
	echo
  echo "Note: currently efi,cros,acer,nuc,pixel and vboot all have the exact"
  echo "same behavior and target specific differences occur client side."
}

# TODO Remove ${board} variable after build system builds common images
case "${FUCHSIA_ARCH}" in
x64)
  board="pc"
  ;;
arm64)
  board="qemu"
  ;;
*)
  echo >&2 "Unknown zircon board for arch: \"$FUCHSIA_ARCH\""
  exit 1
esac

board_list_file="${FUCHSIA_BUILD_DIR}/zircon-boards.list"

zircon_bin="zircon.bin"
ramdisk_bin="bootdata-blob-${board}.bin"

images_dir="images"
cmdline_txt="${images_dir}/cmdline.txt"
efi_block="${images_dir}/local-${board}.esp.blk"
fvm_block="${images_dir}/fvm.blk"
fvm_sparse_block="${images_dir}/fvm.sparse.blk"
fvm_data_sparse_block="${images_dir}/fvm.data.sparse.blk"
kernc_vboot="${images_dir}/zircon-${board}.vboot"

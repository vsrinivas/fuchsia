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
  echo "  efi     - pave an EFI device (e.g. acer,nuc)"
  echo "  cros    - pave a cros device (e.g. pixelbook,chromebook)"
  echo "  arm     - pave an ARM device (e.g. vim2)"
  echo "Machine type aliases:"
  echo "  acer"
  echo "  nuc"
  echo "  pixel"
  echo "  vboot"
  echo
  echo "Note: currently efi,cros,acer,nuc,pixel and vboot all have the exact"
  echo "same behavior and target specific differences occur client side."
}

source "${FUCHSIA_BUILD_DIR}"/image_paths.sh
source "${FUCHSIA_BUILD_DIR}"/zedboot_image_paths.sh

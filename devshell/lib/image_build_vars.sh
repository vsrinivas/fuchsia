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

zircon_bin="zircon.bin"
ramdisk_bin="bootdata-blobstore-${ZIRCON_PROJECT}.bin"

images_dir="images"
cmdline_txt="${images_dir}/cmdline.txt"
efi_block="${images_dir}/local-${ZIRCON_PROJECT}.esp.blk"
fvm_block="${images_dir}/fvm.blk"
fvm_sparse_block="${images_dir}/fvm.sparse.blk"
fvm_data_sparse_block="${images_dir}/fvm.data.sparse.blk"
kernc_vboot="${images_dir}/zircon-${ZIRCON_PROJECT}.vboot"

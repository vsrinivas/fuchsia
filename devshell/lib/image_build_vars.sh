# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"/vars.sh
fx-config-read

images_dir="${FUCHSIA_BUILD_DIR}/gen/image_builds"
mkdir -p "${images_dir}"

system_package_dir="${images_dir}/system.pkg"
system_package_meta_far="${system_package_dir}/meta.far"

boot_manifest="${FUCHSIA_BUILD_DIR}/boot.manifest"
system_manifest="${FUCHSIA_BUILD_DIR}/system.manifest"

cmdline="${FUCHSIA_BUILD_DIR}/cmdline"

ramdisk_bin="${images_dir}/ramdisk.bin"
zircon_bin="${images_dir}/zircon.bin"

blobstore_block="${images_dir}/blobstore.blk"
data_block="${images_dir}/data.blk"
efi_block="${images_dir}/efi.blk"
fvm_block="${images_dir}/fvm.blk"
fvm_sparse_block="${images_dir}/fvm-sparse.blk"
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"/vars.sh
fx-config-read

declare -r images_dir="${FUCHSIA_BUILD_DIR}/gen/image_builds"
mkdir -p "${images_dir}"

declare -r system_package_dir="${images_dir}/system.pkg"
declare -r system_package_meta_far="${system_package_dir}/meta.far"

declare -r boot_manifest="${FUCHSIA_BUILD_DIR}/boot.manifest"
declare -r system_manifest="${FUCHSIA_BUILD_DIR}/system.manifest"

declare -r cmdline="${FUCHSIA_BUILD_DIR}/cmdline"

declare -r ramdisk_bin="${images_dir}/ramdisk.bin"
declare -r zircon_bin="${images_dir}/zircon.bin"

declare -r blobstore_block="${images_dir}/blobstore.blk"
declare -r data_block="${images_dir}/data.blk"
declare -r efi_block="${images_dir}/efi.blk"
declare -r fvm_block="${images_dir}/fvm.blk"
declare -r fvm_sparse_block="${images_dir}/fvm-sparse.blk"

// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::boot_args::BootArgs;

#[cfg(test)]
pub fn default_config() -> fshost_config::Config {
    fshost_config::Config {
        allow_legacy_data_partition_names: false,
        apply_limits_to_ramdisk: false,
        blobfs: true,
        blobfs_initial_inodes: 0,
        blobfs_max_bytes: 0,
        blobfs_use_deprecated_padded_format: false,
        bootpart: true,
        data: true,
        data_filesystem_format: String::new(),
        data_max_bytes: 0,
        disable_block_watcher: false,
        durable: false,
        factory: false,
        format_data_on_corruption: true,
        check_filesystems: true,
        fvm: true,
        fvm_ramdisk: false,
        fvm_slice_size: 1024 * 1024, // Default to 1 MiB slice size for tests.
        gpt: true,
        gpt_all: false,
        mbr: false,
        nand: false,
        netboot: false,
        no_zxcrypt: false,
        sandbox_decompression: false,
        use_disk_based_minfs_migration: false,
        use_native_fxfs_crypto: true,
        ramdisk_prefix: "/dev/sys/platform/00:00:2d/ramctl".to_owned(),
    }
}

pub fn apply_boot_args_to_config(config: &mut fshost_config::Config, boot_args: &BootArgs) {
    if boot_args.netboot() {
        config.netboot = true;
    }

    if boot_args.check_filesystems() {
        config.check_filesystems = true;
    }
}

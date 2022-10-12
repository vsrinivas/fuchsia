// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
pub fn default_config() -> fshost_config::Config {
    fshost_config::Config {
        allow_legacy_data_partition_names: false,
        apply_limits_to_ramdisk: false,
        blobfs: true,
        blobfs_max_bytes: 0,
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

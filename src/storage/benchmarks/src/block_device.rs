// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {async_trait::async_trait, std::path::Path};

/// Block device configuration options.
pub struct BlockDeviceConfig {
    /// If true, zxcrypt is initialized on top of the block device.
    pub use_zxcrypt: bool,

    /// For filesystem that are not FVM-aware, this option can be used to pre-allocate space inside
    /// of the FVM volume.
    pub fvm_volume_size: Option<u64>,
}

/// A trait representing a block device.
pub trait BlockDevice: Send {
    /// Returns a `NodeProxy` to the block device.
    fn get_path(&self) -> &Path;
}

/// A trait for constructing block devices.
#[async_trait]
pub trait BlockDeviceFactory: Send + Sync {
    /// Constructs a new block device.
    async fn create_block_device(&self, config: &BlockDeviceConfig) -> Box<dyn BlockDevice>;
}

/// A BlockDeviceFactory that panics when trying to create a block device. This is useful for
/// benchmarking filesystems that don't need to create a block device.
pub struct PanickingBlockDeviceFactory {}

impl PanickingBlockDeviceFactory {
    pub fn new() -> Self {
        Self {}
    }
}

#[async_trait]
impl BlockDeviceFactory for PanickingBlockDeviceFactory {
    async fn create_block_device(&self, _config: &BlockDeviceConfig) -> Box<dyn BlockDevice> {
        panic!("PanickingBlockDeviceFactory can't create block devices");
    }
}

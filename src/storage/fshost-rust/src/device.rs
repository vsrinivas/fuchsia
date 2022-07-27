// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    async_trait::async_trait,
    std::path::{Path, PathBuf},
};

#[derive(Clone, Copy, Eq, PartialEq)]
pub enum ContentFormat {
    #[cfg(test)]
    Unknown,
    Gpt,
    Fvm,
}

#[async_trait]
pub trait Device: Sync {
    // Returns BlockInfo (the result of calling fuchsia.hardware.block/Block.Query).
    async fn get_block_info(&self) -> Result<fidl_fuchsia_hardware_block::BlockInfo, Error>;

    // True if this is a NAND device.
    fn is_nand(&self) -> bool;

    // Returns the format as determined by content sniffing. This should be used sparingly when
    // other means of determining the format are not possible.
    async fn content_format(&self) -> Result<ContentFormat, Error>;

    // Returns the topological path.
    async fn topological_path(&self) -> Result<&Path, Error>;

    // If this device is a partition, this returns the label. Otherwise, an error is returned.
    async fn partition_label(&self) -> Result<&str, Error>;

    // If this device is a partition, this returns the type GUID. Otherwise, an error is returned.
    async fn partition_type(&self) -> Result<&[u8; 16], Error>;
}

/// A block device.
#[derive(Clone, Debug)]
pub struct BlockDevice {
    path: PathBuf,
}

impl BlockDevice {
    pub fn new(path: PathBuf) -> Self {
        Self { path }
    }

    #[allow(unused)]
    pub fn path(&self) -> &Path {
        &self.path
    }
}

#[async_trait]
impl Device for BlockDevice {
    async fn get_block_info(&self) -> Result<fidl_fuchsia_hardware_block::BlockInfo, Error> {
        todo!();
    }

    fn is_nand(&self) -> bool {
        false
    }

    async fn content_format(&self) -> Result<ContentFormat, Error> {
        todo!();
    }

    async fn topological_path(&self) -> Result<&Path, Error> {
        todo!();
    }

    async fn partition_label(&self) -> Result<&str, Error> {
        todo!();
    }

    async fn partition_type(&self) -> Result<&[u8; 16], Error> {
        todo!();
    }
}

// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod constants;

use {
    self::constants::{FVM_MAGIC, GPT_MAGIC},
    anyhow::{anyhow, Error},
    async_trait::async_trait,
    fidl::endpoints::{create_endpoints, Proxy},
    fidl_fuchsia_device::ControllerMarker,
    fidl_fuchsia_hardware_block::BlockProxy,
    fidl_fuchsia_hardware_block_volume::VolumeAndNodeProxy,
    fidl_fuchsia_io::OpenFlags,
    fuchsia_component::client::connect_to_protocol_at_path,
    fuchsia_zircon::{self as zx, HandleBased},
};

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ContentFormat {
    Unknown,
    Gpt,
    Fvm,
}

#[async_trait]
pub trait Device: Send + Sync {
    /// Returns BlockInfo (the result of calling fuchsia.hardware.block/Block.Query).
    async fn get_block_info(&self) -> Result<fidl_fuchsia_hardware_block::BlockInfo, Error>;

    /// True if this is a NAND device.
    fn is_nand(&self) -> bool;

    /// Returns the format as determined by content sniffing. This should be used sparingly when
    /// other means of determining the format are not possible.
    async fn content_format(&mut self) -> Result<ContentFormat, Error>;

    /// Returns the topological path.
    fn topological_path(&self) -> &str;

    /// If this device is a partition, this returns the label. Otherwise, an error is returned.
    async fn partition_label(&mut self) -> Result<&str, Error>;

    /// If this device is a partition, this returns the type GUID. Otherwise, an error is returned.
    async fn partition_type(&mut self) -> Result<&[u8; 16], Error>;

    /// Returns a proxy for the device.
    fn proxy(&self) -> Result<BlockProxy, Error>;
}

/// A block device.
#[derive(Clone, Debug)]
pub struct BlockDevice {
    // The path of the device in /dev/class/.
    path: String,

    // The topological path.
    topological_path: String,

    // The proxy for the device.  N.B. The device might not support the volume protocol or the
    // composed partition protocol, but it should support the block and node protocols.
    volume_proxy: VolumeAndNodeProxy,

    // Memoized fields.
    content_format: Option<ContentFormat>,
    partition_label: Option<String>,
    partition_type: Option<[u8; 16]>,
}

impl BlockDevice {
    pub async fn new(path: impl ToString) -> Result<Self, Error> {
        let path = path.to_string();
        let device_proxy = connect_to_protocol_at_path::<ControllerMarker>(&path)?;
        let topological_path =
            device_proxy.get_topological_path().await?.map_err(zx::Status::from_raw)?;
        Ok(Self {
            path: path.to_string(),
            topological_path,
            volume_proxy: VolumeAndNodeProxy::new(device_proxy.into_channel().unwrap()),
            content_format: None,
            partition_label: None,
            partition_type: None,
        })
    }

    #[allow(unused)]
    pub fn path(&self) -> &str {
        &self.path
    }
}

#[async_trait]
impl Device for BlockDevice {
    async fn get_block_info(&self) -> Result<fidl_fuchsia_hardware_block::BlockInfo, Error> {
        let (status, info) = self.volume_proxy.get_info().await?;
        zx::Status::ok(status)?;
        info.ok_or(anyhow!("Expected BlockInfo")).map(|i| *i)
    }

    fn is_nand(&self) -> bool {
        false
    }

    async fn content_format(&mut self) -> Result<ContentFormat, Error> {
        if let Some(format) = self.content_format {
            return Ok(format);
        }
        let vmo = zx::Vmo::create(8192)?;
        let status = self
            .volume_proxy
            .read_blocks(vmo.duplicate_handle(zx::Rights::SAME_RIGHTS)?, 8192, 0, 0)
            .await?;
        zx::Status::ok(status)?;
        let mut data = [0; 16];
        vmo.read(&mut data, 0)?;
        self.content_format = Some({
            if &data[..8] == &FVM_MAGIC {
                ContentFormat::Fvm
            } else if &data[..16] == &GPT_MAGIC {
                ContentFormat::Gpt
            } else {
                ContentFormat::Unknown
            }
        });
        Ok(self.content_format.unwrap())
    }

    fn topological_path(&self) -> &str {
        &self.topological_path
    }

    async fn partition_label(&mut self) -> Result<&str, Error> {
        if self.partition_label.is_none() {
            let (status, name) = self.volume_proxy.get_name().await?;
            zx::Status::ok(status)?;
            self.partition_label = Some(name.ok_or(anyhow!("Expected name"))?);
        }
        Ok(self.partition_label.as_ref().unwrap())
    }

    async fn partition_type(&mut self) -> Result<&[u8; 16], Error> {
        if self.partition_type.is_none() {
            let (status, partition_type) = self.volume_proxy.get_type_guid().await?;
            zx::Status::ok(status)?;
            self.partition_type = Some(partition_type.ok_or(anyhow!("Expected type"))?.value);
        }
        Ok(self.partition_type.as_ref().unwrap())
    }

    fn proxy(&self) -> Result<BlockProxy, Error> {
        let (client, server) = create_endpoints()?;
        self.volume_proxy.clone(OpenFlags::CLONE_SAME_RIGHTS, server)?;
        Ok(BlockProxy::new(fidl::AsyncChannel::from_channel(client.into_channel())?))
    }
}

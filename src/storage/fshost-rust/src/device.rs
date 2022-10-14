// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod constants;

use {
    anyhow::{anyhow, Context, Error},
    async_trait::async_trait,
    fidl::endpoints::{create_endpoints, Proxy},
    fidl_fuchsia_device::ControllerMarker,
    fidl_fuchsia_hardware_block::BlockProxy,
    fidl_fuchsia_hardware_block_volume::VolumeAndNodeProxy,
    fidl_fuchsia_io::OpenFlags,
    fs_management::format::{detect_disk_format, DiskFormat},
    fuchsia_component::client::connect_to_protocol_at_path,
    fuchsia_zircon::{self as zx},
};

#[async_trait]
pub trait Device: Send + Sync {
    /// Returns BlockInfo (the result of calling fuchsia.hardware.block/Block.Query).
    async fn get_block_info(&self) -> Result<fidl_fuchsia_hardware_block::BlockInfo, Error>;

    /// True if this is a NAND device.
    fn is_nand(&self) -> bool;

    /// Returns the format as determined by content sniffing. This should be used sparingly when
    /// other means of determining the format are not possible.
    async fn content_format(&mut self) -> Result<DiskFormat, Error>;

    /// Returns the topological path.
    fn topological_path(&self) -> &str;

    /// If this device is a partition, this returns the label. Otherwise, an error is returned.
    async fn partition_label(&mut self) -> Result<&str, Error>;

    /// If this device is a partition, this returns the type GUID. Otherwise, an error is returned.
    async fn partition_type(&mut self) -> Result<&[u8; 16], Error>;

    /// Returns a proxy for the device.
    fn proxy(&self) -> Result<BlockProxy, Error>;

    /// Returns a new Device, which is a child of this device with the specified suffix. This
    /// function will return when the device is available. This function assumes the child device
    /// will show up in /dev/class/block.
    async fn get_child(&self, suffix: &str) -> Result<Box<dyn Device>, Error>;
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
    content_format: Option<DiskFormat>,
    partition_label: Option<String>,
    partition_type: Option<[u8; 16]>,
}

impl BlockDevice {
    pub async fn new(path: impl ToString) -> Result<Self, Error> {
        let path = path.to_string();
        let device_proxy = connect_to_protocol_at_path::<ControllerMarker>(&path)?;
        let topological_path =
            device_proxy.get_topological_path().await?.map_err(zx::Status::from_raw)?;
        Ok(Self::from_proxy(
            VolumeAndNodeProxy::new(device_proxy.into_channel().unwrap()),
            path,
            topological_path,
        ))
    }

    pub fn from_proxy(
        volume_proxy: VolumeAndNodeProxy,
        path: impl ToString,
        topological_path: impl ToString,
    ) -> Self {
        Self {
            path: path.to_string(),
            topological_path: topological_path.to_string(),
            volume_proxy,
            content_format: None,
            partition_label: None,
            partition_type: None,
        }
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

    async fn content_format(&mut self) -> Result<DiskFormat, Error> {
        if let Some(format) = self.content_format {
            return Ok(format);
        }

        let block_proxy = self.proxy().context("Failed to get proxy")?;
        return Ok(detect_disk_format(&block_proxy).await);
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

    async fn get_child(&self, suffix: &str) -> Result<Box<dyn Device>, Error> {
        const DEV_CLASS_BLOCK: &str = "/dev/class/block";
        let dev_class_block = fuchsia_fs::directory::open_in_namespace(
            DEV_CLASS_BLOCK,
            OpenFlags::RIGHT_READABLE | OpenFlags::RIGHT_WRITABLE,
        )?;
        let child_path = device_watcher::wait_for_device_with(
            &dev_class_block,
            |device_watcher::DeviceInfo { filename, topological_path }| {
                topological_path.strip_suffix(suffix).and_then(|topological_path| {
                    (topological_path == self.topological_path)
                        .then(|| format!("{}/{}", DEV_CLASS_BLOCK, filename))
                })
            },
        )
        .await?;
        let block_device = BlockDevice::new(child_path).await?;
        Ok(Box::new(block_device))
    }
}

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

    async fn content_format(&mut self) -> Result<ContentFormat, Error> {
        if let Some(format) = self.content_format {
            return Ok(format);
        }
        let info = self.get_block_info().await?;
        let size = info.block_size as u64 * 2;
        let vmo = zx::Vmo::create(size)?;
        let status = self
            .volume_proxy
            .read_blocks(vmo.duplicate_handle(zx::Rights::SAME_RIGHTS)?, size, 0, 0)
            .await?;
        zx::Status::ok(status)?;
        let mut data = vec![0; size as usize];
        vmo.read(&mut data, 0)?;
        self.content_format = Some({
            if &data[..8] == &FVM_MAGIC {
                ContentFormat::Fvm
            } else if &data[info.block_size as usize..info.block_size as usize + 16] == &GPT_MAGIC {
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

#[cfg(test)]
mod tests {
    use {
        super::{BlockDevice, ContentFormat, Device, FVM_MAGIC, GPT_MAGIC},
        fidl::endpoints::create_proxy_and_stream,
        fidl_fuchsia_hardware_block::BlockInfo,
        fidl_fuchsia_hardware_block_volume::{VolumeAndNodeMarker, VolumeAndNodeRequest},
        fuchsia_zircon as zx,
        futures::{pin_mut, select, FutureExt, TryStreamExt},
    };

    async fn get_content_format(content: &[u8]) -> ContentFormat {
        let (proxy, mut stream) = create_proxy_and_stream::<VolumeAndNodeMarker>().unwrap();

        let mock_device = async {
            while let Some(request) = stream.try_next().await.unwrap() {
                match request {
                    VolumeAndNodeRequest::GetInfo { responder } => {
                        responder
                            .send(
                                zx::sys::ZX_OK,
                                Some(&mut BlockInfo {
                                    block_count: 1000,
                                    block_size: 512,
                                    max_transfer_size: 1024 * 1024,
                                    flags: 0,
                                    reserved: 0,
                                }),
                            )
                            .unwrap();
                    }
                    VolumeAndNodeRequest::ReadBlocks {
                        vmo,
                        length,
                        dev_offset,
                        vmo_offset,
                        responder,
                    } => {
                        assert_eq!(dev_offset, 0);
                        assert_eq!(length, 1024);
                        vmo.write(content, vmo_offset).unwrap();
                        responder.send(zx::sys::ZX_OK).unwrap();
                    }
                    _ => unreachable!(),
                }
            }
        }
        .fuse();

        pin_mut!(mock_device);

        select! {
            _ = mock_device => unreachable!(),
            format = async {
                let mut device = BlockDevice::from_proxy(
                    proxy, "/mock_device", "/mock_device_topo_path");
                device.content_format().await.expect("content_format failed")
            }.fuse() => return format,
        }
    }

    #[fuchsia::test]
    async fn content_format_gpt() {
        let mut data = vec![0; 1024];
        data[512..512 + GPT_MAGIC.len()].copy_from_slice(&GPT_MAGIC);
        assert_eq!(get_content_format(&data).await, ContentFormat::Gpt);
    }

    #[fuchsia::test]
    async fn content_format_fvm() {
        let mut data = vec![0; 1024];
        data[0..0 + FVM_MAGIC.len()].copy_from_slice(&FVM_MAGIC);
        assert_eq!(get_content_format(&data).await, ContentFormat::Fvm);
    }

    #[fuchsia::test]
    async fn content_format_unknown() {
        assert_eq!(get_content_format(&vec![0; 1024]).await, ContentFormat::Unknown);
    }
}

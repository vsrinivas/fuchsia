// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context, Error},
    fdio,
    fidl::endpoints::Proxy,
    fidl_fuchsia_device::ControllerProxy,
    fidl_fuchsia_hardware_block::BlockProxy,
    fuchsia_zircon as zx, fuchsia_zircon_status as zx_status,
    std::{fs, path::Path},
};

async fn connect_to_service(path: &str) -> Result<fidl::AsyncChannel, Error> {
    let (local, remote) = zx::Channel::create().context("Creating channel")?;
    fdio::service_connect(path, remote).context("Connecting to service")?;
    let local = fidl::AsyncChannel::from_channel(local).context("Creating AsyncChannel")?;
    Ok(local)
}

async fn block_device_get_info(
    block_channel: fidl::AsyncChannel,
) -> Result<Option<(String, u64)>, Error> {
    // Figure out topological path of the block device, so we can guess if it's a disk or a
    // partition.
    let (maybe_path, block_channel) = get_topological_path(block_channel).await?;
    let topo_path = maybe_path.ok_or(anyhow!("Failed to get topo path for device"))?;

    if topo_path.contains("/ramdisk-") {
        // This is probably ram, skip it
        return Ok(None);
    }

    let block = BlockProxy::from_channel(block_channel);
    let (status, maybe_info) = block.get_info().await?;
    if let Some(info) = maybe_info {
        let blocks = info.block_count;
        let block_size = info.block_size as u64;
        return Ok(Some((topo_path, blocks * block_size)));
    }

    return Err(Error::new(zx_status::Status::from_raw(status)));
}

// There's no nice way to use a service without losing the channel,
// so this function returns the controller.
async fn get_topological_path(
    channel: fidl::AsyncChannel,
) -> Result<(Option<String>, fidl::AsyncChannel), Error> {
    let controller = ControllerProxy::from_channel(channel);
    let topo_resp = controller.get_topological_path().await.context("Getting topological path")?;
    Ok((topo_resp.ok(), controller.into_channel().unwrap()))
}

#[derive(Debug, PartialEq, Clone)]
pub struct BlockDevice {
    /// Topological path of the block device.
    pub topo_path: String,
    /// Path to the block device under /dev/class/block.
    pub class_path: String,
    /// Size of the block device, in bytes.
    pub size: u64,
}

impl BlockDevice {
    /// Returns true if this block device is a disk.
    pub fn is_disk(&self) -> bool {
        // partitions have paths like this:
        // /dev/sys/platform/pci/00:14.0/xhci/usb-bus/001/001/ifc-000/ums/lun-000/block/part-000/block
        // while disks are like this:
        // /dev/sys/platform/pci/00:17.0/ahci/sata2/block
        !self.topo_path.contains("/block/part-")
    }
}

pub async fn get_block_device(class_path: String) -> Result<Option<BlockDevice>, Error> {
    let block_channel = connect_to_service(&class_path).await?;
    let result = block_device_get_info(block_channel).await.context("Getting block device info")?;
    Ok(result.map(|(topo_path, size)| BlockDevice { topo_path, class_path, size }))
}

pub async fn get_block_devices() -> Result<Vec<BlockDevice>, Error> {
    let block_dir = Path::new("/dev/class/block");
    let mut devices = Vec::new();
    for entry in fs::read_dir(block_dir)? {
        let name = entry?.path().to_str().unwrap().to_owned();
        if let Some(bd) = get_block_device(name.clone()).await? {
            devices.push(bd);
        } else {
            println!("Bad disk: {:?}", name);
        }
    }
    Ok(devices)
}

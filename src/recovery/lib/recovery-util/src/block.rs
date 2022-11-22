// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context as _, Error},
    fidl::endpoints::Proxy,
    fidl_fuchsia_device::ControllerProxy,
    fidl_fuchsia_hardware_block::{BlockMarker, BlockProxy},
    fuchsia_zircon as zx,
};

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

pub async fn get_block_device(class_path: &str) -> Result<Option<BlockDevice>, Error> {
    let block = fuchsia_component::client::connect_to_protocol_at_path::<BlockMarker>(class_path)?;
    // TODO(https://fxbug.dev/112484): this relies on multiplexing.
    let channel = block
        .into_channel()
        .map_err(|_: BlockProxy| anyhow!("could not get channel from proxy"))?;
    let controller = ControllerProxy::from_channel(channel);
    let topo_path = controller
        .get_topological_path()
        .await
        .context("FIDL: get_topological_path()")?
        .map_err(zx::Status::from_raw)
        .context("response: get_topological_path()")?;
    if topo_path.contains("/ramdisk-") {
        // This is probably ram, skip it
        Ok(None)
    } else {
        let channel = controller
            .into_channel()
            .map_err(|_: ControllerProxy| anyhow!("could not get channel from proxy"))?;
        let block = BlockProxy::from_channel(channel);
        let info = block
            .get_info()
            .await
            .context("FIDL: get_info()")?
            .map_err(zx::Status::from_raw)
            .context("response: get_info()")?;
        let block_count = info.block_count;
        let block_size = info.block_size;
        let size = block_count.checked_mul(block_size.into()).ok_or_else(|| {
            anyhow!("device size overflow: block_count={} block_size={}", block_count, block_size)
        })?;
        let class_path = class_path.to_owned();
        Ok(Some(BlockDevice { topo_path, class_path, size }))
    }
}

pub async fn get_block_devices() -> Result<Vec<BlockDevice>, Error> {
    const BLOCK_DIR: &str = "/dev/class/block";
    let entries = std::fs::read_dir(BLOCK_DIR)?;
    let futures = entries.map(|entry| async {
        let entry = entry?;
        let path = entry.path();
        let class_path =
            path.to_str().ok_or_else(|| anyhow!("path contains non-UTF8: {}", path.display()))?;
        get_block_device(class_path).await
    });
    let options = futures::future::try_join_all(futures).await?;
    let devices = options.into_iter().filter_map(std::convert::identity).collect();
    Ok(devices)
}

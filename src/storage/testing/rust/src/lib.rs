// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context, Result},
    device_watcher::recursive_wait_and_open_node,
    fidl_fuchsia_device::ControllerProxy,
    fidl_fuchsia_hardware_block_partition::{PartitionMarker, PartitionProxy},
    fidl_fuchsia_io as fio,
    fuchsia_component::client::connect_to_protocol_at_path,
    fuchsia_vfs_watcher::{WatchEvent, Watcher},
    fuchsia_zircon as zx,
    futures::TryStreamExt,
    std::path::{Path, PathBuf},
};

pub mod fvm;
pub mod zxcrypt;

pub type Guid = [u8; 16];

pub fn create_random_guid() -> Guid {
    *uuid::Uuid::new_v4().as_bytes()
}

pub async fn bind_fvm(proxy: &ControllerProxy) -> Result<()> {
    fvm::bind_fvm_driver(proxy).await
}

/// Waits for the ramctl device to be ready.
pub async fn wait_for_ramctl() -> Result<()> {
    let dev = fuchsia_fs::directory::open_in_namespace(
        "/dev",
        fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
    )?;
    recursive_wait_and_open_node(&dev, "sys/platform/00:00:2d/ramctl").await?;
    Ok(())
}

async fn partition_type_guid_matches(guid: &Guid, partition: &PartitionProxy) -> Result<bool> {
    let (status, type_guid) = partition.get_type_guid().await?;
    zx::ok(status).context("Failed to get type guid")?;
    let type_guid = if let Some(guid) = type_guid { guid } else { return Ok(false) };
    Ok(type_guid.value == *guid)
}

async fn partition_instance_guid_matches(guid: &Guid, partition: &PartitionProxy) -> Result<bool> {
    let (status, instance_guid) = partition.get_instance_guid().await?;
    zx::ok(status).context("Failed to get instance guid")?;
    let instance_guid = if let Some(guid) = instance_guid { guid } else { return Ok(false) };
    Ok(instance_guid.value == *guid)
}

async fn partition_name_matches(name: &str, partition: &PartitionProxy) -> Result<bool> {
    let (status, partition_name) = partition.get_name().await?;
    zx::ok(status).context("Failed to get partition name")?;
    let partition_name = if let Some(name) = partition_name { name } else { return Ok(false) };
    Ok(partition_name == name)
}

/// A constraint for the block device being waited for in `wait_for_block_device`.
pub enum BlockDeviceMatcher<'a> {
    /// Only matches block devices that have this type Guid.
    TypeGuid(&'a Guid),

    /// Only matches block devices that have this instance Guid.
    InstanceGuid(&'a Guid),

    /// Only matches block devices that have this name.
    Name(&'a str),
}

impl BlockDeviceMatcher<'_> {
    async fn matches(&self, partition: &PartitionProxy) -> Result<bool> {
        match self {
            Self::TypeGuid(guid) => partition_type_guid_matches(guid, partition).await,
            Self::InstanceGuid(guid) => partition_instance_guid_matches(guid, partition).await,
            Self::Name(name) => partition_name_matches(name, partition).await,
        }
    }
}

/// Waits for a block device to appear in `/dev/class/block` that meets all of the requirements of
/// `matchers`. Returns the path to the matched block device.
pub async fn wait_for_block_device(matchers: &[BlockDeviceMatcher<'_>]) -> Result<PathBuf> {
    const DEV_CLASS_BLOCK: &str = "/dev/class/block";
    assert!(!matchers.is_empty());
    let block_dev_dir = fuchsia_fs::directory::open_in_namespace(
        DEV_CLASS_BLOCK,
        fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
    )?;
    let mut watcher = Watcher::new(Clone::clone(&block_dev_dir)).await?;
    while let Some(msg) = watcher.try_next().await? {
        if msg.event != WatchEvent::ADD_FILE && msg.event != WatchEvent::EXISTING {
            continue;
        }
        let path = Path::new(DEV_CLASS_BLOCK).join(msg.filename);
        let partition = connect_to_protocol_at_path::<PartitionMarker>(path.to_str().unwrap())?;
        let mut matches = true;
        for matcher in matchers {
            if !matcher.matches(&partition).await.unwrap_or(false) {
                matches = false;
                break;
            }
        }
        if matches {
            return Ok(path);
        }
    }
    Err(anyhow!("Failed to wait for block device"))
}

#[cfg(test)]
mod tests {
    use {
        super::*, fidl_fuchsia_hardware_block_volume::ALLOCATE_PARTITION_FLAG_INACTIVE,
        ramdevice_client::RamdiskClient,
    };
    const BLOCK_SIZE: u64 = 512;
    const BLOCK_COUNT: u64 = 64 * 1024 * 1024 / BLOCK_SIZE;
    const FVM_SLICE_SIZE: usize = 1024 * 1024;
    const INSTANCE_GUID: Guid = [
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e,
        0x0f,
    ];
    const TYPE_GUID: Guid = [
        0x00, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80, 0x90, 0xa0, 0xb0, 0xc0, 0xd0, 0xe0,
        0xf0,
    ];
    const VOLUME_NAME: &str = "volume-name";

    #[fuchsia::test]
    async fn wait_for_block_device_with_all_match_criteria() {
        wait_for_ramctl().await.unwrap();
        let ramdisk = RamdiskClient::create(BLOCK_SIZE, BLOCK_COUNT).unwrap();
        let fvm = fvm::set_up_fvm(Path::new(ramdisk.get_path()), FVM_SLICE_SIZE)
            .await
            .expect("Failed to format ramdisk with FVM");
        fvm::create_fvm_volume(
            &fvm,
            VOLUME_NAME,
            &TYPE_GUID,
            &INSTANCE_GUID,
            None,
            ALLOCATE_PARTITION_FLAG_INACTIVE,
        )
        .await
        .expect("Failed to create fvm volume");

        wait_for_block_device(&[
            BlockDeviceMatcher::TypeGuid(&TYPE_GUID),
            BlockDeviceMatcher::InstanceGuid(&INSTANCE_GUID),
            BlockDeviceMatcher::Name(VOLUME_NAME),
        ])
        .await
        .expect("Failed to find block device");
    }
}

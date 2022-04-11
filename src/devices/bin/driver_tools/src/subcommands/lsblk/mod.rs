// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod args;
mod guids;

use {
    super::common,
    anyhow::{format_err, Result},
    args::LsblkCommand,
    async_trait::async_trait,
    fidl::endpoints::Proxy,
    fidl_fuchsia_developer_remotecontrol as fremotecontrol, fidl_fuchsia_device as fdevice,
    fidl_fuchsia_hardware_block as fblock, fidl_fuchsia_hardware_block_partition as fpartition,
    fidl_fuchsia_hardware_skipblock as fskipblock, fidl_fuchsia_io as fio,
    fuchsia_async::futures::TryStreamExt,
    fuchsia_zircon_status as zx,
    std::fmt,
    std::path::Path,
};

pub async fn lsblk(
    remote_control: fremotecontrol::RemoteControlProxy,
    cmd: LsblkCommand,
) -> Result<()> {
    let dev = common::get_devfs_proxy(remote_control, cmd.select).await?;
    println!(
        "{:<3} {:<4} {:<16} {:<20} {:<6} {}",
        "ID", "SIZE", "TYPE", "LABEL", "FLAGS", "DEVICE"
    );

    if let Ok(block_dir) =
        io_util::open_directory(&dev, &Path::new("class/block"), fio::OpenFlags::RIGHT_READABLE)
    {
        for device in get_devices::<BlockDevice>(&block_dir).await? {
            println!("{}", device);
        }
    } else {
        println!("Error opening /dev/class/block");
    }

    if let Ok(skip_block_dir) = io_util::open_directory(
        &dev,
        &Path::new("class/skip-block"),
        fio::OpenFlags::RIGHT_READABLE,
    ) {
        for device in get_devices::<SkipBlockDevice>(&skip_block_dir).await? {
            println!("{}", device);
        }
    } else {
        println!("Error opening /dev/class/skip-block");
    }
    Ok(())
}

#[async_trait]
trait New {
    type Output;
    async fn new(name: &str, node: fio::NodeProxy) -> Result<Self::Output>;
}

async fn get_devices<DeviceType: New + New<Output = DeviceType>>(
    dir: &fio::DirectoryProxy,
) -> Result<Vec<DeviceType>> {
    let mut watcher = fuchsia_vfs_watcher::Watcher::new(io_util::clone_directory(
        dir,
        fio::OpenFlags::RIGHT_READABLE,
    )?)
    .await?;
    let mut devices = Vec::new();
    while let Some(msg) = watcher.try_next().await? {
        if msg.event == fuchsia_vfs_watcher::WatchEvent::IDLE {
            return Ok(devices);
        }
        if msg.event != fuchsia_vfs_watcher::WatchEvent::EXISTING
            && msg.event != fuchsia_vfs_watcher::WatchEvent::ADD_FILE
        {
            continue;
        }
        let device = io_util::open_node(
            dir,
            &msg.filename,
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
            fio::MODE_TYPE_SERVICE,
        )?;
        let device = DeviceType::new(msg.filename.to_str().unwrap(), device).await?;
        devices.push(device);
    }
    unreachable!();
}

fn size_to_string(size: u64) -> String {
    const KILOBYTE: u64 = 1u64 << 10;
    const MEGABYTE: u64 = 1u64 << 20;
    const GIGABYTE: u64 = 1u64 << 30;
    const TERABYTE: u64 = 1u64 << 40;
    // We don't use pattern matching due to lack of support for exclusive ranges in pattern
    // matching.
    let (unit, div) = if (0..KILOBYTE).contains(&size) {
        ("", 1)
    } else if (KILOBYTE..MEGABYTE).contains(&size) {
        ("K", KILOBYTE)
    } else if (MEGABYTE..GIGABYTE).contains(&size) {
        ("M", MEGABYTE)
    } else if (GIGABYTE..TERABYTE).contains(&size) {
        ("G", GIGABYTE)
    } else {
        ("T", TERABYTE)
    };
    format!("{}{}", size / div, unit)
}

fn type_guid_to_name(type_guid: &[u8; 16]) -> String {
    // We get d1-d3 in little endian when in array form, but uuid::Uuid expects them in big endian
    // when in array form. This bit shifting rearranges the bytes around.
    let d1: u32 = (type_guid[0] as u32) << 24
        | (type_guid[1] as u32) << 16
        | (type_guid[2] as u32) << 8
        | type_guid[3] as u32;
    let d2: u16 = (type_guid[4] as u16) << 8 | type_guid[5] as u16;
    let d3: u16 = (type_guid[6] as u16) << 8 | type_guid[7] as u16;
    let guid = uuid::Uuid::from_fields_le(d1, d2, d3, &type_guid[8..]).unwrap();
    guids::TYPE_GUID_TO_NAME.get(&guid).unwrap_or(&"").to_string()
}

struct BlockDevice {
    name: String,
    topological_path: String,
    size: String,
    partition_type: String,
    partition_name: String,
    flags: String,
}

#[async_trait]
impl New for BlockDevice {
    type Output = BlockDevice;
    async fn new(name: &str, node: fio::NodeProxy) -> Result<BlockDevice> {
        let name = name.to_string();
        let controller = fdevice::ControllerProxy::new(node.into_channel().unwrap());
        let topological_path = controller
            .get_topological_path()
            .await?
            .map_err(|raw| format_err!("zx error: {}", zx::Status::from_raw(raw)))?;

        let block = fblock::BlockProxy::new(controller.into_channel().unwrap());
        let (status, info) = block.get_info().await?;
        zx::Status::ok(status)?;
        let info = info.unwrap();
        let size = size_to_string(info.block_size as u64 * info.block_count);

        let partition = fpartition::PartitionProxy::new(block.into_channel().unwrap());
        let partition_name = match partition.get_name().await {
            Ok((_status, Some(partition_name))) => partition_name,
            _ => "".to_string(),
        };
        let partition_type = match partition.get_type_guid().await {
            Ok((_status, Some(partition_type))) => type_guid_to_name(&partition_type.value),
            _ => "".to_string(),
        };

        let mut flags = String::new();
        if info.flags & fblock::FLAG_READONLY == fblock::FLAG_READONLY {
            flags.push_str("RO ")
        }
        if info.flags & fblock::FLAG_REMOVABLE == fblock::FLAG_REMOVABLE {
            flags.push_str("RE ")
        }
        if info.flags & fblock::FLAG_BOOTPART == fblock::FLAG_BOOTPART {
            flags.push_str("BP ")
        }

        Ok(BlockDevice { name, topological_path, size, partition_type, partition_name, flags })
    }
}

impl fmt::Display for BlockDevice {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{:<3.3} {:<4.4} {:<16.16} {:20.20} {:<6.6} {}",
            self.name,
            self.size,
            self.partition_type,
            self.partition_name,
            self.flags,
            self.topological_path
        )
    }
}

struct SkipBlockDevice {
    name: String,
    topological_path: String,
    size: String,
    partition_type: String,
}

#[async_trait]
impl New for SkipBlockDevice {
    type Output = SkipBlockDevice;
    async fn new(name: &str, node: fio::NodeProxy) -> Result<SkipBlockDevice> {
        let name = name.to_string();
        let controller = fdevice::ControllerProxy::new(node.into_channel().unwrap());
        let topological_path = controller
            .get_topological_path()
            .await?
            .map_err(|raw| format_err!("zx error: {}", zx::Status::from_raw(raw)))?;

        let skip_block = fskipblock::SkipBlockProxy::new(controller.into_channel().unwrap());
        let (size, partition_type) = match skip_block.get_partition_info().await {
            Ok((status, info)) if zx::Status::ok(status).is_ok() => {
                let size = size_to_string(
                    info.block_size_bytes as u64 * info.partition_block_count as u64,
                );
                let partition_type = type_guid_to_name(&info.partition_guid);
                (size, partition_type)
            }
            _ => ("".to_string(), "".to_string()),
        };

        Ok(SkipBlockDevice { name, topological_path, size, partition_type })
    }
}

impl fmt::Display for SkipBlockDevice {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{:<3.3} {:<4.4} {:<16.16} {:20.20} {:<6.6} {}",
            self.name, self.size, self.partition_type, "", "", self.topological_path
        )
    }
}

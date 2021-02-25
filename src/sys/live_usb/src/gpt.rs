// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::block_wrapper::{FileLike, WrappedBlockDevice},
    anyhow::{Context, Error},
    fuchsia_syslog::fx_log_info,
    gpt::{
        disk::LogicalBlockSize,
        partition_types::{OperatingSystem, Type as PartitionType},
        GptConfig,
    },
    std::collections::BTreeMap,
};

/// Size of the misc partition on the ramdisk, in bytes.
pub const MISC_SIZE: u64 = 64 * 1024;
/// Minimum amount of extra size the FVM needs beyond the size of the sparse FVM.
/// This number is not exact, but should be enough to account for extra overhead from the FVM
/// format, and a little bit of space for minfs.
pub const FVM_MINIMUM_PADDING: u64 = 32 * 1024 * 1024;

const FVM_TYPE_GUID: &str = "41d0e340-57e3-954e-8c1e-17ecac44cff5";
const MISC_TYPE_GUID: &str = "1d75395d-f2c6-476b-a8b7-45cc1c97b476";

pub fn write_ramdisk<'a, T: FileLike + 'a>(ramdisk: T) -> Result<gpt::DiskDeviceObject<'a>, Error> {
    let wrapper = Box::new(WrappedBlockDevice::new(ramdisk, 512));
    let mut disk = GptConfig::new()
        .writable(true)
        .initialized(false)
        .logical_block_size(LogicalBlockSize::Lb512)
        .create_from_device(wrapper, None)?;
    disk.update_partitions(BTreeMap::new()).context("initialising disk")?;

    let misc_type =
        PartitionType { guid: MISC_TYPE_GUID, os: OperatingSystem::Custom("Fuchsia".to_owned()) };
    let fvm_type =
        PartitionType { guid: FVM_TYPE_GUID, os: OperatingSystem::Custom("Fuchsia".to_owned()) };

    disk.write_inplace().context("writing")?;

    let sectors = disk.find_free_sectors();
    assert!(sectors.len() == 1);
    let block_size: u64 = disk.logical_block_size().clone().into();
    let available_space = block_size * sectors[0].1 as u64;
    disk.add_partition("fuchsia-fvm", available_space - MISC_SIZE, fvm_type, 0)
        .context("adding fvm partition")?;
    disk.add_partition("misc", MISC_SIZE, misc_type, 0).context("adding misc partition")?;
    fx_log_info!("free sectors: {:?}", disk.find_free_sectors());
    fx_log_info!("partitions: {:?}", disk.partitions());
    fx_log_info!("disk header: {:?}", disk.primary_header().unwrap());

    Ok(disk.write().context("Writing GPT")?)
}

#[cfg(test)]
mod tests {
    use {super::*, std::io::Cursor};

    #[ignore]
    // TODO(simonshields): enable this once https://github.com/Quyzi/gpt/pull/67 is in the Fuchsia tree.
    #[fuchsia::test]
    async fn test_write_ramdisk() {
        // 512K disk
        let mut fake_disk = Vec::with_capacity(512 * 1024);
        fake_disk.resize(512 * 1024, 0);

        let cursor = Cursor::new(fake_disk);
        let disk = write_ramdisk(Box::new(cursor)).unwrap();

        let gpt = GptConfig::new()
            .writable(false)
            .initialized(true)
            .logical_block_size(LogicalBlockSize::Lb512)
            .open_from_device(Box::new(disk))
            .expect("parse GPT ok");

        let partitions = gpt.partitions();

        assert_eq!(partitions.len(), 2);
        let fvm = partitions.get(&0).unwrap();
        let header = gpt.primary_header().unwrap();
        assert_eq!(fvm.first_lba, 37);
        assert_eq!(fvm.last_lba, header.last_usable - (1 + MISC_SIZE / 512));
        //assert_eq!(fvm.part_type_guid.guid, Cow::Owned(FVM_TYPE_GUID.to_owned().to_uppercase()) as Cow<'_, str>);

        let misc = partitions.get(&1).unwrap();
        assert_eq!(misc.first_lba, fvm.last_lba + 1);
        assert_eq!(misc.last_lba, header.last_usable);
        //assert_eq!(misc.part_type_guid.guid, Cow::Owned(MISC_TYPE_GUID.to_owned().to_uppercase()) as Cow<'_, str>);
    }
}

// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::Guid,
    anyhow::{Context, Result},
    device_watcher::recursive_wait_and_open_node,
    fidl_fuchsia_device::{ControllerMarker, ControllerProxy},
    fidl_fuchsia_hardware_block_partition::Guid as FidlGuid,
    fidl_fuchsia_hardware_block_volume::{VolumeManagerMarker, VolumeManagerProxy},
    fidl_fuchsia_io as fio,
    fuchsia_component::client::connect_to_protocol_at_path,
    fuchsia_zircon::{self as zx, zx_status_t},
    std::os::raw::c_int,
    std::{
        fs::OpenOptions,
        os::unix::io::AsRawFd,
        path::{Path, PathBuf},
    },
};

const FVM_DRIVER_PATH: &str = "fvm.so";

#[link(name = "fvm")]
extern "C" {
    // This function initializes FVM on a fuchsia.hardware.block.Block device
    // with a given slice size.
    fn fvm_init(fd: c_int, slice_size: usize) -> zx_status_t;
}

/// Formats the block device at `block_device` to be an empty FVM instance.
pub fn format_for_fvm(block_device: &Path, fvm_slice_size: usize) -> Result<()> {
    let file = OpenOptions::new().read(true).write(true).open(block_device)?;
    let status = unsafe { fvm_init(file.as_raw_fd(), fvm_slice_size) };
    Ok(zx::ok(status)?)
}

/// Binds the FVM driver to the device at `controller`. Does not wait for the driver to be ready.
pub async fn bind_fvm_driver(controller: &ControllerProxy) -> Result<()> {
    controller.bind(FVM_DRIVER_PATH).await?.map_err(zx::Status::from_raw)?;
    Ok(())
}

/// Waits for an FVM device to appear under `block_device`. Returns a path to the FVM device.
pub async fn wait_for_fvm_driver(block_device: &Path) -> Result<PathBuf> {
    const FVM_DEVICE_NAME: &str = "fvm";
    let device = fuchsia_fs::open_directory_in_namespace(
        block_device.to_str().unwrap(),
        fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
    )?;
    recursive_wait_and_open_node(&device, FVM_DEVICE_NAME).await?;
    Ok(block_device.join(FVM_DEVICE_NAME))
}

/// Sets up an FVM instance on `block_device`. Returns a connection to the newly created FVM
/// instance.
pub async fn set_up_fvm(block_device: &Path, fvm_slice_size: usize) -> Result<VolumeManagerProxy> {
    format_for_fvm(block_device, fvm_slice_size)?;

    let controller =
        connect_to_protocol_at_path::<ControllerMarker>(block_device.to_str().unwrap())?;
    bind_fvm_driver(&controller).await?;

    let fvm_path = wait_for_fvm_driver(block_device).await?;
    Ok(connect_to_protocol_at_path::<VolumeManagerMarker>(fvm_path.to_str().unwrap())?)
}

/// Creates an FVM volume in `volume_manager`.
///
/// If `volume_size` is not provided then the volume will start with 1 slice. If `volume_size` is
/// provided then the volume will start with the minimum number of slices required to have
/// `volume_size` bytes.
///
/// `wait_for_block_device` can be used to find the volume after its created.
pub async fn create_fvm_volume(
    volume_manager: &VolumeManagerProxy,
    name: &str,
    type_guid: &Guid,
    instance_guid: &Guid,
    volume_size: Option<u64>,
    flags: u32,
) -> Result<()> {
    let slice_count = match volume_size {
        Some(volume_size) => {
            let (status, info) =
                volume_manager.get_info().await.context("Failed to get FVM info")?;
            zx::ok(status).context("Get Info Error")?;
            let slice_size = info.unwrap().slice_size;
            assert!(slice_size > 0);
            // Number of slices needed to satisfy volume_size.
            (volume_size + slice_size - 1) / slice_size
        }
        None => 1,
    };
    let mut type_guid = FidlGuid { value: type_guid.clone() };
    let mut instance_guid = FidlGuid { value: instance_guid.clone() };

    let status = volume_manager
        .allocate_partition(slice_count, &mut type_guid, &mut instance_guid, name, flags)
        .await?;
    Ok(zx::ok(status)?)
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{wait_for_block_device, wait_for_ramctl, BlockDeviceMatcher},
        fidl_fuchsia_hardware_block_volume::VolumeMarker,
        fidl_fuchsia_hardware_block_volume::ALLOCATE_PARTITION_FLAG_INACTIVE,
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
    async fn set_up_fvm_test() {
        wait_for_ramctl().await.unwrap();
        let ramdisk = RamdiskClient::create(BLOCK_SIZE, BLOCK_COUNT).unwrap();
        let fvm = set_up_fvm(Path::new(ramdisk.get_path()), FVM_SLICE_SIZE)
            .await
            .expect("Failed to set up FVM");

        let fvm_info = fvm.get_info().await.unwrap();
        zx::ok(fvm_info.0).unwrap();
        let fvm_info = fvm_info.1.unwrap();
        assert_eq!(fvm_info.slice_size, FVM_SLICE_SIZE as u64);
        assert_eq!(fvm_info.assigned_slice_count, 0);
    }

    #[fuchsia::test]
    async fn create_fvm_volume_without_volume_size_has_one_slice() {
        wait_for_ramctl().await.unwrap();
        let ramdisk = RamdiskClient::create(BLOCK_SIZE, BLOCK_COUNT).unwrap();
        let fvm = set_up_fvm(Path::new(ramdisk.get_path()), FVM_SLICE_SIZE)
            .await
            .expect("Failed to set up FVM");

        create_fvm_volume(
            &fvm,
            VOLUME_NAME,
            &TYPE_GUID,
            &INSTANCE_GUID,
            None,
            ALLOCATE_PARTITION_FLAG_INACTIVE,
        )
        .await
        .expect("Failed to create fvm volume");
        let block_device_path = wait_for_block_device(&[
            BlockDeviceMatcher::TypeGuid(&TYPE_GUID),
            BlockDeviceMatcher::InstanceGuid(&INSTANCE_GUID),
            BlockDeviceMatcher::Name(VOLUME_NAME),
        ])
        .await
        .expect("Failed to find block device");

        let volume =
            connect_to_protocol_at_path::<VolumeMarker>(block_device_path.to_str().unwrap())
                .unwrap();
        let volume_info = volume.get_volume_info().await.unwrap();
        zx::ok(volume_info.0).unwrap();
        let volume_info = volume_info.2.unwrap();
        assert_eq!(volume_info.partition_slice_count, 1);
    }

    #[fuchsia::test]
    async fn create_fvm_volume_with_unaligned_volume_size_rounds_up_to_slice_multiple() {
        wait_for_ramctl().await.unwrap();
        let ramdisk = RamdiskClient::create(BLOCK_SIZE, BLOCK_COUNT).unwrap();
        let fvm = set_up_fvm(Path::new(ramdisk.get_path()), FVM_SLICE_SIZE)
            .await
            .expect("Failed to set up FVM");

        create_fvm_volume(
            &fvm,
            VOLUME_NAME,
            &TYPE_GUID,
            &INSTANCE_GUID,
            Some((FVM_SLICE_SIZE * 5 + 4) as u64),
            ALLOCATE_PARTITION_FLAG_INACTIVE,
        )
        .await
        .expect("Failed to create fvm volume");

        let block_device_path = wait_for_block_device(&[
            BlockDeviceMatcher::TypeGuid(&TYPE_GUID),
            BlockDeviceMatcher::InstanceGuid(&INSTANCE_GUID),
            BlockDeviceMatcher::Name(VOLUME_NAME),
        ])
        .await
        .expect("Failed to find block device");

        let volume =
            connect_to_protocol_at_path::<VolumeMarker>(block_device_path.to_str().unwrap())
                .unwrap();
        let volume_info = volume.get_volume_info().await.unwrap();
        zx::ok(volume_info.0).unwrap();
        let volume_info = volume_info.2.unwrap();
        assert_eq!(volume_info.partition_slice_count, 6);
    }
}

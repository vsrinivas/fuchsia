// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_device::ControllerMarker,
    fidl_fuchsia_hardware_block_volume::{VolumeManagerMarker, VolumeManagerProxy},
    fuchsia_component::client::connect_to_protocol_at_path,
    fuchsia_zircon::{AsHandleRef, Rights, Status, Vmo},
    ramdevice_client::{RamdiskClient, VmoRamdiskClientBuilder},
    std::path::{Path, PathBuf},
    storage_isolated_driver_manager::{
        create_random_guid, fvm, wait_for_block_device, wait_for_ramctl, BlockDeviceMatcher,
    },
};

pub use storage_isolated_driver_manager::Guid;

fn create_ramdisk(vmo: &Vmo, ramdisk_block_size: u64) -> RamdiskClient {
    let duplicated_handle = vmo.as_handle_ref().duplicate(Rights::SAME_RIGHTS).unwrap();
    let duplicated_vmo = Vmo::from(duplicated_handle);

    // Create the ramdisks
    VmoRamdiskClientBuilder::new(duplicated_vmo).block_size(ramdisk_block_size).build().unwrap()
}

async fn start_fvm_driver(ramdisk_path: &Path) -> VolumeManagerProxy {
    let controller =
        connect_to_protocol_at_path::<ControllerMarker>(ramdisk_path.to_str().unwrap()).unwrap();
    fvm::bind_fvm_driver(&controller).await.unwrap();

    // Wait until the FVM driver is available
    let fvm_path = fvm::wait_for_fvm_driver(ramdisk_path).await.unwrap();

    // Connect to the Volume Manager
    let proxy =
        connect_to_protocol_at_path::<VolumeManagerMarker>(fvm_path.to_str().unwrap()).unwrap();
    proxy
}

/// This structs holds processes of component manager, isolated-devmgr
/// and the fvm driver.
///
/// NOTE: The order of fields in this struct is important.
/// Destruction happens top-down. Test must be destroyed last.
pub struct FvmInstance {
    /// A proxy to fuchsia.hardware.block.VolumeManager protocol
    /// Used to create new FVM volumes
    volume_manager: VolumeManagerProxy,

    /// Manages the ramdisk device that is backed by a VMO
    ramdisk: RamdiskClient,
}

impl FvmInstance {
    /// Start an isolated FVM driver against the given VMO.
    /// If `init` is true, initialize the VMO with FVM layout first.
    pub async fn new(init: bool, vmo: &Vmo, fvm_slice_size: u64, ramdisk_block_size: u64) -> Self {
        wait_for_ramctl().await.unwrap();

        let ramdisk = create_ramdisk(&vmo, ramdisk_block_size);
        let ramdisk_path = Path::new(ramdisk.get_path());

        if init {
            fvm::format_for_fvm(ramdisk_path, fvm_slice_size as usize).unwrap();
        }

        let volume_manager = start_fvm_driver(ramdisk_path).await;

        Self { ramdisk, volume_manager }
    }

    /// Create a new FVM volume with the given name and type GUID.
    /// Returns the instance GUID used to uniquely identify this volume.
    pub async fn new_volume(
        &mut self,
        name: &str,
        type_guid: &Guid,
        initial_volume_size: Option<u64>,
    ) -> Guid {
        let instance_guid = create_random_guid();

        fvm::create_fvm_volume(
            &self.volume_manager,
            name,
            type_guid,
            &instance_guid,
            initial_volume_size,
            0,
        )
        .await
        .unwrap();

        instance_guid
    }

    /// Returns the number of bytes the FVM partition has available.
    pub async fn free_space(&self) -> u64 {
        let (status, info) = self.volume_manager.get_info().await.unwrap();
        Status::ok(status).unwrap();
        let info = info.unwrap();

        (info.slice_count - info.assigned_slice_count) * info.slice_size
    }

    pub fn ramdisk_path(&self) -> PathBuf {
        PathBuf::from(self.ramdisk.get_path())
    }
}

/// Gets the full path to a volume matching the given instance GUID at the given
/// /dev/class/block path. This function will wait until a matching volume is found.
pub async fn get_volume_path(instance_guid: &Guid) -> PathBuf {
    wait_for_block_device(&[BlockDeviceMatcher::InstanceGuid(instance_guid)]).await.unwrap()
}

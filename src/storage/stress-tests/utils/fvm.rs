// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::io::Directory,
    fidl_fuchsia_device::ControllerMarker,
    fidl_fuchsia_hardware_block_partition::Guid,
    fidl_fuchsia_hardware_block_volume::{
        VolumeManagerMarker, VolumeManagerProxy, VolumeMarker, VolumeProxy,
    },
    fidl_fuchsia_io::OPEN_RIGHT_READABLE,
    fuchsia_component::client::connect_to_protocol_at_path,
    fuchsia_zircon::{sys::zx_status_t, AsHandleRef, Rights, Status, Vmo},
    ramdevice_client::{RamdiskClient, VmoRamdiskClientBuilder},
    rand::{rngs::SmallRng, FromEntropy, Rng},
    std::{
        fs::OpenOptions,
        os::{raw::c_int, unix::io::AsRawFd},
        path::PathBuf,
        time::Duration,
    },
    storage_isolated_driver_manager::bind_fvm,
};

// These are the paths associated with isolated-devmgr's dev directory.
//
// We make some assumptions when using these paths:
// * there exists an isolated-devmgr component that is a child of the test
// * that component exposes its /dev directory
// * the test exposes the /dev directory from isolated-devmgr
//
// The test can then access /dev of isolated-devmgr by accessing its own expose
// directory from the hub. Touching /dev will cause isolated-devmgr to be started
// by component manager.
pub const DEV_PATH: &'static str = "/hub/exec/expose/dev";
pub const BLOCK_PATH: &'static str = "/hub/exec/expose/dev/class/block";
pub const RAMCTL_PATH: &'static str = "/hub/exec/expose/dev/sys/platform/00:00:2d/ramctl";

#[link(name = "fs-management")]
extern "C" {
    // This function initializes FVM on a fuchsia.hardware.block.Block device
    // with a given slice size.
    pub fn fvm_init(fd: c_int, slice_size: usize) -> zx_status_t;
}

fn wait_for_ramctl() {
    ramdevice_client::wait_for_device(RAMCTL_PATH, Duration::from_secs(20))
        .expect("Could not wait for ramctl from isolated-devmgr");
}

fn create_ramdisk(vmo: &Vmo, ramdisk_block_size: u64) -> RamdiskClient {
    let duplicated_handle = vmo.as_handle_ref().duplicate(Rights::SAME_RIGHTS).unwrap();
    let duplicated_vmo = Vmo::from(duplicated_handle);

    // Create the ramdisks
    let dev_root = OpenOptions::new().read(true).write(true).open(&DEV_PATH).unwrap();
    VmoRamdiskClientBuilder::new(duplicated_vmo)
        .block_size(ramdisk_block_size)
        .dev_root(dev_root)
        .build()
        .unwrap()
}

fn init_fvm(ramdisk_path: &str, fvm_slice_size: u64) {
    // Create the FVM filesystem
    let ramdisk_file = OpenOptions::new().read(true).write(true).open(ramdisk_path).unwrap();
    let ramdisk_fd = ramdisk_file.as_raw_fd();
    let status = unsafe { fvm_init(ramdisk_fd, fvm_slice_size as usize) };
    Status::ok(status).unwrap();
}

async fn start_fvm_driver(ramdisk_path: &str) -> VolumeManagerProxy {
    let controller = connect_to_protocol_at_path::<ControllerMarker>(ramdisk_path).unwrap();
    bind_fvm(&controller).await.unwrap();

    // Wait until the FVM driver is available
    let fvm_path = PathBuf::from(ramdisk_path).join("fvm");
    let fvm_path = fvm_path.to_str().unwrap();
    ramdevice_client::wait_for_device(fvm_path, Duration::from_secs(20)).unwrap();

    // Connect to the Volume Manager
    let proxy = connect_to_protocol_at_path::<VolumeManagerMarker>(fvm_path).unwrap();
    proxy
}

async fn does_guid_match(volume_proxy: &VolumeProxy, expected_instance_guid: &Guid) -> bool {
    // The GUIDs must match
    let (status, actual_guid_instance) = volume_proxy.get_instance_guid().await.unwrap();

    // The ramdisk is also a block device, but does not support the Volume protocol
    if let Err(Status::NOT_SUPPORTED) = Status::ok(status) {
        return false;
    }

    let actual_guid_instance = actual_guid_instance.unwrap();
    *actual_guid_instance == *expected_instance_guid
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
    _ramdisk: RamdiskClient,
}

impl FvmInstance {
    /// Start an isolated FVM driver against the given VMO.
    /// If `init` is true, initialize the VMO with FVM layout first.
    pub async fn new(init: bool, vmo: &Vmo, fvm_slice_size: u64, ramdisk_block_size: u64) -> Self {
        wait_for_ramctl();
        let ramdisk = create_ramdisk(&vmo, ramdisk_block_size);

        let dev_path = PathBuf::from(DEV_PATH);
        let ramdisk_path = dev_path.join(ramdisk.get_path());
        let ramdisk_path = ramdisk_path.to_str().unwrap();

        if init {
            init_fvm(ramdisk_path, fvm_slice_size);
        }

        let volume_manager = start_fvm_driver(ramdisk_path).await;

        Self { _ramdisk: ramdisk, volume_manager }
    }

    /// Create a new FVM volume with the given name and type GUID. This volume will consume
    /// exactly 1 slice. Returns the instance GUID used to uniquely identify this volume.
    pub async fn new_volume(&mut self, name: &str, mut type_guid: Guid) -> Guid {
        // Generate a random instance GUID
        let mut rng = SmallRng::from_entropy();
        let mut instance_guid = Guid { value: rng.gen() };

        // Create the new volume
        let status = self
            .volume_manager
            .allocate_partition(1, &mut type_guid, &mut instance_guid, name, 0)
            .await
            .unwrap();
        Status::ok(status).unwrap();

        instance_guid
    }
}

/// Gets the full path to a volume matching the given instance GUID at the given
/// /dev/class/block path. This function will wait until a matching volume is found.
pub async fn get_volume_path(instance_guid: &Guid) -> PathBuf {
    let dir = Directory::from_namespace(BLOCK_PATH, OPEN_RIGHT_READABLE).unwrap();
    let block_path = PathBuf::from(BLOCK_PATH);
    loop {
        // TODO(xbhatnag): Find a better way to wait for the volume to appear
        for entry in dir.entries().await.unwrap() {
            let volume_path = block_path.join(entry);
            let volume_path_str = volume_path.to_str().unwrap();

            // Connect to the Volume FIDL protocol
            let volume_proxy =
                connect_to_protocol_at_path::<VolumeMarker>(volume_path_str).unwrap();
            if does_guid_match(&volume_proxy, &instance_guid).await {
                return volume_path;
            }
        }
    }
}

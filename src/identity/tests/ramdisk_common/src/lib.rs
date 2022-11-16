// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![warn(clippy::all)]
#![allow(clippy::expect_fun_call)]

use {
    fidl::{
        endpoints::{Proxy, ServerEnd},
        HandleBased,
    },
    fidl_fuchsia_device::ControllerMarker,
    fidl_fuchsia_device::ControllerProxy,
    fidl_fuchsia_hardware_block_encrypted::{DeviceManagerMarker, DeviceManagerProxy},
    fidl_fuchsia_hardware_block_partition::Guid,
    fidl_fuchsia_hardware_block_volume::VolumeManagerMarker,
    fidl_fuchsia_io as fio,
    fuchsia_async::{self as fasync},
    fuchsia_component_test::RealmInstance,
    fuchsia_driver_test as _,
    fuchsia_zircon::{sys::zx_status_t, Status},
    ramdevice_client::{RamdiskClient, RamdiskClientBuilder},
    rand::{rngs::SmallRng, Rng, SeedableRng},
    std::{fs, os::raw::c_int, time::Duration},
    storage_isolated_driver_manager::bind_fvm,
};

const RAMCTL_PATH: &str = "sys/platform/00:00:2d/ramctl";
const BLOCK_SIZE: u64 = 4096;
const BLOCK_COUNT: u64 = 1024; // 4MB RAM ought to be good enough

// 1 block for zxcrypt, and minfs needs at least 3 blocks.
const FVM_SLICE_SIZE: usize = BLOCK_SIZE as usize * 4;

// The maximum time to wait for a `wait_for_device_at` call. For whatever reason, using
// `Duration::MAX` seems to trigger immediate ZX_ERR_TIMED_OUT in the wait_for_device_at calls, so
// we just set a quite large timeout here.
const DEVICE_WAIT_TIMEOUT: Duration = Duration::from_secs(60);

#[link(name = "fs-management")]
extern "C" {
    pub fn fvm_init(fd: c_int, slice_size: usize) -> zx_status_t;
}

/// Given a realm instance, return a DirectoryProxy which is a client to
///
/// NB: This method calls .expect() and panics rather than returning a result, so
/// it is suitable only for use in tests.
/// root.open(..,"dev",..).
pub fn get_dev_root(realm_instance: &RealmInstance) -> fio::DirectoryProxy {
    let (dev_dir_client, dev_dir_server) =
        fidl::endpoints::create_proxy::<fio::DirectoryMarker>().expect("create channel pair");

    realm_instance
        .root
        .get_exposed_dir()
        .open(
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
            fio::MODE_TYPE_DIRECTORY,
            "dev",
            ServerEnd::new(dev_dir_server.into_channel()),
        )
        .expect("Get /dev from isolated_devmgr");
    dev_dir_client
}

/// Given a realm instance, return a File which is /dev, taken as a handle.
///
/// NB: This method calls .expect() and panics rather than returning a result, so
/// it is suitable only for use in tests.
pub fn get_dev_root_fd(realm_instance: &RealmInstance) -> fs::File {
    let dev_root_proxy = get_dev_root(realm_instance);
    fdio::create_fd(
        dev_root_proxy
            .into_channel()
            .expect("Could not convert dev root DirectoryProxy into channel")
            .into_zx_channel()
            .into_handle(),
    )
    .expect("create fd of dev root")
}

/// Given a realm instance, a GUID, and the name of some RAM partition, allocates
/// that volume in RAM and returns the RamDiskClient by value. NB: Callers should
/// retain this client in a test framework, since dropping RamdiskClient destroys
/// the disk.
///
/// NB: This method calls .expect() and panics rather than returning a result, so
/// it is suitable only for use in tests.
pub async fn setup_ramdisk(
    realm_instance: &RealmInstance,
    mut type_guid: Guid,
    name: &str,
) -> RamdiskClient {
    let dev_root_fd = get_dev_root_fd(realm_instance);

    // Wait for ramctl in namespace at /dev/sys/platform/00:00:2d/ramctl
    ramdevice_client::wait_for_device_at(&dev_root_fd, RAMCTL_PATH, DEVICE_WAIT_TIMEOUT)
        .expect("Could not wait for ramctl from isolated-devmgr");

    // Create ramdisk
    let ramdisk = RamdiskClientBuilder::new(BLOCK_SIZE, BLOCK_COUNT)
        .dev_root(get_dev_root_fd(realm_instance))
        .build()
        .expect("Could not create ramdisk");

    // Open ramdisk device and initialize FVM
    {
        let ramdisk_handle = ramdisk.open().expect("Could not re-open ramdisk").into_handle();
        let ramdisk_fd = fdio::create_fd(ramdisk_handle).expect("create fd of dev root");
        let status = unsafe { fvm_init(ramdisk_fd, FVM_SLICE_SIZE) };
        Status::ok(status).expect("could not initialize FVM structures in ramdisk");
        // ramdisk_file drops, closing the fd we created
    }

    // Open ramdisk device again as fidl_fuchsia_device::ControllerProxy
    let ramdisk_chan = ramdisk.open().expect("Could not re-open ramdisk");
    let controller_chan = fasync::Channel::from_channel(ramdisk_chan)
        .expect("Could not convert ramdisk channel to async channel");
    let controller = ControllerProxy::from_channel(controller_chan);

    // Bind FVM to that ramdisk
    bind_fvm(&controller).await.expect("Could not bind FVM");

    // wait for /fvm child device to appear and open it
    let fvm_path = ramdisk.get_path().to_string() + "/fvm";
    ramdevice_client::wait_for_device_at(&dev_root_fd, &fvm_path, DEVICE_WAIT_TIMEOUT)
        .expect("Could not wait for fvm from isolated-devmgr");

    let (volume_manager_client, volume_manager_server) =
        fidl::endpoints::create_proxy::<VolumeManagerMarker>()
            .expect("Could not create volume manager channel pair");
    get_dev_root(realm_instance)
        .open(
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
            fio::MODE_TYPE_SERVICE,
            &fvm_path,
            ServerEnd::new(volume_manager_server.into_channel()),
        )
        .expect("Could not connect to VolumeManager");

    // create FVM child volume with desired GUID/label
    let mut rng = SmallRng::from_entropy();
    let mut instance_guid = Guid { value: rng.gen() };
    let status = volume_manager_client
        .allocate_partition(1, &mut type_guid, &mut instance_guid, name, 0)
        .await
        .expect("Could not request to create volume");
    Status::ok(status).expect("Could not create volume");

    let fvm_inner_block_path = fvm_path + "/" + name + "-p-1/block";
    ramdevice_client::wait_for_device_at(&dev_root_fd, &fvm_inner_block_path, DEVICE_WAIT_TIMEOUT)
        .expect("Could not wait for inner fvm block device");

    // Return handle to ramdisk since RamdiskClient's Drop impl destroys the ramdisk.
    ramdisk
}

/// Given a realm instance, opens /<ramdisk>/fvm/<name>-p-1/block/zxcrypt and
/// returns a proxy to it.
///
/// NB: This method calls .expect() and panics rather than returning a result, so
/// it is suitable only for use in tests.
pub fn open_zxcrypt_manager(
    realm_instance: &RealmInstance,
    ramdisk: &RamdiskClient,
    name: &str,
) -> DeviceManagerProxy {
    let (manager_client, manager_server) = fidl::endpoints::create_proxy::<DeviceManagerMarker>()
        .expect("Could not create encryption volume manager channel pair");
    let mgr_path = ramdisk.get_path().to_string() + "/fvm/" + name + "-p-1/block/zxcrypt";
    get_dev_root(realm_instance)
        .open(
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
            fio::MODE_TYPE_SERVICE,
            &mgr_path,
            ServerEnd::new(manager_server.into_channel()),
        )
        .expect("Could not connect to zxcrypt manager");

    manager_client
}

/// Given a realm instance and a ramdisk, formats that disk under
/// /<ramdisk>/fvm/<name>-p-1/block as a zxcrypt disk.
///
/// NB: This method calls .expect() and panics rather than returning a result, so
/// it is suitable only for use in tests.
pub async fn format_zxcrypt(realm_instance: &RealmInstance, ramdisk: &RamdiskClient, name: &str) {
    let (controller_client, controller_server) =
        fidl::endpoints::create_proxy::<ControllerMarker>().expect("create channel pair");
    let block_path = ramdisk.get_path().to_string() + "/fvm/" + name + "-p-1/block";
    get_dev_root(realm_instance)
        .open(
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
            fio::MODE_TYPE_SERVICE,
            &block_path,
            ServerEnd::new(controller_server.into_channel()),
        )
        .expect("Could not connect to fvm block device");

    // Bind the zxcrypt driver to the block device
    controller_client
        .bind("zxcrypt.so")
        .await
        .expect("Could not send request to bind zxcrypt driver")
        .expect("Could not bind zxcrypt driver");

    // Wait for zxcrypt device manager node to appear
    let zxcrypt_path = block_path + "/zxcrypt";
    let dev_root_fd = get_dev_root_fd(realm_instance);
    ramdevice_client::wait_for_device_at(&dev_root_fd, &zxcrypt_path, DEVICE_WAIT_TIMEOUT)
        .expect("wait for zxcrypt from isolated-devmgr");

    // Open zxcrypt device manager node
    let manager = open_zxcrypt_manager(realm_instance, ramdisk, name);
    let key: [u8; 32] = [0; 32];
    manager.format(&key, 0).await.expect("Could not format zxcrypt");
}

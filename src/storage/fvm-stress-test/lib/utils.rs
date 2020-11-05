// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::fvm::VolumeManager,
    fidl_fuchsia_device::{ControllerMarker, ControllerProxy},
    fidl_fuchsia_hardware_block_volume::VolumeManagerMarker,
    fuchsia_component::client::connect_to_service_at_path,
    fuchsia_zircon::{sys::zx_status_t, AsHandleRef, Rights, Status, Vmo},
    ramdevice_client::{RamdiskClient, VmoRamdiskClientBuilder},
    std::{fs::OpenOptions, os::raw::c_int, os::unix::io::AsRawFd, path::PathBuf, time::Duration},
    test_utils_lib::{
        events::{Event, Started},
        matcher::EventMatcher,
        opaque_test::OpaqueTest,
    },
};

#[link(name = "fs-management")]
extern "C" {
    // This function initializes FVM on a fuchsia.hardware.block.Block device
    // with a given slice size.
    pub fn fvm_init(fd: c_int, slice_size: usize) -> zx_status_t;
}

// Path to the fvm driver, from the perspective of isolated-devmgr's namespace
pub const FVM_DRIVER_PATH: &str = "/pkg/bin/driver/fvm.so";

pub async fn start_test() -> OpaqueTest {
    let test: OpaqueTest =
        OpaqueTest::default("fuchsia-pkg://fuchsia.com/fvm-stress-test#meta/root.cm")
            .await
            .unwrap();

    // Wait for the root component to start
    let event_source = test.connect_to_event_source().await.unwrap();
    let mut started_event_stream = event_source.subscribe(vec![Started::NAME]).await.unwrap();
    event_source.start_component_tree().await;
    EventMatcher::ok().moniker(".").expect_match::<Started>(&mut started_event_stream).await;

    test
}

pub fn create_ramdisk(test: &OpaqueTest, vmo: &Vmo, ramdisk_block_size: u64) -> RamdiskClient {
    // Wait until the ramctl driver is available
    let dev_path = test.get_hub_v2_path().join("exec/expose/dev");
    let ramctl_path = dev_path.join("misc/ramctl");
    let ramctl_path = ramctl_path.to_str().unwrap();
    ramdevice_client::wait_for_device(ramctl_path, Duration::from_secs(20)).unwrap();

    let duplicated_handle = vmo.as_handle_ref().duplicate(Rights::SAME_RIGHTS).unwrap();
    let duplicated_vmo = Vmo::from(duplicated_handle);

    // Create the ramdisks
    let dev_root = OpenOptions::new().read(true).write(true).open(&dev_path).unwrap();
    VmoRamdiskClientBuilder::new(duplicated_vmo)
        .block_size(ramdisk_block_size)
        .dev_root(dev_root)
        .build()
        .unwrap()
}

pub fn init_fvm(ramdisk_path: &str, fvm_slice_size: u64) {
    // Create the FVM filesystem
    let ramdisk_file = OpenOptions::new().read(true).write(true).open(ramdisk_path).unwrap();
    let ramdisk_fd = ramdisk_file.as_raw_fd();
    let status = unsafe { fvm_init(ramdisk_fd, fvm_slice_size as usize) };
    Status::ok(status).unwrap();
}

pub async fn start_fvm_driver(ramdisk_path: &str) -> (ControllerProxy, VolumeManager) {
    // Start the FVM driver
    let controller = connect_to_service_at_path::<ControllerMarker>(ramdisk_path).unwrap();
    controller.bind(FVM_DRIVER_PATH).await.unwrap().unwrap();

    // Wait until the FVM driver is available
    let fvm_path = PathBuf::from(ramdisk_path).join("fvm");
    let fvm_path = fvm_path.to_str().unwrap();
    ramdevice_client::wait_for_device(fvm_path, Duration::from_secs(20)).unwrap();

    // Connect to the Volume Manager
    let proxy = connect_to_service_at_path::<VolumeManagerMarker>(fvm_path).unwrap();
    (controller, VolumeManager::new(proxy))
}

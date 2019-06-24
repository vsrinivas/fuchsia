// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{Error, ResultExt},
    fidl_fuchsia_bluetooth::Address,
    fidl_fuchsia_bluetooth_host::HostProxy,
    fidl_fuchsia_bluetooth_test::EmulatorSettings,
    fuchsia_async as fasync,
    fuchsia_bluetooth::{
        constants::HOST_DEVICE_DIR,
        device_watcher::{DeviceWatcher, WatchFilter},
        hci,
        hci_emulator::Emulator,
        host,
    },
    fuchsia_zircon as zx,
    std::path::PathBuf,
};

fn timeout() -> zx::Duration {
    zx::Duration::from_seconds(10)
}

// Tests that creating and destroying a fake HCI device binds and unbinds the bt-host driver.
pub async fn lifecycle_test(_: ()) -> Result<(), Error> {
    let addr_bytes = Address { bytes: [1, 2, 3, 4, 5, 6] };
    let addr_str = "06:05:04:03:02:01";
    let settings = EmulatorSettings {
        address: Some(addr_bytes),
        hci_config: None,
        extended_advertising: None,
        acl_buffer_settings: None,
        le_acl_buffer_settings: None,
    };

    let fake_hci = await!(Emulator::create("bt-hci-integration-lifecycle"))?;
    let hci_topo = PathBuf::from(fdio::device_get_topo_path(fake_hci.file())?);

    // Publish the bt-hci device and verify that a bt-host appears under its topology within a
    // reasonable timeout.
    let mut watcher = DeviceWatcher::new(HOST_DEVICE_DIR, timeout())?;
    let _ = await!(fake_hci.publish(settings))?;
    let bthost = await!(watcher.watch_new(&hci_topo, WatchFilter::AddedOnly))?;

    // Check the right driver is bound to the device
    let driver_name = hci::get_device_driver_name(bthost.file())?;
    assert_eq!("bt_host", driver_name);

    // Open a host channel using a fidl call and check the device is responsive
    let handle = host::open_host_channel(bthost.file())?;
    let host = HostProxy::new(fasync::Channel::from_channel(handle.into())?);
    let info = await!(host.get_info())
        .context("Is bt-gap running? If so, try stopping it and re-running these tests")?;

    // The bt-host should have been initialized with the address that we initially configured.
    assert_eq!(addr_str, info.address);

    // Remove the bt-hci device
    drop(fake_hci);

    // Check that the bt-host device is also destroyed.
    await!(watcher.watch_removed(bthost.path()))
}

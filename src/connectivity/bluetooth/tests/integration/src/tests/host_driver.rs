// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::Error,
    fidl_fuchsia_bluetooth_control::TechnologyType,
    fuchsia_bluetooth::{error::Error as BtError, expectation},
};

use crate::harness::host_driver::{expect_eq, expect_remote_device, HostDriverHarness};

// TODO(BT-229): Currently these tests rely on fakes that are hard-coded in the fake
// HCI driver. Remove these once it is possible to set up mock devices programmatically.
const FAKE_LE_DEVICE_ADDR: &str = "00:00:00:00:00:01";
const FAKE_BREDR_DEVICE_ADDR: &str = "00:00:00:00:00:02";
const FAKE_LE_CONN_ERROR_DEVICE_ADDR: &str = "00:00:00:00:00:03";
const FAKE_DEVICE_COUNT: usize = 3;

// Tests that the local host driver address is 0.
pub async fn test_bd_addr(test_state: HostDriverHarness) -> Result<(), Error> {
    let info = await!(test_state.host_proxy().get_info())
        .map_err(|_| BtError::new("failed to read host driver info"))?;
    expect_eq!("00:00:00:00:00:00", info.address.as_str())
}

// Tests that setting the local name succeeds.
// TODO(armansito): Test for FakeHciDevice state changes.
pub async fn test_set_local_name(test_state: HostDriverHarness) -> Result<(), Error> {
    let name = "test1234";
    await!(test_state.host_proxy().set_local_name(&name))?;
    await!(test_state.expect(expectation::host_driver::name(name)))?;

    Ok(())
}

// Tests that host state updates when discoverable mode is turned on.
// TODO(armansito): Test for FakeHciDevice state changes.
pub async fn test_discoverable(test_state: HostDriverHarness) -> Result<(), Error> {
    // Enable discoverable mode.
    await!(test_state.host_proxy().set_discoverable(true))?;
    await!(test_state.expect(expectation::host_driver::discoverable(true)))?;

    // Disable discoverable mode
    await!(test_state.host_proxy().set_discoverable(false))?;
    await!(test_state.expect(expectation::host_driver::discoverable(false)))?;

    Ok(())
}

// Tests that host state updates when discovery is started and stopped.
// TODO(armansito): Test for FakeHciDevice state changes.
pub async fn test_discovery(test_state: HostDriverHarness) -> Result<(), Error> {
    // Start discovery. "discovering" should get set to true.
    await!(test_state.host_proxy().start_discovery())?;
    await!(test_state.expect(expectation::host_driver::discovering(true)))?;

    // The host should discover a fake peer.
    // TODO(BT-229): The name is currently hard-coded in
    //   src/connectivity/bluetooth/hci/fake/fake_device.cc:89.
    // Configure this dynamically when it is supported.
    let new_device = expectation::peer::name("Fake");
    await!(test_state.expect_peer(None, new_device))?;

    // Stop discovery. "discovering" should get set to false.
    await!(test_state.host_proxy().stop_discovery())?;
    await!(test_state.expect(expectation::host_driver::discovering(false)))?;

    Ok(())
}

// Tests that "close" cancels all operations.
// TODO(armansito): Test for FakeHciDevice state changes.
pub async fn test_close(test_state: HostDriverHarness) -> Result<(), Error> {
    // Enable all procedures.
    await!(test_state.host_proxy().start_discovery())?;
    await!(test_state.host_proxy().set_discoverable(true))?;
    let active_state = expectation::host_driver::discoverable(true)
        .and(expectation::host_driver::discovering(true));
    await!(test_state.expect(active_state))?;

    // Close should cancel these procedures.
    test_state.host_proxy().close()?;

    let closed_state_update = expectation::host_driver::discoverable(false)
        .and(expectation::host_driver::discovering(false));

    await!(test_state.expect(closed_state_update))?;

    Ok(())
}

// Tests that "list_devices" returns devices from a host's cache.
pub async fn test_list_devices(test_state: HostDriverHarness) -> Result<(), Error> {
    // Devices should be initially empty.
    let devices = await!(test_state.host_proxy().list_devices())?;
    expect_eq!(vec![], devices)?;

    // Wait for all fake devices to be discovered.
    // TODO(BT-229): Add support for setting these up programmatically instead of hardcoding
    // them. The fake HCI driver currently sets up one LE and one BR/EDR peer.
    await!(test_state.host_proxy().start_discovery())?;
    let expected_le = expectation::peer::address(FAKE_LE_DEVICE_ADDR)
        .and(expectation::peer::technology(TechnologyType::LowEnergy));

    let expected_bredr = expectation::peer::address(FAKE_BREDR_DEVICE_ADDR)
        .and(expectation::peer::technology(TechnologyType::Classic));

    let expected_le2 = expectation::peer::address(FAKE_LE_CONN_ERROR_DEVICE_ADDR)
        .and(expectation::peer::technology(TechnologyType::LowEnergy));

    await!(test_state.expect_peer(None, expected_le.clone()))?;
    await!(test_state.expect_peer(None, expected_bredr.clone()))?;
    await!(test_state.expect_peer(None, expected_le2.clone()))?;

    // List the host's devices
    let devices = await!(test_state.host_proxy().list_devices())?;

    // Both fake devices should be in the map.
    expect_eq!(FAKE_DEVICE_COUNT, devices.len())?;
    expect_remote_device(&test_state, FAKE_LE_DEVICE_ADDR, &expected_le)?;
    expect_remote_device(&test_state, FAKE_BREDR_DEVICE_ADDR, &expected_bredr)?;
    expect_remote_device(&test_state, FAKE_LE_CONN_ERROR_DEVICE_ADDR, &expected_le2)?;
    Ok(())
}

pub async fn test_connect(test_state: HostDriverHarness) -> Result<(), Error> {
    // Start discovery and let bt-host process the fake LE devices.
    await!(test_state.host_proxy().start_discovery())?;

    let le_dev = expectation::peer::address(FAKE_LE_DEVICE_ADDR);
    let le_error_dev = expectation::peer::address(FAKE_LE_CONN_ERROR_DEVICE_ADDR);

    await!(test_state.expect_peer(None, le_dev))?;
    await!(test_state.expect_peer(None, le_error_dev))?;

    let devices = await!(test_state.host_proxy().list_devices())?;
    expect_true!(devices.len() >= 2)?;

    // Obtain bt-host assigned IDs of the devices.
    let success_dev = devices
        .iter()
        .find(|x| x.address == FAKE_LE_DEVICE_ADDR)
        .ok_or(BtError::new("success peer not found"))?;
    let failure_dev = devices
        .iter()
        .find(|x| x.address == FAKE_LE_CONN_ERROR_DEVICE_ADDR)
        .ok_or(BtError::new("error peer not found"))?;

    // Connecting to the failure peer should result in an error.
    let mut status = await!(test_state.host_proxy().connect(&failure_dev.identifier))?;
    expect_true!(status.error.is_some())?;

    // Connecting to the success peer should return success and the peer should become connected.
    status = await!(test_state.host_proxy().connect(&success_dev.identifier))?;
    expect_true!(status.error.is_none())?;
    await!(test_state
        .expect_peer(Some(success_dev.identifier.clone()), expectation::peer::connected(true)))?;

    Ok(())
}

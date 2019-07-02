// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{err_msg, Error},
    fidl_fuchsia_bluetooth_control::TechnologyType,
    fuchsia_bluetooth::{
        error::Error as BtError,
        expectation::{self, peer},
    },
};

use crate::harness::host_driver::{
    expect_adapter_state, expect_eq, expect_host_peer, expect_no_peer, expect_remote_device,
    HostDriverHarness,
};

// TODO(BT-229): Currently these tests rely on fakes that are hard-coded in the fake
// HCI driver. Remove these once it is possible to set up mock devices programmatically.
const FAKE_LE_DEVICE_ADDR: &str = "00:00:00:00:00:01";
const FAKE_BREDR_DEVICE_ADDR: &str = "00:00:00:00:00:02";
const FAKE_LE_CONN_ERROR_DEVICE_ADDR: &str = "00:00:00:00:00:03";
const FAKE_DEVICE_COUNT: usize = 3;

// Tests that the local host driver address is 0.
pub async fn test_bd_addr(test_state: HostDriverHarness) -> Result<(), Error> {
    let info = await!(test_state.aux().0.get_info())
        .map_err(|_| BtError::new("failed to read host driver info"))?;
    expect_eq!("00:00:00:00:00:00", info.address.as_str())
}

// Tests that setting the local name succeeds.
// TODO(armansito): Test for FakeHciDevice state changes.
pub async fn test_set_local_name(test_state: HostDriverHarness) -> Result<(), Error> {
    let name = "test1234";
    await!(test_state.aux().0.set_local_name(&name))?;
    await!(expect_adapter_state(&test_state, expectation::host_driver::name(name)))?;

    Ok(())
}

// Tests that host state updates when discoverable mode is turned on.
// TODO(armansito): Test for FakeHciDevice state changes.
pub async fn test_discoverable(test_state: HostDriverHarness) -> Result<(), Error> {
    // Enable discoverable mode.
    await!(test_state.aux().0.set_discoverable(true))?;
    await!(expect_adapter_state(&test_state, expectation::host_driver::discoverable(true)))?;

    // Disable discoverable mode
    await!(test_state.aux().0.set_discoverable(false))?;
    await!(expect_adapter_state(&test_state, expectation::host_driver::discoverable(false)))?;

    Ok(())
}

// Tests that host state updates when discovery is started and stopped.
// TODO(armansito): Test for FakeHciDevice state changes.
pub async fn test_discovery(test_state: HostDriverHarness) -> Result<(), Error> {
    // Start discovery. "discovering" should get set to true.
    await!(test_state.aux().0.start_discovery())?;
    await!(expect_adapter_state(&test_state, expectation::host_driver::discovering(true)))?;

    // The host should discover a fake peer.
    // TODO(BT-229): The name is currently hard-coded in
    //   src/connectivity/bluetooth/hci/emulator/device.cc:89.
    // Configure this dynamically when it is supported.
    await!(expect_host_peer(&test_state, peer::name("Fake")))?;

    // Stop discovery. "discovering" should get set to false.
    await!(test_state.aux().0.stop_discovery())?;
    await!(expect_adapter_state(&test_state, expectation::host_driver::discovering(false)))?;

    Ok(())
}

// Tests that "close" cancels all operations.
// TODO(armansito): Test for FakeHciDevice state changes.
pub async fn test_close(test_state: HostDriverHarness) -> Result<(), Error> {
    // Enable all procedures.
    await!(test_state.aux().0.start_discovery())?;
    await!(test_state.aux().0.set_discoverable(true))?;
    let active_state = expectation::host_driver::discoverable(true)
        .and(expectation::host_driver::discovering(true));
    await!(expect_adapter_state(&test_state, active_state))?;

    // Close should cancel these procedures.
    test_state.aux().0.close()?;

    let closed_state_update = expectation::host_driver::discoverable(false)
        .and(expectation::host_driver::discovering(false));

    await!(expect_adapter_state(&test_state, closed_state_update))?;

    Ok(())
}

// Tests that "list_devices" returns devices from a host's cache.
pub async fn test_list_devices(test_state: HostDriverHarness) -> Result<(), Error> {
    // Devices should be initially empty.
    let devices = await!(test_state.aux().0.list_devices())?;
    expect_eq!(vec![], devices)?;

    // Wait for all fake devices to be discovered.
    // TODO(BT-229): Add support for setting these up programmatically instead of hardcoding
    // them. The fake HCI driver currently sets up one LE and one BR/EDR peer.
    await!(test_state.aux().0.start_discovery())?;
    let expected_le =
        peer::address(FAKE_LE_DEVICE_ADDR).and(peer::technology(TechnologyType::LowEnergy));

    let expected_bredr =
        peer::address(FAKE_BREDR_DEVICE_ADDR).and(peer::technology(TechnologyType::Classic));

    let expected_le2 = peer::address(FAKE_LE_CONN_ERROR_DEVICE_ADDR)
        .and(peer::technology(TechnologyType::LowEnergy));

    await!(expect_host_peer(&test_state, expected_le.clone()))?;
    await!(expect_host_peer(&test_state, expected_bredr.clone()))?;
    await!(expect_host_peer(&test_state, expected_le2.clone()))?;

    // List the host's devices
    let devices = await!(test_state.aux().0.list_devices())?;

    // Both fake devices should be in the map.
    expect_eq!(FAKE_DEVICE_COUNT, devices.len())?;
    expect_remote_device(&test_state, FAKE_LE_DEVICE_ADDR, &expected_le)?;
    expect_remote_device(&test_state, FAKE_BREDR_DEVICE_ADDR, &expected_bredr)?;
    expect_remote_device(&test_state, FAKE_LE_CONN_ERROR_DEVICE_ADDR, &expected_le2)?;
    Ok(())
}

pub async fn test_connect(test_state: HostDriverHarness) -> Result<(), Error> {
    // Start discovery and let bt-host process the fake devices.
    await!(test_state.aux().0.start_discovery())?;

    let le_dev = peer::address(FAKE_LE_DEVICE_ADDR);
    let le_error_dev = peer::address(FAKE_LE_CONN_ERROR_DEVICE_ADDR);

    await!(expect_host_peer(&test_state, le_dev))?;
    await!(expect_host_peer(&test_state, le_error_dev))?;

    let devices = await!(test_state.aux().0.list_devices())?;
    expect_true!(devices.len() >= 2)?;

    // Obtain bt-host assigned IDs of the devices.
    let success_dev = devices
        .iter()
        .find(|x| x.address == FAKE_LE_DEVICE_ADDR)
        .ok_or(err_msg("success peer not found"))?;
    let failure_dev = devices
        .iter()
        .find(|x| x.address == FAKE_LE_CONN_ERROR_DEVICE_ADDR)
        .ok_or(err_msg("error peer not found"))?;

    // Connecting to the failure peer should result in an error.
    let status = await!(test_state.aux().0.connect(&failure_dev.identifier))?;
    expect_true!(status.error.is_some())?;

    // Connecting to the success peer should return success and the peer should become connected.
    let status = await!(test_state.aux().0.connect(&success_dev.identifier))?;
    expect_true!(status.error.is_none())?;

    let connected = peer::identifier(&success_dev.identifier).and(peer::connected(true));
    await!(expect_host_peer(&test_state, connected))?;
    Ok(())
}

pub async fn wait_for_test_device(test_state: HostDriverHarness) -> Result<String, Error> {
    // Start discovery and let bt-host process the fake LE devices.
    await!(test_state.aux().0.start_discovery())?;
    let le_dev = expectation::peer::address(FAKE_LE_DEVICE_ADDR);
    await!(expect_host_peer(&test_state, le_dev))?;
    let devices = await!(test_state.aux().0.list_devices())?;
    expect_true!(devices.len() >= 2)?;

    // Obtain bt-host assigned IDs of the device.
    let success_dev = devices
        .iter()
        .find(|x| x.address == FAKE_LE_DEVICE_ADDR)
        .ok_or(err_msg("success peer not found"))?;
    Ok(success_dev.identifier.clone())
}

// TODO(BT-932) - Add a test for disconnect failure when a connection attempt is outgoing, provided
// that we can provide a manner of doing so that will not flake.

/// Disconnecting from an unknown device should succeed
pub async fn disconnect_unknown_device(test_state: HostDriverHarness) -> Result<(), Error> {
    let unknown_id = "0123401234";
    let status = await!(test_state.aux().0.disconnect(unknown_id))?;
    expect_eq!(status.error, None)
}

/// Disconnecting from a known, unconnected device should succeed
pub async fn disconnect_unconnected_device(test_state: HostDriverHarness) -> Result<(), Error> {
    let success_dev = await!(wait_for_test_device(test_state.clone()))?;
    let status = await!(test_state.aux().0.disconnect(&success_dev))?;
    expect_eq!(status.error, None)
}

/// Disconnecting from a connected device should succeed and result in the device being disconnected
pub async fn disconnect_connected_device(test_state: HostDriverHarness) -> Result<(), Error> {
    let success_dev = await!(wait_for_test_device(test_state.clone()))?;

    let status = await!(test_state.aux().0.connect(&success_dev))?;
    expect_eq!(status.error, None)?;

    let connected = peer::address(FAKE_LE_DEVICE_ADDR).and(peer::connected(true));
    let disconnected = peer::address(FAKE_LE_DEVICE_ADDR).and(peer::connected(false));

    let _ = await!(expect_host_peer(&test_state, connected))?;
    let status = await!(test_state.aux().0.disconnect(&success_dev))?;
    expect_eq!(status.error, None)?;
    let _ = await!(expect_host_peer(&test_state, disconnected))?;
    Ok(())
}

pub async fn test_forget(test_state: HostDriverHarness) -> Result<(), Error> {
    // Start discovery and let bt-host process the fake peers.
    await!(test_state.aux().0.start_discovery())?;

    // Wait for fake peer to be discovered.
    let expected_peer = expectation::peer::address(FAKE_LE_DEVICE_ADDR);
    await!(expect_host_peer(&test_state, expected_peer.clone()))?;

    let peers = await!(test_state.aux().0.list_devices())?;

    // Obtain bt-host assigned ID of the peer.
    let le_peer = peers
        .iter()
        .find(|x| x.address == FAKE_LE_DEVICE_ADDR)
        .ok_or(BtError::new("success peer not found"))?;

    // Connecting to the peer should return success and the peer should become connected.
    let mut status = await!(test_state.aux().0.connect(&le_peer.identifier))?;
    expect_true!(status.error.is_none())?;

    await!(expect_host_peer(&test_state, expected_peer.and(expectation::peer::connected(true))))?;

    // Forgetting the peer should result in its removal.
    status = await!(test_state.aux().0.forget(&le_peer.identifier))?;
    expect_true!(status.error.is_none())?;
    await!(expect_no_peer(&test_state, le_peer.identifier.clone()))?;

    // TODO(BT-879): Test that the link closes by querying fake HCI.

    Ok(())
}

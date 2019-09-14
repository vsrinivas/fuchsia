// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{err_msg, Error, ResultExt},
    fidl_fuchsia_bluetooth_control::TechnologyType,
    fidl_fuchsia_bluetooth_host::HostProxy,
    fidl_fuchsia_bluetooth_test::{EmulatorSettings, HciError, PeerProxy},
    fuchsia_async as fasync,
    fuchsia_bluetooth::{
        constants::HOST_DEVICE_DIR,
        device_watcher::{DeviceWatcher, WatchFilter},
        error::Error as BtError,
        expectation::{self, peer},
        hci_emulator::Emulator,
        host,
        types::Address,
    },
    fuchsia_zircon as zx,
    std::path::PathBuf,
};

use crate::harness::{
    expect::expect_eq,
    host_driver::{
        expect_adapter_state, expect_host_peer, expect_no_peer, expect_remote_device,
        HostDriverHarness,
    },
};

// Tests that creating and destroying a fake HCI device binds and unbinds the bt-host driver.
async fn test_lifecycle(_: ()) -> Result<(), Error> {
    let address = Address::Public([1, 2, 3, 4, 5, 6]);
    let settings = EmulatorSettings {
        address: Some(address.to_fidl()),
        hci_config: None,
        extended_advertising: None,
        acl_buffer_settings: None,
        le_acl_buffer_settings: None,
    };

    let mut emulator = Emulator::create("bt-hci-integration-lifecycle").await?;
    let hci_topo = PathBuf::from(fdio::device_get_topo_path(emulator.file())?);

    // Publish the bt-hci device and verify that a bt-host appears under its topology within a
    // reasonable timeout.
    let mut watcher = DeviceWatcher::new(HOST_DEVICE_DIR, zx::Duration::from_seconds(10)).await?;
    let _ = emulator.publish(settings).await?;
    let bthost = watcher.watch_new(&hci_topo, WatchFilter::AddedOnly).await?;

    // Open a host channel using a fidl call and check the device is responsive
    let handle = host::open_host_channel(bthost.file())?;
    let host = HostProxy::new(fasync::Channel::from_channel(handle.into())?);
    let info = host
        .get_info()
        .await
        .context("Is bt-gap running? If so, try stopping it and re-running these tests")?;

    // The bt-host should have been initialized with the address that we initially configured.
    assert_eq!(address.to_string(), info.address);

    // Remove the bt-hci device and check that the test device is also destroyed.
    emulator.destroy_and_wait().await?;

    // Check that the bt-host device is also destroyed.
    watcher.watch_removed(bthost.path()).await
}

// Tests that the local host driver address is 0.
async fn test_bd_addr(test_state: HostDriverHarness) -> Result<(), Error> {
    let info = test_state
        .aux()
        .proxy()
        .get_info()
        .await
        .map_err(|_| BtError::new("failed to read host driver info"))?;
    expect_eq!("00:00:00:00:00:00", info.address.as_str())
}

// Tests that setting the local name succeeds.
// TODO(armansito): Test for FakeHciDevice state changes.
async fn test_set_local_name(test_state: HostDriverHarness) -> Result<(), Error> {
    let name = "test1234";
    test_state.aux().proxy().set_local_name(&name).await?;
    expect_adapter_state(&test_state, expectation::host_driver::name(name)).await?;

    Ok(())
}

// Tests that host state updates when discoverable mode is turned on.
// TODO(armansito): Test for FakeHciDevice state changes.
async fn test_discoverable(test_state: HostDriverHarness) -> Result<(), Error> {
    // Enable discoverable mode.
    test_state.aux().proxy().set_discoverable(true).await?;
    expect_adapter_state(&test_state, expectation::host_driver::discoverable(true)).await?;

    // Disable discoverable mode
    test_state.aux().proxy().set_discoverable(false).await?;
    expect_adapter_state(&test_state, expectation::host_driver::discoverable(false)).await?;

    Ok(())
}

// Tests that host state updates when discovery is started and stopped.
// TODO(armansito): Test for FakeHciDevice state changes.
async fn test_discovery(test_state: HostDriverHarness) -> Result<(), Error> {
    // Start discovery. "discovering" should get set to true.
    test_state.aux().proxy().start_discovery().await?;
    expect_adapter_state(&test_state, expectation::host_driver::discovering(true)).await?;

    let address = Address::Random([1, 0, 0, 0, 0, 0]);
    let _peer = test_state.aux().add_le_peer_default(&address).await?;

    // The host should discover a fake peer.
    expect_host_peer(&test_state, peer::name("Fake").and(peer::address(&address.to_string())))
        .await?;

    // Stop discovery. "discovering" should get set to false.
    test_state.aux().proxy().stop_discovery().await?;
    expect_adapter_state(&test_state, expectation::host_driver::discovering(false)).await?;

    Ok(())
}

// Tests that "close" cancels all operations.
// TODO(armansito): Test for FakeHciDevice state changes.
async fn test_close(test_state: HostDriverHarness) -> Result<(), Error> {
    // Enable all procedures.
    test_state.aux().proxy().start_discovery().await?;
    test_state.aux().proxy().set_discoverable(true).await?;
    let active_state = expectation::host_driver::discoverable(true)
        .and(expectation::host_driver::discovering(true));
    expect_adapter_state(&test_state, active_state).await?;

    // Close should cancel these procedures.
    test_state.aux().proxy().close()?;

    let closed_state_update = expectation::host_driver::discoverable(false)
        .and(expectation::host_driver::discovering(false));

    expect_adapter_state(&test_state, closed_state_update).await?;

    Ok(())
}

// Tests that "list_devices" returns devices from a host's cache.
async fn test_list_devices(test_state: HostDriverHarness) -> Result<(), Error> {
    // Devices should be initially empty.
    let devices = test_state.aux().proxy().list_devices().await?;
    expect_eq!(vec![], devices)?;

    // Add a LE and a BR/EDR peer with the given addresses.
    let le_peer_address = Address::Random([1, 0, 0, 0, 0, 0]);
    let bredr_peer_address = Address::Public([2, 0, 0, 0, 0, 0]);

    let _le_peer = test_state.aux().add_le_peer_default(&le_peer_address).await?;
    let _bredr_peer = test_state.aux().add_bredr_peer_default(&bredr_peer_address).await?;

    // Wait for all fake devices to be discovered.
    test_state.aux().proxy().start_discovery().await?;
    let expected_le = peer::address(&le_peer_address.to_string())
        .and(peer::technology(TechnologyType::LowEnergy));
    let expected_bredr = peer::address(&bredr_peer_address.to_string())
        .and(peer::technology(TechnologyType::Classic));

    expect_host_peer(&test_state, expected_le.clone()).await?;
    expect_host_peer(&test_state, expected_bredr.clone()).await?;

    // List the host's devices
    let devices = test_state.aux().proxy().list_devices().await?;

    // Both fake devices should be in the map.
    expect_eq!(2, devices.len())?;
    expect_remote_device(&test_state, &le_peer_address.to_string(), &expected_le)?;
    expect_remote_device(&test_state, &bredr_peer_address.to_string(), &expected_bredr)?;
    Ok(())
}

async fn test_connect(test_state: HostDriverHarness) -> Result<(), Error> {
    let address1 = Address::Random([1, 0, 0, 0, 0, 0]);
    let address2 = Address::Random([2, 0, 0, 0, 0, 0]);
    let _peer1 = test_state.aux().add_le_peer_default(&address1).await?;
    let peer2 = test_state.aux().add_le_peer_default(&address2).await?;

    // Configure `peer2` to return an error for the connection attempt.
    let _ = peer2.assign_connection_status(HciError::ConnectionTimeout).await?;

    // Start discovery and let bt-host process the fake devices.
    test_state.aux().proxy().start_discovery().await?;

    let le_dev = peer::address(&address1.to_string());
    let le_error_dev = peer::address(&address2.to_string());

    expect_host_peer(&test_state, le_dev).await?;
    expect_host_peer(&test_state, le_error_dev).await?;

    let devices = test_state.aux().proxy().list_devices().await?;
    expect_true!(devices.len() >= 2)?;

    // Obtain bt-host assigned IDs of the devices.
    let success_dev = devices
        .iter()
        .find(|x| x.address == address1.to_string())
        .ok_or(err_msg("success peer not found"))?;
    let failure_dev = devices
        .iter()
        .find(|x| x.address == address2.to_string())
        .ok_or(err_msg("error peer not found"))?;

    // Connecting to the failure peer should result in an error.
    let status = test_state.aux().proxy().connect(&failure_dev.identifier).await?;
    expect_true!(status.error.is_some())?;

    // Connecting to the success peer should return success and the peer should become connected.
    let status = test_state.aux().proxy().connect(&success_dev.identifier).await?;
    expect_true!(status.error.is_none())?;

    let connected = peer::identifier(&success_dev.identifier).and(peer::connected(true));
    expect_host_peer(&test_state, connected).await?;
    Ok(())
}

async fn wait_for_test_device(
    test_state: HostDriverHarness,
    address: &Address,
) -> Result<(String, PeerProxy), Error> {
    let peer = test_state.aux().add_le_peer_default(&address).await?;

    // Start discovery and let bt-host process the fake LE peer.
    test_state.aux().proxy().start_discovery().await?;
    let le_dev = expectation::peer::address(&address.to_string());
    expect_host_peer(&test_state, le_dev).await?;
    let devices = test_state.aux().proxy().list_devices().await?;
    expect_true!(devices.len() == 1)?;

    // Obtain bt-host assigned IDs of the device.
    let success_dev = devices
        .iter()
        .find(|x| x.address == address.to_string())
        .ok_or(err_msg("success peer not found"))?;

    Ok((success_dev.identifier.clone(), peer))
}

// TODO(BT-932) - Add a test for disconnect failure when a connection attempt is outgoing, provided
// that we can provide a manner of doing so that will not flake.

/// Disconnecting from an unknown device should succeed
async fn test_disconnect_unknown_device(test_state: HostDriverHarness) -> Result<(), Error> {
    let unknown_id = "0123401234";
    let status = test_state.aux().proxy().disconnect(unknown_id).await?;
    expect_eq!(status.error, None)
}

/// Disconnecting from a known, unconnected device should succeed
async fn test_disconnect_unconnected_device(test_state: HostDriverHarness) -> Result<(), Error> {
    let address = Address::Random([1, 0, 0, 0, 0, 0]);
    let (success_dev, _proxy) = wait_for_test_device(test_state.clone(), &address).await?;
    let status = test_state.aux().proxy().disconnect(&success_dev).await?;
    expect_eq!(status.error, None)
}

/// Disconnecting from a connected device should succeed and result in the device being disconnected
async fn test_disconnect_connected_device(test_state: HostDriverHarness) -> Result<(), Error> {
    let address = Address::Random([1, 0, 0, 0, 0, 0]);
    let (success_dev, _proxy) = wait_for_test_device(test_state.clone(), &address).await?;

    let status = test_state.aux().proxy().connect(&success_dev).await?;
    expect_eq!(status.error, None)?;

    let connected = peer::address(&address.to_string()).and(peer::connected(true));
    let disconnected = peer::address(&address.to_string()).and(peer::connected(false));

    let _ = expect_host_peer(&test_state, connected).await?;
    let status = test_state.aux().proxy().disconnect(&success_dev).await?;
    expect_eq!(status.error, None)?;
    let _ = expect_host_peer(&test_state, disconnected).await?;
    Ok(())
}

async fn test_forget(test_state: HostDriverHarness) -> Result<(), Error> {
    let address = Address::Random([1, 0, 0, 0, 0, 0]);
    let (le_peer, _proxy) = wait_for_test_device(test_state.clone(), &address).await?;

    // Start discovery and let bt-host process the fake peers.
    test_state.aux().proxy().start_discovery().await?;

    // Wait for fake peer to be discovered.
    let expected_peer = expectation::peer::address(&address.to_string());
    expect_host_peer(&test_state, expected_peer.clone()).await?;

    // Connecting to the peer should return success and the peer should become connected.
    let mut status = test_state.aux().proxy().connect(&le_peer).await?;
    expect_true!(status.error.is_none())?;

    expect_host_peer(&test_state, expected_peer.and(expectation::peer::connected(true))).await?;

    // Forgetting the peer should result in its removal.
    status = test_state.aux().proxy().forget(&le_peer).await?;
    expect_true!(status.error.is_none())?;
    expect_no_peer(&test_state, le_peer).await?;

    // TODO(BT-879): Test that the link closes by querying fake HCI.

    Ok(())
}

/// Run all test cases.
pub fn run_all() -> Result<(), Error> {
    run_suite!(
        "bt-host driver",
        [
            test_lifecycle,
            test_bd_addr,
            test_set_local_name,
            test_discoverable,
            test_discovery,
            test_close,
            test_list_devices,
            test_connect,
            test_forget,
            test_disconnect_unknown_device,
            test_disconnect_unconnected_device,
            test_disconnect_connected_device
        ]
    )
}

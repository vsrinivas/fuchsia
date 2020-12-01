// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This entire library is actually only used to create a test binary, so mark the entire module to
// only build in test configuration
#![cfg(test)]

use {
    anyhow::{format_err, Context as _, Error},
    bt_test_harness::{
        emulator::{self, add_bredr_peer, add_le_peer, default_bredr_peer, default_le_peer},
        host_driver::{expectation as host_expectation, HostDriverHarness},
    },
    fidl_fuchsia_bluetooth::{self as fbt, DeviceClass, MAJOR_DEVICE_CLASS_TOY},
    fidl_fuchsia_bluetooth_host::HostProxy,
    fidl_fuchsia_bluetooth_sys::{self as fsys, TechnologyType},
    fidl_fuchsia_bluetooth_test::{EmulatorSettings, HciError, PeerProxy},
    fuchsia_async as fasync,
    fuchsia_bluetooth::{
        constants::{integration_timeout_duration, HOST_DEVICE_DIR},
        device_watcher::{DeviceWatcher, WatchFilter},
        expect_eq,
        expectation::{
            self,
            asynchronous::{ExpectableExt, ExpectableStateExt},
            peer,
        },
        hci_emulator::Emulator,
        host,
        types::{Address, HostInfo, PeerId},
    },
    fuchsia_zircon as zx,
    std::{convert::TryInto, path::PathBuf},
};

// Tests that creating and destroying a fake HCI device binds and unbinds the bt-host driver.
#[test_harness::run_singlethreaded_test]
async fn test_lifecycle(_: ()) -> Result<(), Error> {
    let address = Address::Public([1, 2, 3, 4, 5, 6]);
    let settings = EmulatorSettings {
        address: Some(address.to_fidl()),
        hci_config: None,
        extended_advertising: None,
        acl_buffer_settings: None,
        le_acl_buffer_settings: None,
        ..EmulatorSettings::EMPTY
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
    let info: HostInfo = host
        .watch_state()
        .await
        .context("Is bt-gap running? If so, try stopping it and re-running these tests")?
        .try_into()?;

    // The bt-host should have been initialized with the address that we initially configured.
    assert_eq!(address, info.address);

    // Remove the bt-hci device and check that the test device is also destroyed.
    emulator.destroy_and_wait().await?;

    // Check that the bt-host device is also destroyed.
    watcher.watch_removed(bthost.path()).await
}

// Tests that the bt-host driver assigns the local name to "fuchsia" when initialized.
#[test_harness::run_singlethreaded_test]
async fn test_default_local_name(harness: HostDriverHarness) -> Result<(), Error> {
    const NAME: &str = "fuchsia";
    let _ = harness
        .when_satisfied(emulator::expectation::local_name_is(NAME), integration_timeout_duration())
        .await?;
    host_expectation::host_state(&harness, expectation::host_driver::name(NAME)).await?;
    Ok(())
}

// Tests that the local name assigned to a bt-host is reflected in `AdapterState` and propagated
// down to the controller.
#[test_harness::run_singlethreaded_test]
async fn test_set_local_name(harness: HostDriverHarness) -> Result<(), Error> {
    const NAME: &str = "test1234";
    let proxy = harness.aux().host.clone();
    let result = proxy.set_local_name(NAME).await?;
    expect_eq!(Ok(()), result)?;

    let _ = harness
        .when_satisfied(emulator::expectation::local_name_is(NAME), integration_timeout_duration())
        .await?;
    host_expectation::host_state(&harness, expectation::host_driver::name(NAME)).await?;

    Ok(())
}

// Tests that the device class assigned to a bt-host gets propagated down to the controller.
#[test_harness::run_singlethreaded_test]
async fn test_set_device_class(harness: HostDriverHarness) -> Result<(), Error> {
    let mut device_class = DeviceClass { value: MAJOR_DEVICE_CLASS_TOY + 4 };
    let proxy = harness.aux().host.clone();
    let result = proxy.set_device_class(&mut device_class).await?;
    expect_eq!(Ok(()), result)?;

    let _ = harness
        .when_satisfied(
            emulator::expectation::device_class_is(device_class),
            integration_timeout_duration(),
        )
        .await?;
    Ok(())
}

// Tests that host state updates when discoverable mode is turned on.
// TODO(armansito): Test for FakeHciDevice state changes.
#[test_harness::run_singlethreaded_test]
async fn test_discoverable(harness: HostDriverHarness) -> Result<(), Error> {
    let proxy = harness.aux().host.clone();

    // Disabling discoverable mode when not discoverable should succeed.
    let result = proxy.set_discoverable(false).await?;
    expect_eq!(Ok(()), result)?;

    // Enable discoverable mode.
    let result = proxy.set_discoverable(true).await?;
    expect_eq!(Ok(()), result)?;
    host_expectation::host_state(&harness, expectation::host_driver::discoverable(true)).await?;

    // Disable discoverable mode
    let result = proxy.set_discoverable(false).await?;
    expect_eq!(Ok(()), result)?;
    host_expectation::host_state(&harness, expectation::host_driver::discoverable(false)).await?;

    // Disabling discoverable mode when not discoverable should succeed.
    let result = proxy.set_discoverable(false).await?;
    expect_eq!(Ok(()), result)?;

    Ok(())
}

// Tests that host state updates when discovery is started and stopped.
// TODO(armansito): Test for FakeHciDevice state changes.
#[test_harness::run_singlethreaded_test]
async fn test_discovery(harness: HostDriverHarness) -> Result<(), Error> {
    let proxy = harness.aux().host.clone();

    // Start discovery. "discovering" should get set to true.
    let result = proxy.start_discovery().await?;
    expect_eq!(Ok(()), result)?;
    host_expectation::host_state(&harness, expectation::host_driver::discovering(true)).await?;

    let address = Address::Random([1, 0, 0, 0, 0, 0]);
    let fut = add_le_peer(harness.aux().as_ref(), default_le_peer(&address));
    let _peer = fut.await?;

    // The host should discover a fake peer.
    host_expectation::peer(&harness, peer::name("Fake").and(peer::address(address))).await?;

    // Stop discovery. "discovering" should get set to false.
    let _ = proxy.stop_discovery()?;
    host_expectation::host_state(&harness, expectation::host_driver::discovering(false)).await?;

    Ok(())
}

// Tests that "close" cancels all operations.
// TODO(armansito): Test for FakeHciDevice state changes.
#[test_harness::run_singlethreaded_test]
async fn test_close(harness: HostDriverHarness) -> Result<(), Error> {
    // Enable all procedures.
    let proxy = harness.aux().host.clone();
    let result = proxy.start_discovery().await?;
    expect_eq!(Ok(()), result)?;
    let result = proxy.set_discoverable(true).await?;
    expect_eq!(Ok(()), result)?;

    let active_state = expectation::host_driver::discoverable(true)
        .and(expectation::host_driver::discovering(true));
    host_expectation::host_state(&harness, active_state).await?;

    // Close should cancel these procedures.
    proxy.close()?;

    let closed_state_update = expectation::host_driver::discoverable(false)
        .and(expectation::host_driver::discovering(false));

    host_expectation::host_state(&harness, closed_state_update).await?;

    Ok(())
}

#[test_harness::run_singlethreaded_test]
async fn test_watch_peers(harness: HostDriverHarness) -> Result<(), Error> {
    // `HostDriverHarness` internally calls `Host.WatchPeers()` to monitor peers and satisfy peer
    // expectations. `harness.peers()` represents the local cache monitored using this method.
    // Peers should be initially empty.
    expect_eq!(0, harness.write_state().peers().len())?;

    // Calling `Host.WatchPeers()` directly will hang since the harness already calls this
    // internally. We issue our own request and verify that it gets satisfied later.

    // Add a LE and a BR/EDR peer with the given addresses.
    let le_peer_address = Address::Random([1, 0, 0, 0, 0, 0]);
    let bredr_peer_address = Address::Public([2, 0, 0, 0, 0, 0]);

    let fut = add_le_peer(harness.aux().as_ref(), default_le_peer(&le_peer_address));
    let _le_peer = fut.await?;
    let fut = add_bredr_peer(harness.aux().as_ref(), default_bredr_peer(&bredr_peer_address));
    let _bredr_peer = fut.await?;

    // At this stage the fake peers are registered with the emulator but bt-host does not know about
    // them yet. Check that `watch_fut` is still unsatisfied.
    expect_eq!(0, harness.write_state().peers().len())?;

    // Wait for all fake devices to be discovered.
    let proxy = harness.aux().host.clone();
    let result = proxy.start_discovery().await?;
    expect_eq!(Ok(()), result)?;
    let expected_le =
        peer::address(le_peer_address).and(peer::technology(TechnologyType::LowEnergy));
    let expected_bredr =
        peer::address(bredr_peer_address).and(peer::technology(TechnologyType::Classic));

    host_expectation::peer(&harness, expected_le).await?;
    host_expectation::peer(&harness, expected_bredr).await?;
    expect_eq!(2, harness.write_state().peers().len())?;

    Ok(())
}

#[test_harness::run_singlethreaded_test]
async fn test_connect(harness: HostDriverHarness) -> Result<(), Error> {
    let address1 = Address::Random([1, 0, 0, 0, 0, 0]);
    let address2 = Address::Random([2, 0, 0, 0, 0, 0]);
    let fut = add_le_peer(harness.aux().as_ref(), default_le_peer(&address1));
    let _peer1 = fut.await?;
    let fut = add_le_peer(harness.aux().as_ref(), default_le_peer(&address2));
    let peer2 = fut.await?;

    // Configure `peer2` to return an error for the connection attempt.
    let _ = peer2.assign_connection_status(HciError::ConnectionTimeout).await?;

    let proxy = harness.aux().host.clone();

    // Start discovery and let bt-host process the fake devices.
    let result = proxy.start_discovery().await?;
    expect_eq!(Ok(()), result)?;

    host_expectation::peer(&harness, peer::address(address1)).await?;
    host_expectation::peer(&harness, peer::address(address2)).await?;

    let peers = harness.write_state().peers().clone();
    expect_eq!(2, peers.len())?;

    // Obtain bt-host assigned IDs of the devices.
    let success_id = peers
        .iter()
        .find(|x| x.1.address == address1)
        .ok_or(format_err!("success peer not found"))?
        .0
        .clone();
    let failure_id = peers
        .iter()
        .find(|x| x.1.address == address2)
        .ok_or(format_err!("error peer not found"))?
        .0
        .clone();
    let mut success_id: fbt::PeerId = success_id.into();
    let mut failure_id: fbt::PeerId = failure_id.into();

    // Connecting to the failure peer should result in an error.
    let status = proxy.connect(&mut failure_id).await?;
    expect_eq!(Err(fsys::Error::Failed), status)?;

    // Connecting to the success peer should return success and the peer should become connected.
    let status = proxy.connect(&mut success_id).await?;
    expect_eq!(Ok(()), status)?;

    let connected = peer::identifier(success_id.into()).and(peer::connected(true));
    host_expectation::peer(&harness, connected).await?;
    Ok(())
}

async fn wait_for_test_peer(
    harness: HostDriverHarness,
    address: &Address,
) -> Result<(PeerId, PeerProxy), Error> {
    let fut = add_le_peer(harness.aux().as_ref(), default_le_peer(&address));
    let proxy = fut.await?;

    // Start discovery and let bt-host process the fake LE peer.
    let host = harness.aux().host.clone();
    let result = host.start_discovery().await?;
    expect_eq!(Ok(()), result)?;

    let le_dev = expectation::peer::address(address.clone());
    host_expectation::peer(&harness, le_dev).await?;

    let peer_id = harness
        .write_state()
        .peers()
        .iter()
        .find(|(_, p)| p.address == *address)
        .ok_or(format_err!("could not find peer with address: {}", address))?
        .0
        .clone();
    Ok((peer_id, proxy))
}

// TODO(fxbug.dev/1525) - Add a test for disconnect failure when a connection attempt is outgoing, provided
// that we can provide a manner of doing so that will not flake.

/// Disconnecting from an unknown device should succeed
#[test_harness::run_singlethreaded_test]
async fn test_disconnect_unknown_device(harness: HostDriverHarness) -> Result<(), Error> {
    let mut unknown_id = PeerId(0).into();
    let fut = harness.aux().host.disconnect(&mut unknown_id);
    let status = fut.await?;
    expect_eq!(Ok(()), status)
}

/// Disconnecting from a known, unconnected device should succeed
#[test_harness::run_singlethreaded_test]
async fn test_disconnect_unconnected_device(harness: HostDriverHarness) -> Result<(), Error> {
    let address = Address::Random([1, 0, 0, 0, 0, 0]);
    let (id, _proxy) = wait_for_test_peer(harness.clone(), &address).await?;
    let mut id = id.into();
    let fut = harness.aux().host.disconnect(&mut id);
    let status = fut.await?;
    expect_eq!(Ok(()), status)
}

/// Disconnecting from a connected device should succeed and result in the device being disconnected
#[test_harness::run_singlethreaded_test]
async fn test_disconnect_connected_device(harness: HostDriverHarness) -> Result<(), Error> {
    let address = Address::Random([1, 0, 0, 0, 0, 0]);
    let (id, _proxy) = wait_for_test_peer(harness.clone(), &address).await?;
    let mut id = id.into();
    let proxy = harness.aux().host.clone();

    let status = proxy.connect(&mut id).await?;
    expect_eq!(Ok(()), status)?;

    let connected = peer::address(address).and(peer::connected(true));
    let disconnected = peer::address(address).and(peer::connected(false));

    let _ = host_expectation::peer(&harness, connected).await?;
    let status = proxy.disconnect(&mut id).await?;
    expect_eq!(Ok(()), status)?;

    let _ = host_expectation::peer(&harness, disconnected).await?;
    Ok(())
}

#[test_harness::run_singlethreaded_test]
async fn test_forget(harness: HostDriverHarness) -> Result<(), Error> {
    let address = Address::Random([1, 0, 0, 0, 0, 0]);
    let (id, _proxy) = wait_for_test_peer(harness.clone(), &address).await?;
    let mut id = id.into();
    let proxy = harness.aux().host.clone();

    // Wait for fake peer to be discovered (`wait_for_test_peer` starts discovery).
    let expected_peer = expectation::peer::address(address);
    host_expectation::peer(&harness, expected_peer.clone()).await?;

    // Connecting to the peer should return success and the peer should become connected.
    let status = proxy.connect(&mut id).await?;
    expect_eq!(Ok(()), status)?;
    host_expectation::peer(&harness, expected_peer.and(expectation::peer::connected(true))).await?;

    // Forgetting the peer should result in its removal.
    let status = proxy.forget(&mut id).await?;
    expect_eq!(Ok(()), status)?;
    host_expectation::no_peer(&harness, id.into()).await?;

    // TODO(fxbug.dev/1472): Test that the link closes by querying fake HCI.

    Ok(())
}

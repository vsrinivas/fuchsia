// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    anyhow::{format_err, Error},
    bt_test_harness::{emulator, low_energy_peripheral_v2::PeripheralHarness},
    fidl::endpoints::{create_endpoints, Proxy, ServerEnd},
    fidl_fuchsia_bluetooth::{ConnectionRole, Uuid},
    fidl_fuchsia_bluetooth_le::{
        AdvertisingData, AdvertisingHandleMarker, AdvertisingModeHint, AdvertisingParameters,
        PeripheralError, PeripheralStartAdvertisingResult as AdvertisingResult,
    },
    fidl_fuchsia_bluetooth_test::{
        ConnectionState, HciEmulatorProxy, LegacyAdvertisingType, LowEnergyPeerParameters,
        PeerProxy, MAX_LEGACY_ADVERTISING_DATA_LENGTH,
    },
    fuchsia_async::{self as fasync, DurationExt, TimeoutExt},
    fuchsia_bluetooth::{
        constants::INTEGRATION_TIMEOUT,
        expectation::asynchronous::{ExpectableExt, ExpectableState, ExpectableStateExt},
        types::Address,
    },
    futures::TryFutureExt,
    std::{iter::repeat, mem::drop, ops::Deref},
    test_harness,
};

mod expectation {
    use {
        bt_test_harness::low_energy_peripheral_v2::PeripheralState,
        fuchsia_bluetooth::expectation::Predicate,
    };

    pub fn peripheral_received_connection() -> Predicate<PeripheralState> {
        Predicate::predicate(
            move |state: &PeripheralState| -> bool { !state.connections.is_empty() },
            "le.Peripheral notified a connection",
        )
    }
}

fn empty_advertising_data() -> AdvertisingData {
    AdvertisingData {
        name: None,
        appearance: None,
        tx_power_level: None,
        service_uuids: None,
        service_data: None,
        manufacturer_data: None,
        uris: None,
        ..AdvertisingData::EMPTY
    }
}

async fn start_advertising(
    harness: &PeripheralHarness,
    params: AdvertisingParameters,
    handle: ServerEnd<AdvertisingHandleMarker>,
) -> Result<AdvertisingResult, Error> {
    let fut = harness
        .aux()
        .peripheral
        .start_advertising(params, handle)
        .map_err(|e| e.into())
        .on_timeout(INTEGRATION_TIMEOUT.after_now(), move || Err(format_err!("timed out")));
    fut.await.map_err(|e| e.context("Could not start advertising").into())
}

fn default_parameters() -> AdvertisingParameters {
    AdvertisingParameters {
        data: None,
        scan_response: None,
        mode_hint: None,
        connectable: None,
        connection_options: None,
        ..AdvertisingParameters::EMPTY
    }
}

#[test_harness::run_singlethreaded_test]
async fn test_enable_advertising(harness: PeripheralHarness) {
    let (_handle, handle_remote) = create_endpoints::<AdvertisingHandleMarker>().unwrap();
    let result = start_advertising(&harness, default_parameters(), handle_remote).await.unwrap();
    result.expect("failed to start advertising");

    let _ = harness
        .when_satisfied(emulator::expectation::advertising_is_enabled(true), INTEGRATION_TIMEOUT)
        .await
        .unwrap();
}

#[test_harness::run_singlethreaded_test]
async fn test_enable_and_disable_advertising(harness: PeripheralHarness) {
    let (handle, handle_remote) = create_endpoints::<AdvertisingHandleMarker>().unwrap();
    let result = start_advertising(&harness, default_parameters(), handle_remote).await.unwrap();
    result.expect("failed to start advertising");
    let _ = harness
        .when_satisfied(emulator::expectation::advertising_is_enabled(true), INTEGRATION_TIMEOUT)
        .await
        .unwrap();

    // Closing the advertising handle should stop advertising.
    drop(handle);
    let _ = harness
        .when_satisfied(emulator::expectation::advertising_is_enabled(false), INTEGRATION_TIMEOUT)
        .await
        .unwrap();
}

#[test_harness::run_singlethreaded_test]
async fn test_advertising_handle_closed_while_pending(harness: PeripheralHarness) {
    let (handle, handle_remote) = create_endpoints::<AdvertisingHandleMarker>().unwrap();

    // Drop the handle before getting a response to abort the procedure.
    drop(handle);
    let result = start_advertising(&harness, default_parameters(), handle_remote).await.unwrap();
    result.expect("failed to start advertising");

    // Advertising should become disabled after getting enabled once.
    let _ = harness
        .when_satisfied(
            emulator::expectation::advertising_was_enabled(true)
                .and(emulator::expectation::advertising_is_enabled(false)),
            INTEGRATION_TIMEOUT,
        )
        .await
        .unwrap();
}

#[test_harness::run_singlethreaded_test]
async fn test_advertising_data_too_long(harness: PeripheralHarness) {
    const LENGTH: usize = (MAX_LEGACY_ADVERTISING_DATA_LENGTH + 1) as usize;
    let (_handle, handle_remote) = create_endpoints::<AdvertisingHandleMarker>().unwrap();

    // Assign a very long name.
    let mut params = default_parameters();
    params.data = Some(AdvertisingData {
        name: Some(repeat("x").take(LENGTH).collect::<String>()),
        ..empty_advertising_data()
    });
    let result = start_advertising(&harness, params, handle_remote).await.unwrap();
    assert_eq!(Err(PeripheralError::AdvertisingDataTooLong), result);
}

#[test_harness::run_singlethreaded_test]
async fn test_scan_response_data_too_long(harness: PeripheralHarness) {
    const LENGTH: usize = (MAX_LEGACY_ADVERTISING_DATA_LENGTH + 1) as usize;
    let (_handle, handle_remote) = create_endpoints::<AdvertisingHandleMarker>().unwrap();

    // Assign a very long name.
    let mut params = default_parameters();
    params.scan_response = Some(AdvertisingData {
        name: Some(repeat("x").take(LENGTH).collect::<String>()),
        ..empty_advertising_data()
    });
    let result = start_advertising(&harness, params, handle_remote).await.unwrap();
    assert_eq!(Err(PeripheralError::ScanResponseDataTooLong), result);
}

#[test_harness::run_singlethreaded_test]
async fn test_update_advertising(harness: PeripheralHarness) {
    let (_handle, handle_remote) = create_endpoints::<AdvertisingHandleMarker>().unwrap();
    let result = start_advertising(&harness, default_parameters(), handle_remote).await.unwrap();
    result.expect("failed to start advertising");
    let _ = harness
        .when_satisfied(emulator::expectation::advertising_is_enabled(true), INTEGRATION_TIMEOUT)
        .await
        .unwrap();
    let _ = harness
        .when_satisfied(
            emulator::expectation::advertising_type_is(LegacyAdvertisingType::AdvNonconnInd),
            INTEGRATION_TIMEOUT,
        )
        .await
        .unwrap();
    harness.write_state().reset();

    // Call `start_advertising` again with new parameters.
    let mut params = default_parameters();
    params.connectable = Some(true);
    let (_handle2, handle_remote) = create_endpoints::<AdvertisingHandleMarker>().unwrap();
    let result = start_advertising(&harness, params, handle_remote).await.unwrap();
    result.expect("failed to start advertising");

    // Advertising should stop and start with the new parameters.
    let _ = harness
        .when_satisfied(
            emulator::expectation::advertising_was_enabled(false)
                .and(emulator::expectation::advertising_is_enabled(true)),
            INTEGRATION_TIMEOUT,
        )
        .await
        .unwrap();
    let _ = harness
        .when_satisfied(
            emulator::expectation::advertising_type_is(LegacyAdvertisingType::AdvInd),
            INTEGRATION_TIMEOUT,
        )
        .await
        .unwrap();
}

#[test_harness::run_singlethreaded_test]
async fn test_advertising_types(harness: PeripheralHarness) {
    // Non-connectable
    let params = AdvertisingParameters { connectable: Some(false), ..default_parameters() };
    let (_handle, handle_remote) = create_endpoints::<AdvertisingHandleMarker>().unwrap();
    let result = start_advertising(&harness, params, handle_remote).await.unwrap();
    result.expect("failed to start advertising");
    let _ = harness
        .when_satisfied(
            emulator::expectation::advertising_type_is(LegacyAdvertisingType::AdvNonconnInd),
            INTEGRATION_TIMEOUT,
        )
        .await
        .unwrap();

    // Connectable
    let params = AdvertisingParameters { connectable: Some(true), ..default_parameters() };
    let (_handle, handle_remote) = create_endpoints::<AdvertisingHandleMarker>().unwrap();
    let result = start_advertising(&harness, params, handle_remote).await.unwrap();
    result.expect("failed to start advertising");
    let _ = harness
        .when_satisfied(
            emulator::expectation::advertising_type_is(LegacyAdvertisingType::AdvInd),
            INTEGRATION_TIMEOUT,
        )
        .await
        .unwrap();

    // Scannable
    let params = AdvertisingParameters {
        connectable: Some(false),
        scan_response: Some(AdvertisingData {
            name: Some("hello".to_string()),
            ..empty_advertising_data()
        }),
        ..default_parameters()
    };
    let (_handle, handle_remote) = create_endpoints::<AdvertisingHandleMarker>().unwrap();
    let result = start_advertising(&harness, params, handle_remote).await.unwrap();
    result.expect("failed to start advertising");
    let _ = harness
        .when_satisfied(
            emulator::expectation::advertising_type_is(LegacyAdvertisingType::AdvScanInd),
            INTEGRATION_TIMEOUT,
        )
        .await
        .unwrap();

    // Connectable and scannable
    let params = AdvertisingParameters {
        connectable: Some(true),
        scan_response: Some(AdvertisingData {
            name: Some("hello".to_string()),
            ..empty_advertising_data()
        }),
        ..default_parameters()
    };
    let (_handle, handle_remote) = create_endpoints::<AdvertisingHandleMarker>().unwrap();
    let result = start_advertising(&harness, params, handle_remote).await.unwrap();
    result.expect("failed to start advertising");
    let _ = harness
        .when_satisfied(
            emulator::expectation::advertising_type_is(LegacyAdvertisingType::AdvInd),
            INTEGRATION_TIMEOUT,
        )
        .await
        .unwrap();
}

#[test_harness::run_singlethreaded_test]
async fn test_advertising_modes(harness: PeripheralHarness) {
    // Very fast advertising interval (<= 60 ms), only supported for connectable advertising.
    let params = AdvertisingParameters {
        connectable: Some(true),
        mode_hint: Some(AdvertisingModeHint::VeryFast),
        ..default_parameters()
    };
    let (_handle, handle_remote) = create_endpoints::<AdvertisingHandleMarker>().unwrap();
    let result = start_advertising(&harness, params, handle_remote).await.unwrap();
    result.expect("failed to start advertising");
    let _ = harness
        .when_satisfied(emulator::expectation::advertising_max_interval_is(60), INTEGRATION_TIMEOUT)
        .await
        .unwrap();

    // Very fast advertising interval (<= 60 ms) falls back to "fast" parameters for non-connectable
    // advertising.
    let params = AdvertisingParameters {
        mode_hint: Some(AdvertisingModeHint::VeryFast),
        ..default_parameters()
    };
    let (_handle, handle_remote) = create_endpoints::<AdvertisingHandleMarker>().unwrap();
    let result = start_advertising(&harness, params, handle_remote).await.unwrap();
    result.expect("failed to start advertising");
    let _ = harness
        .when_satisfied(
            emulator::expectation::advertising_max_interval_is(150),
            INTEGRATION_TIMEOUT,
        )
        .await
        .unwrap();

    // Fast advertising interval (<= 150 ms)
    let params = AdvertisingParameters {
        mode_hint: Some(AdvertisingModeHint::Fast),
        ..default_parameters()
    };
    let (_handle, handle_remote) = create_endpoints::<AdvertisingHandleMarker>().unwrap();
    let result = start_advertising(&harness, params, handle_remote).await.unwrap();
    result.expect("failed to start advertising");
    let _ = harness
        .when_satisfied(
            emulator::expectation::advertising_max_interval_is(150),
            INTEGRATION_TIMEOUT,
        )
        .await
        .unwrap();

    // Slow advertising interval (<= 1.2 s)
    let params = AdvertisingParameters {
        mode_hint: Some(AdvertisingModeHint::Slow),
        ..default_parameters()
    };
    let (_handle, handle_remote) = create_endpoints::<AdvertisingHandleMarker>().unwrap();
    let result = start_advertising(&harness, params, handle_remote).await.unwrap();
    result.expect("failed to start advertising");
    let _ = harness
        .when_satisfied(
            emulator::expectation::advertising_max_interval_is(1200),
            INTEGRATION_TIMEOUT,
        )
        .await
        .unwrap();
}

#[test_harness::run_singlethreaded_test]
async fn test_advertising_data(harness: PeripheralHarness) {
    // Test that encoding one field works. The serialization of other fields is unit tested elsewhere.
    let params = AdvertisingParameters {
        data: Some(AdvertisingData { name: Some("hello".to_string()), ..empty_advertising_data() }),
        ..default_parameters()
    };
    let (_handle, handle_remote) = create_endpoints::<AdvertisingHandleMarker>().unwrap();
    let result = start_advertising(&harness, params, handle_remote).await.unwrap();
    result.expect("failed to start advertising");

    let data = vec![
        // Flags (General discoverable mode)
        0x02,
        0x01,
        0x02,
        // The local name, as above.
        0x06,
        0x09,
        ('h' as u8),
        ('e' as u8),
        ('l' as u8),
        ('l' as u8),
        ('o' as u8),
    ];
    let _ = harness
        .when_satisfied(
            emulator::expectation::advertising_is_enabled(true)
                .and(emulator::expectation::advertising_data_is(data)),
            INTEGRATION_TIMEOUT,
        )
        .await
        .unwrap();
}

#[test_harness::run_singlethreaded_test]
async fn test_scan_response(harness: PeripheralHarness) {
    // Test that encoding one field works. The serialization of other fields is unit tested elsewhere.
    let params = AdvertisingParameters {
        data: Some(AdvertisingData {
            service_uuids: Some(vec![Uuid {
                value: [
                    0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0x0d,
                    0x18, 0x00, 0x00,
                ],
            }]),
            ..empty_advertising_data()
        }),
        scan_response: Some(AdvertisingData {
            name: Some("hello".to_string()),
            ..empty_advertising_data()
        }),
        ..default_parameters()
    };
    let (_handle, handle_remote) = create_endpoints::<AdvertisingHandleMarker>().unwrap();
    let result = start_advertising(&harness, params, handle_remote).await.unwrap();
    result.expect("failed to start advertising");

    let data = vec![
        0x02, 0x01, 0x02, // Flags (General discoverable mode)
        0x03, 0x02, 0x0d, 0x18, // Incomplete list of service UUIDs
    ];
    let scan_rsp = vec![
        // The local name, as above.
        0x06,
        0x09,
        ('h' as u8),
        ('e' as u8),
        ('l' as u8),
        ('l' as u8),
        ('o' as u8),
    ];
    let _ = harness
        .when_satisfied(
            emulator::expectation::advertising_is_enabled(true)
                .and(emulator::expectation::advertising_data_is(data))
                .and(emulator::expectation::scan_response_is(scan_rsp)),
            INTEGRATION_TIMEOUT,
        )
        .await
        .unwrap();
}

fn default_address() -> Address {
    Address::Public([1, 0, 0, 0, 0, 0])
}

async fn add_fake_peer(proxy: &HciEmulatorProxy, address: &Address) -> Result<PeerProxy, Error> {
    let (local, remote) = fidl::endpoints::create_proxy()?;
    let params = LowEnergyPeerParameters {
        address: Some(address.into()),
        connectable: Some(true),
        advertisement: None,
        scan_response: None,
        ..LowEnergyPeerParameters::EMPTY
    };
    let _ = proxy
        .add_low_energy_peer(params, remote)
        .await?
        .map_err(|e| format_err!("Failed to register fake peer: {:?}", e))?;
    Ok(local)
}

#[test_harness::run_singlethreaded_test]
async fn test_receive_connection(harness: PeripheralHarness) {
    let emulator = harness.aux().as_ref().clone();
    let address = default_address();
    let peer = add_fake_peer(&emulator, &address).await.unwrap();
    let (handle, handle_remote) = create_endpoints::<AdvertisingHandleMarker>().unwrap();

    let mut params = default_parameters();
    params.connectable = Some(true);

    let result = start_advertising(&harness, params, handle_remote).await.unwrap();
    result.expect("failed to start advertising");
    let _ = harness
        .when_satisfied(emulator::expectation::advertising_is_enabled(true), INTEGRATION_TIMEOUT)
        .await
        .unwrap();

    peer.emulate_le_connection_complete(ConnectionRole::Follower).unwrap();
    let _ = harness
        .when_satisfied(expectation::peripheral_received_connection(), INTEGRATION_TIMEOUT)
        .await
        .unwrap();

    // Receiving a connection is expected to stop advertising. Verify that the emulator no longer
    // advertises.
    let _ = harness
        .when_satisfied(emulator::expectation::advertising_is_enabled(false), INTEGRATION_TIMEOUT)
        .await
        .unwrap();

    // Similarly our AdvertisingHandle should be closed by the system.
    let handle = handle.into_proxy().unwrap();
    let _ = handle.on_closed().await.unwrap();
}

#[test_harness::run_singlethreaded_test]
async fn test_connection_dropped_when_not_connectable(harness: PeripheralHarness) {
    let emulator = harness.aux().as_ref().clone();
    let address = default_address();
    let peer = add_fake_peer(&emulator, &address).await.unwrap();
    let (_handle, handle_remote) = create_endpoints::<AdvertisingHandleMarker>().unwrap();

    // `default_parameters()` are configured as non-connectable.
    let result = start_advertising(&harness, default_parameters(), handle_remote).await.unwrap();
    result.expect("failed to start advertising");
    let _ = harness
        .when_satisfied(emulator::expectation::advertising_is_enabled(true), INTEGRATION_TIMEOUT)
        .await
        .unwrap();

    peer.emulate_le_connection_complete(ConnectionRole::Follower).unwrap();

    // Wait for the connection to get dropped by the stack as it should be rejected when we are not
    // connectable. We assign our own PeerId here for tracking purposes (this is distinct from the
    // PeerId that the Peripheral proxy would report).
    fasync::Task::spawn(
        emulator::watch_peer_connection_states(harness.deref().clone(), address, peer.clone())
            .unwrap_or_else(|_| ()),
    )
    .detach();

    let _ = harness
        .when_satisfied(
            emulator::expectation::peer_connection_state_was(address, ConnectionState::Connected)
                .and(emulator::expectation::peer_connection_state_is(
                    address,
                    ConnectionState::Disconnected,
                )),
            INTEGRATION_TIMEOUT,
        )
        .await
        .unwrap();

    // Make sure that we haven't received any connection events over the Peripheral protocol.
    assert!(harness.read().connections.is_empty());
}

#[test_harness::run_singlethreaded_test]
async fn test_drop_connection(harness: PeripheralHarness) {
    let emulator = harness.aux().as_ref().clone();
    let address = default_address();
    let peer = add_fake_peer(&emulator, &address).await.unwrap();
    let (_handle, handle_remote) = create_endpoints::<AdvertisingHandleMarker>().unwrap();

    let mut params = default_parameters();
    params.connectable = Some(true);

    let result = start_advertising(&harness, params, handle_remote).await.unwrap();
    result.expect("failed to start advertising");

    peer.emulate_le_connection_complete(ConnectionRole::Follower).unwrap();
    fasync::Task::spawn(
        emulator::watch_peer_connection_states(harness.deref().clone(), address, peer.clone())
            .unwrap_or_else(|_| ()),
    )
    .detach();

    let _ = harness
        .when_satisfied(expectation::peripheral_received_connection(), INTEGRATION_TIMEOUT)
        .await
        .unwrap();

    assert!(harness.read().connections.len() == 1);
    let (_, conn) = harness.write_state().connections.remove(0);

    // Explicitly drop the connection handle. This should tell the emulator to disconnect the peer.
    drop(conn);
    let _ = harness
        .when_satisfied(
            emulator::expectation::peer_connection_state_was(address, ConnectionState::Connected)
                .and(emulator::expectation::peer_connection_state_is(
                    address,
                    ConnectionState::Disconnected,
                )),
            INTEGRATION_TIMEOUT,
        )
        .await
        .unwrap();
}

#[test_harness::run_singlethreaded_test]
async fn test_connection_handle_closes_on_disconnect(harness: PeripheralHarness) {
    let emulator = harness.aux().as_ref().clone();
    let address = default_address();
    let peer = add_fake_peer(&emulator, &address).await.unwrap();
    let (_handle, handle_remote) = create_endpoints::<AdvertisingHandleMarker>().unwrap();

    let mut params = default_parameters();
    params.connectable = Some(true);

    let result = start_advertising(&harness, params, handle_remote).await.unwrap();
    result.expect("failed to start advertising");

    peer.emulate_le_connection_complete(ConnectionRole::Follower).unwrap();
    fasync::Task::spawn(
        emulator::watch_peer_connection_states(harness.deref().clone(), address, peer.clone())
            .unwrap_or_else(|_| ()),
    )
    .detach();

    let _ = harness
        .when_satisfied(expectation::peripheral_received_connection(), INTEGRATION_TIMEOUT)
        .await
        .unwrap();

    assert!(harness.read().connections.len() == 1);
    let (_, conn) = harness.write_state().connections.remove(0);

    // Tell the controller to disconnect the link. The harness should get notified of this.
    peer.emulate_disconnection_complete().unwrap();
    let _ = harness
        .when_satisfied(
            emulator::expectation::peer_connection_state_was(address, ConnectionState::Connected)
                .and(emulator::expectation::peer_connection_state_is(
                    address,
                    ConnectionState::Disconnected,
                )),
            INTEGRATION_TIMEOUT,
        )
        .await
        .unwrap();

    // Our connection handle should be closed by the system.
    let _ = conn.on_closed().await.unwrap();
}

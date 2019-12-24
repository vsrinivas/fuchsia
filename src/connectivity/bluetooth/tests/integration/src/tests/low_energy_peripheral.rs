// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    fidl::endpoints::{create_endpoints, ServerEnd},
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
        expectation::asynchronous::{ExpectableState, ExpectableStateExt},
        types::Address,
    },
    fuchsia_zircon::{Duration, DurationNum},
    futures::{Future, Stream, TryFutureExt, TryStream, TryStreamExt},
    std::{iter::repeat, mem::drop},
};

use crate::harness::{
    emulator,
    expect::{expect_eq, expect_ok},
    low_energy_peripheral::PeripheralHarness,
};

mod expectation {
    use {
        crate::harness::low_energy_peripheral::PeripheralState,
        fuchsia_bluetooth::expectation::Predicate,
    };

    pub fn peripheral_received_connection() -> Predicate<PeripheralState> {
        Predicate::new(
            move |state: &PeripheralState| -> bool { !state.connections.is_empty() },
            Some("le.Peripheral notified a connection"),
        )
    }
}

fn test_timeout() -> Duration {
    10.seconds()
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
    }
}

async fn start_advertising(
    harness: &PeripheralHarness,
    params: AdvertisingParameters,
    handle: ServerEnd<AdvertisingHandleMarker>,
) -> Result<AdvertisingResult, Error> {
    let fut = harness
        .aux()
        .proxy()
        .start_advertising(params, handle)
        .map_err(|e| e.into())
        .on_timeout(test_timeout().after_now(), move || Err(format_err!("timed out")));
    fut.await.map_err(|e| e.context("Could not start advertising").into())
}

fn default_parameters() -> AdvertisingParameters {
    AdvertisingParameters { data: None, scan_response: None, mode_hint: None, connectable: None }
}

async fn test_enable_advertising(harness: PeripheralHarness) -> Result<(), Error> {
    let (_handle, handle_remote) = create_endpoints::<AdvertisingHandleMarker>()?;
    let result = start_advertising(&harness, default_parameters(), handle_remote).await?;
    expect_ok(result, "failed to start advertising")?;

    let _ = harness
        .when_satisfied(emulator::expectation::advertising_is_enabled(true), test_timeout())
        .await?;
    Ok(())
}

async fn test_enable_and_disable_advertising(harness: PeripheralHarness) -> Result<(), Error> {
    let (handle, handle_remote) = create_endpoints::<AdvertisingHandleMarker>()?;
    let result = start_advertising(&harness, default_parameters(), handle_remote).await?;
    expect_ok(result, "failed to start advertising")?;
    let _ = harness
        .when_satisfied(emulator::expectation::advertising_is_enabled(true), test_timeout())
        .await?;

    // Closing the advertising handle should stop advertising.
    drop(handle);
    let _ = harness
        .when_satisfied(emulator::expectation::advertising_is_enabled(false), test_timeout())
        .await?;
    Ok(())
}

async fn test_advertising_handle_closed_while_pending(
    harness: PeripheralHarness,
) -> Result<(), Error> {
    let (handle, handle_remote) = create_endpoints::<AdvertisingHandleMarker>()?;

    // Drop the handle before getting a response to abort the procedure.
    drop(handle);
    let result = start_advertising(&harness, default_parameters(), handle_remote).await?;
    expect_ok(result, "failed to start advertising")?;

    // Advertising should become disabled after getting enabled once.
    let _ = harness
        .when_satisfied(
            emulator::expectation::advertising_was_enabled(true)
                .and(emulator::expectation::advertising_is_enabled(false)),
            test_timeout(),
        )
        .await?;
    Ok(())
}

async fn test_advertising_data_too_long(harness: PeripheralHarness) -> Result<(), Error> {
    const LENGTH: usize = (MAX_LEGACY_ADVERTISING_DATA_LENGTH + 1) as usize;
    let (_handle, handle_remote) = create_endpoints::<AdvertisingHandleMarker>()?;

    // Assign a very long name.
    let mut params = default_parameters();
    params.data = Some(AdvertisingData {
        name: Some(repeat("x").take(LENGTH).collect::<String>()),
        ..empty_advertising_data()
    });
    let result = start_advertising(&harness, params, handle_remote).await?;
    expect_eq!(Err(PeripheralError::AdvertisingDataTooLong), result)?;
    Ok(())
}

async fn test_scan_response_data_too_long(harness: PeripheralHarness) -> Result<(), Error> {
    const LENGTH: usize = (MAX_LEGACY_ADVERTISING_DATA_LENGTH + 1) as usize;
    let (_handle, handle_remote) = create_endpoints::<AdvertisingHandleMarker>()?;

    // Assign a very long name.
    let mut params = default_parameters();
    params.scan_response = Some(AdvertisingData {
        name: Some(repeat("x").take(LENGTH).collect::<String>()),
        ..empty_advertising_data()
    });
    let result = start_advertising(&harness, params, handle_remote).await?;
    expect_eq!(Err(PeripheralError::ScanResponseDataTooLong), result)?;
    Ok(())
}

async fn test_update_advertising(harness: PeripheralHarness) -> Result<(), Error> {
    let (_handle, handle_remote) = create_endpoints::<AdvertisingHandleMarker>()?;
    let result = start_advertising(&harness, default_parameters(), handle_remote).await?;
    expect_ok(result, "failed to start advertising")?;
    let _ = harness
        .when_satisfied(emulator::expectation::advertising_is_enabled(true), test_timeout())
        .await?;
    let _ = harness
        .when_satisfied(
            emulator::expectation::advertising_type_is(LegacyAdvertisingType::AdvNonconnInd),
            test_timeout(),
        )
        .await?;
    harness.write_state().reset();

    // Call `start_advertising` again with new parameters.
    let mut params = default_parameters();
    params.connectable = Some(true);
    let (_handle2, handle_remote) = create_endpoints::<AdvertisingHandleMarker>()?;
    let result = start_advertising(&harness, params, handle_remote).await?;
    expect_ok(result, "failed to start advertising")?;

    // Advertising should stop and start with the new parameters.
    let _ = harness
        .when_satisfied(
            emulator::expectation::advertising_was_enabled(false)
                .and(emulator::expectation::advertising_is_enabled(true)),
            test_timeout(),
        )
        .await?;
    let _ = harness
        .when_satisfied(
            emulator::expectation::advertising_type_is(LegacyAdvertisingType::AdvInd),
            test_timeout(),
        )
        .await?;

    Ok(())
}

async fn test_advertising_types(harness: PeripheralHarness) -> Result<(), Error> {
    // Non-connectable
    let params = AdvertisingParameters { connectable: Some(false), ..default_parameters() };
    let (_handle, handle_remote) = create_endpoints::<AdvertisingHandleMarker>()?;
    let result = start_advertising(&harness, params, handle_remote).await?;
    expect_ok(result, "failed to start advertising")?;
    let _ = harness
        .when_satisfied(
            emulator::expectation::advertising_type_is(LegacyAdvertisingType::AdvNonconnInd),
            test_timeout(),
        )
        .await?;

    // Connectable
    let params = AdvertisingParameters { connectable: Some(true), ..default_parameters() };
    let (_handle, handle_remote) = create_endpoints::<AdvertisingHandleMarker>()?;
    let result = start_advertising(&harness, params, handle_remote).await?;
    expect_ok(result, "failed to start advertising")?;
    let _ = harness
        .when_satisfied(
            emulator::expectation::advertising_type_is(LegacyAdvertisingType::AdvInd),
            test_timeout(),
        )
        .await?;

    // Scannable
    let params = AdvertisingParameters {
        connectable: Some(false),
        scan_response: Some(AdvertisingData {
            name: Some("hello".to_string()),
            ..empty_advertising_data()
        }),
        ..default_parameters()
    };
    let (_handle, handle_remote) = create_endpoints::<AdvertisingHandleMarker>()?;
    let result = start_advertising(&harness, params, handle_remote).await?;
    expect_ok(result, "failed to start advertising")?;
    let _ = harness
        .when_satisfied(
            emulator::expectation::advertising_type_is(LegacyAdvertisingType::AdvScanInd),
            test_timeout(),
        )
        .await?;

    // Connectable and scannable
    let params = AdvertisingParameters {
        connectable: Some(true),
        scan_response: Some(AdvertisingData {
            name: Some("hello".to_string()),
            ..empty_advertising_data()
        }),
        ..default_parameters()
    };
    let (_handle, handle_remote) = create_endpoints::<AdvertisingHandleMarker>()?;
    let result = start_advertising(&harness, params, handle_remote).await?;
    expect_ok(result, "failed to start advertising")?;
    let _ = harness
        .when_satisfied(
            emulator::expectation::advertising_type_is(LegacyAdvertisingType::AdvInd),
            test_timeout(),
        )
        .await?;

    Ok(())
}

async fn test_advertising_modes(harness: PeripheralHarness) -> Result<(), Error> {
    // Very fast advertising interval (<= 60 ms), only supported for connectable advertising.
    let params = AdvertisingParameters {
        connectable: Some(true),
        mode_hint: Some(AdvertisingModeHint::VeryFast),
        ..default_parameters()
    };
    let (_handle, handle_remote) = create_endpoints::<AdvertisingHandleMarker>()?;
    let result = start_advertising(&harness, params, handle_remote).await?;
    expect_ok(result, "failed to start advertising")?;
    let _ = harness
        .when_satisfied(emulator::expectation::advertising_max_interval_is(60), test_timeout())
        .await?;

    // Very fast advertising interval (<= 60 ms) falls back to "fast" parameters for non-connectable
    // advertising.
    let params = AdvertisingParameters {
        mode_hint: Some(AdvertisingModeHint::VeryFast),
        ..default_parameters()
    };
    let (_handle, handle_remote) = create_endpoints::<AdvertisingHandleMarker>()?;
    let result = start_advertising(&harness, params, handle_remote).await?;
    expect_ok(result, "failed to start advertising")?;
    let _ = harness
        .when_satisfied(emulator::expectation::advertising_max_interval_is(150), test_timeout())
        .await?;

    // Fast advertising interval (<= 150 ms)
    let params = AdvertisingParameters {
        mode_hint: Some(AdvertisingModeHint::Fast),
        ..default_parameters()
    };
    let (_handle, handle_remote) = create_endpoints::<AdvertisingHandleMarker>()?;
    let result = start_advertising(&harness, params, handle_remote).await?;
    expect_ok(result, "failed to start advertising")?;
    let _ = harness
        .when_satisfied(emulator::expectation::advertising_max_interval_is(150), test_timeout())
        .await?;

    // Slow advertising interval (<= 1.2 s)
    let params = AdvertisingParameters {
        mode_hint: Some(AdvertisingModeHint::Slow),
        ..default_parameters()
    };
    let (_handle, handle_remote) = create_endpoints::<AdvertisingHandleMarker>()?;
    let result = start_advertising(&harness, params, handle_remote).await?;
    expect_ok(result, "failed to start advertising")?;
    let _ = harness
        .when_satisfied(emulator::expectation::advertising_max_interval_is(1200), test_timeout())
        .await?;

    Ok(())
}

async fn test_advertising_data(harness: PeripheralHarness) -> Result<(), Error> {
    // Test that encoding one field works. The serialization of other fields is unit tested elsewhere.
    let params = AdvertisingParameters {
        data: Some(AdvertisingData { name: Some("hello".to_string()), ..empty_advertising_data() }),
        ..default_parameters()
    };
    let (_handle, handle_remote) = create_endpoints::<AdvertisingHandleMarker>()?;
    let result = start_advertising(&harness, params, handle_remote).await?;
    expect_ok(result, "failed to start advertising")?;

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
            test_timeout(),
        )
        .await?;

    Ok(())
}

async fn test_scan_response(harness: PeripheralHarness) -> Result<(), Error> {
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
    let (_handle, handle_remote) = create_endpoints::<AdvertisingHandleMarker>()?;
    let result = start_advertising(&harness, params, handle_remote).await?;
    expect_ok(result, "failed to start advertising")?;

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
            test_timeout(),
        )
        .await?;

    Ok(())
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
    };
    let _ = proxy
        .add_low_energy_peer(params, remote)
        .await?
        .map_err(|e| format_err!("Failed to register fake peer: {:?}", e))?;
    Ok(local)
}

// Returns success when the `events` stream returns a "None" or fails. (This is what FIDL handles do
// when their peer gets closed). Returns an error if the timeout expires before then.
fn watch_handle_closed<T, V>(mut events: T) -> impl Future<Output = Result<(), Error>>
where
    T: Stream<Item = Result<V, fidl::Error>>
        + Send
        + std::marker::Unpin
        + TryStream<Error = fidl::Error>
        + TryStreamExt,
{
    let f = async move {
        while events.try_next().await?.is_some() {}
        Ok(())
    };
    f.on_timeout(test_timeout().after_now(), || Err(format_err!("Timed out before handle closed")))
}

async fn test_receive_connection(harness: PeripheralHarness) -> Result<(), Error> {
    let emulator = harness.aux().emulator().clone();
    let address = default_address();
    let peer = add_fake_peer(&emulator, &address).await?;
    let (handle, handle_remote) = create_endpoints::<AdvertisingHandleMarker>()?;

    let mut params = default_parameters();
    params.connectable = Some(true);

    let result = start_advertising(&harness, params, handle_remote).await?;
    expect_ok(result, "failed to start advertising")?;
    let _ = harness
        .when_satisfied(emulator::expectation::advertising_is_enabled(true), test_timeout())
        .await?;

    peer.emulate_le_connection_complete(ConnectionRole::Follower)?;
    let _ = harness
        .when_satisfied(expectation::peripheral_received_connection(), test_timeout())
        .await?;

    // Receiving a connection is expected to stop advertising. Verify that the emulator no longer
    // advertises.
    let _ = harness
        .when_satisfied(emulator::expectation::advertising_is_enabled(false), test_timeout())
        .await?;

    // Similarly our AdvertisingHandle should be closed by the system. Polling for events should
    // result in an error. Keep a local copy to ensure that the handle doesn't get dropped
    // locally.
    let handle = handle.into_proxy()?;
    watch_handle_closed(handle.clone().take_event_stream()).await
}

async fn test_connection_dropped_when_not_connectable(
    harness: PeripheralHarness,
) -> Result<(), Error> {
    let emulator = harness.aux().emulator().clone();
    let address = default_address();
    let peer = add_fake_peer(&emulator, &address).await?;
    let (_handle, handle_remote) = create_endpoints::<AdvertisingHandleMarker>()?;

    // `default_parameters()` are configured as non-connectable.
    let result = start_advertising(&harness, default_parameters(), handle_remote).await?;
    expect_ok(result, "failed to start advertising")?;
    let _ = harness
        .when_satisfied(emulator::expectation::advertising_is_enabled(true), test_timeout())
        .await?;

    peer.emulate_le_connection_complete(ConnectionRole::Follower)?;

    // Wait for the connection to get dropped by the stack as it should be rejected when we are not
    // connectable. We assign our own PeerId here for tracking purposes (this is distinct from the
    // PeerId that the Peripheral proxy would report).
    fasync::spawn(
        emulator::watch_peer_connection_states(harness.clone(), address, peer.clone())
            .unwrap_or_else(|_| ()),
    );

    let _ = harness
        .when_satisfied(
            emulator::expectation::peer_connection_state_was(address, ConnectionState::Connected)
                .and(emulator::expectation::peer_connection_state_is(
                    address,
                    ConnectionState::Disconnected,
                )),
            test_timeout(),
        )
        .await?;

    // Make sure that we haven't received any connection events over the Peripheral protocol.
    expect_true!(harness.read().connections.is_empty())?;

    Ok(())
}

async fn test_drop_connection(harness: PeripheralHarness) -> Result<(), Error> {
    let emulator = harness.aux().emulator().clone();
    let address = default_address();
    let peer = add_fake_peer(&emulator, &address).await?;
    let (_handle, handle_remote) = create_endpoints::<AdvertisingHandleMarker>()?;

    let mut params = default_parameters();
    params.connectable = Some(true);

    let result = start_advertising(&harness, params, handle_remote).await?;
    expect_ok(result, "failed to start advertising")?;

    peer.emulate_le_connection_complete(ConnectionRole::Follower)?;
    fasync::spawn(
        emulator::watch_peer_connection_states(harness.clone(), address, peer.clone())
            .unwrap_or_else(|_| ()),
    );

    let _ = harness
        .when_satisfied(expectation::peripheral_received_connection(), test_timeout())
        .await?;

    expect_true!(harness.read().connections.len() == 1)?;
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
            test_timeout(),
        )
        .await?;

    Ok(())
}

async fn test_connection_handle_closes_on_disconnect(
    harness: PeripheralHarness,
) -> Result<(), Error> {
    let emulator = harness.aux().emulator().clone();
    let address = default_address();
    let peer = add_fake_peer(&emulator, &address).await?;
    let (_handle, handle_remote) = create_endpoints::<AdvertisingHandleMarker>()?;

    let mut params = default_parameters();
    params.connectable = Some(true);

    let result = start_advertising(&harness, params, handle_remote).await?;
    expect_ok(result, "failed to start advertising")?;

    peer.emulate_le_connection_complete(ConnectionRole::Follower)?;
    fasync::spawn(
        emulator::watch_peer_connection_states(harness.clone(), address, peer.clone())
            .unwrap_or_else(|_| ()),
    );

    let _ = harness
        .when_satisfied(expectation::peripheral_received_connection(), test_timeout())
        .await?;

    expect_true!(harness.read().connections.len() == 1)?;
    let (_, conn) = harness.write_state().connections.remove(0);

    // Tell the controller to disconnect the link. The harness should get notified of this.
    peer.emulate_disconnection_complete()?;
    let _ = harness
        .when_satisfied(
            emulator::expectation::peer_connection_state_was(address, ConnectionState::Connected)
                .and(emulator::expectation::peer_connection_state_is(
                    address,
                    ConnectionState::Disconnected,
                )),
            test_timeout(),
        )
        .await?;

    // Our connection handle should be closed by the system. Polling for events should
    // result in an error. Keep a local copy to ensure that the handle doesn't get dropped
    // locally.
    watch_handle_closed(conn.clone().take_event_stream()).await
}

/// Run all test cases.
pub fn run_all() -> Result<(), Error> {
    run_suite!(
        "le.Peripheral",
        [
            test_enable_advertising,
            test_enable_and_disable_advertising,
            test_advertising_handle_closed_while_pending,
            test_advertising_data_too_long,
            test_scan_response_data_too_long,
            test_update_advertising,
            test_advertising_types,
            test_advertising_modes,
            test_advertising_data,
            test_scan_response,
            test_receive_connection,
            test_connection_dropped_when_not_connectable,
            test_drop_connection,
            test_connection_handle_closes_on_disconnect
        ]
    )
}

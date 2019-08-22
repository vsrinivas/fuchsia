// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{err_msg, Error, Fail},
    fidl::endpoints::{create_endpoints, ServerEnd},
    fidl_fuchsia_bluetooth::Uuid,
    fidl_fuchsia_bluetooth_le::{
        AdvertisingData, AdvertisingHandleMarker, AdvertisingModeHint, AdvertisingParameters,
        PeripheralStartAdvertisingResult as AdvertisingResult,
    },
    fidl_fuchsia_bluetooth_test::LegacyAdvertisingType,
    fuchsia_async::{DurationExt, TimeoutExt},
    fuchsia_bluetooth::{
        expectation::asynchronous::ExpectableStateExt, types::emulator::LegacyAdvertisingState,
    },
    fuchsia_zircon::{Duration, DurationNum},
    futures::TryFutureExt,
    std::mem::drop,
};

use crate::harness::{expect::expect_ok, low_energy_peripheral::PeripheralHarness};

mod expectation {
    use crate::harness::low_energy_peripheral::PeripheralState;
    use fidl_fuchsia_bluetooth_test::LegacyAdvertisingType;
    use fuchsia_bluetooth::{expectation::Predicate, types::emulator::LegacyAdvertisingState};

    fn state_partial_match(
        state: &LegacyAdvertisingState,
        expected: &LegacyAdvertisingState,
    ) -> bool {
        if state.enabled != expected.enabled {
            return false;
        }
        if expected.type_.is_some() && state.type_ != expected.type_ {
            return false;
        }
        if expected.address_type.is_some() && state.address_type != expected.address_type {
            return false;
        }
        if expected.interval_min.is_some() && state.interval_min != expected.interval_min {
            return false;
        }
        if expected.interval_max.is_some() && state.interval_max != expected.interval_max {
            return false;
        }
        if expected.advertising_data.is_some()
            && state.advertising_data != expected.advertising_data
        {
            return false;
        }
        if expected.scan_response.is_some() && state.scan_response != expected.scan_response {
            return false;
        }
        true
    }

    fn to_slices(ms: u16) -> u16 {
        let slices = (ms as u32) * 1000 / 625;
        slices as u16
    }

    // Performs a partial match over all valid (not None) fields of `expected` for the latest state.
    pub fn emulator_advertising_state_is(
        expected: LegacyAdvertisingState,
    ) -> Predicate<PeripheralState> {
        let descr = format!("latest advertising state matches {:#?}", expected);
        Predicate::new(
            move |state: &PeripheralState| -> bool {
                match state.advertising_state_changes.last() {
                    Some(s) => state_partial_match(s, &expected),
                    None => false,
                }
            },
            Some(&descr),
        )
    }

    // Performs a partial match over all valid (not None) fields of `expected` for the latest state.
    pub fn emulator_advertising_state_was(
        expected: LegacyAdvertisingState,
    ) -> Predicate<PeripheralState> {
        let descr = format!("a past advertising state matched {:#?}", expected);
        Predicate::new(
            move |state: &PeripheralState| -> bool {
                state.advertising_state_changes.iter().any(|s| state_partial_match(s, &expected))
            },
            Some(&descr),
        )
    }

    pub fn emulator_advertising_is_enabled() -> Predicate<PeripheralState> {
        emulator_advertising_state_is(LegacyAdvertisingState {
            enabled: true,
            ..LegacyAdvertisingState::default()
        })
    }

    pub fn emulator_advertising_is_disabled() -> Predicate<PeripheralState> {
        emulator_advertising_state_is(LegacyAdvertisingState {
            enabled: false,
            ..LegacyAdvertisingState::default()
        })
    }

    pub fn emulator_advertising_was_enabled() -> Predicate<PeripheralState> {
        emulator_advertising_state_was(LegacyAdvertisingState {
            enabled: true,
            ..LegacyAdvertisingState::default()
        })
    }

    pub fn emulator_advertising_was_disabled() -> Predicate<PeripheralState> {
        emulator_advertising_state_was(LegacyAdvertisingState {
            enabled: false,
            ..LegacyAdvertisingState::default()
        })
    }

    pub fn emulator_advertising_type_is(
        expected: LegacyAdvertisingType,
    ) -> Predicate<PeripheralState> {
        emulator_advertising_state_is(LegacyAdvertisingState {
            enabled: true,
            type_: Some(expected),
            ..LegacyAdvertisingState::default()
        })
    }

    pub fn emulator_advertising_interval_no_more_than(
        expected_ms: u16,
    ) -> Predicate<PeripheralState> {
        emulator_advertising_state_is(LegacyAdvertisingState {
            enabled: true,
            interval_max: Some(to_slices(expected_ms)),
            ..LegacyAdvertisingState::default()
        })
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
    harness
        .aux()
        .proxy()
        .start_advertising(params, handle)
        .map_err(|e| e.context("FIDL error sending command").into())
        .on_timeout(test_timeout().after_now(), move || Err(err_msg("timed out")))
        .await
        .map_err(|e| e.context("Could not start advertising").into())
}

fn default_parameters() -> AdvertisingParameters {
    AdvertisingParameters { data: None, scan_response: None, mode_hint: None, connectable: None }
}

pub async fn test_enable_advertising(harness: PeripheralHarness) -> Result<(), Error> {
    let (_handle, handle_remote) = create_endpoints::<AdvertisingHandleMarker>()?;
    let result = start_advertising(&harness, default_parameters(), handle_remote).await?;
    expect_ok(result, "failed to start advertising")?;

    let _ = harness
        .when_satisfied(expectation::emulator_advertising_is_enabled(), test_timeout())
        .await?;
    Ok(())
}

pub async fn test_enable_and_disable_advertising(harness: PeripheralHarness) -> Result<(), Error> {
    let (handle, handle_remote) = create_endpoints::<AdvertisingHandleMarker>()?;
    let result = start_advertising(&harness, default_parameters(), handle_remote).await?;
    expect_ok(result, "failed to start advertising")?;
    let _ = harness
        .when_satisfied(expectation::emulator_advertising_is_enabled(), test_timeout())
        .await?;

    // Closing the advertising handle should stop advertising.
    drop(handle);
    let _ = harness
        .when_satisfied(expectation::emulator_advertising_is_disabled(), test_timeout())
        .await?;
    Ok(())
}

pub async fn test_advertising_handle_closed_while_pending(
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
            expectation::emulator_advertising_was_enabled()
                .and(expectation::emulator_advertising_is_disabled()),
            test_timeout(),
        )
        .await?;
    Ok(())
}

pub async fn test_update_advertising(harness: PeripheralHarness) -> Result<(), Error> {
    let (_handle, handle_remote) = create_endpoints::<AdvertisingHandleMarker>()?;
    let result = start_advertising(&harness, default_parameters(), handle_remote).await?;
    expect_ok(result, "failed to start advertising")?;
    let _ = harness
        .when_satisfied(expectation::emulator_advertising_is_enabled(), test_timeout())
        .await?;
    let _ = harness
        .when_satisfied(
            expectation::emulator_advertising_type_is(LegacyAdvertisingType::AdvNonconnInd),
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
            expectation::emulator_advertising_was_disabled()
                .and(expectation::emulator_advertising_is_enabled()),
            test_timeout(),
        )
        .await?;
    let _ = harness
        .when_satisfied(
            expectation::emulator_advertising_type_is(LegacyAdvertisingType::AdvInd),
            test_timeout(),
        )
        .await?;

    Ok(())
}

pub async fn test_advertising_types(harness: PeripheralHarness) -> Result<(), Error> {
    // Non-connectable
    let params = AdvertisingParameters { connectable: Some(false), ..default_parameters() };
    let (_handle, handle_remote) = create_endpoints::<AdvertisingHandleMarker>()?;
    let result = start_advertising(&harness, params, handle_remote).await?;
    expect_ok(result, "failed to start advertising")?;
    let _ = harness
        .when_satisfied(
            expectation::emulator_advertising_type_is(LegacyAdvertisingType::AdvNonconnInd),
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
            expectation::emulator_advertising_type_is(LegacyAdvertisingType::AdvInd),
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
            expectation::emulator_advertising_type_is(LegacyAdvertisingType::AdvScanInd),
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
            expectation::emulator_advertising_type_is(LegacyAdvertisingType::AdvInd),
            test_timeout(),
        )
        .await?;

    Ok(())
}

pub async fn test_advertising_modes(harness: PeripheralHarness) -> Result<(), Error> {
    // Very fast advertising interval (<= 60 ms)
    let params = AdvertisingParameters {
        mode_hint: Some(AdvertisingModeHint::VeryFast),
        ..default_parameters()
    };
    let (_handle, handle_remote) = create_endpoints::<AdvertisingHandleMarker>()?;
    let result = start_advertising(&harness, params, handle_remote).await?;
    expect_ok(result, "failed to start advertising")?;
    let _ = harness
        .when_satisfied(expectation::emulator_advertising_interval_no_more_than(60), test_timeout())
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
        .when_satisfied(
            expectation::emulator_advertising_interval_no_more_than(150),
            test_timeout(),
        )
        .await?;

    // Slow advertising intterval (<= 1.2 s)
    let params = AdvertisingParameters {
        mode_hint: Some(AdvertisingModeHint::Slow),
        ..default_parameters()
    };
    let (_handle, handle_remote) = create_endpoints::<AdvertisingHandleMarker>()?;
    let result = start_advertising(&harness, params, handle_remote).await?;
    expect_ok(result, "failed to start advertising")?;
    let _ = harness
        .when_satisfied(
            expectation::emulator_advertising_interval_no_more_than(1200),
            test_timeout(),
        )
        .await?;

    Ok(())
}

pub async fn test_advertising_data(harness: PeripheralHarness) -> Result<(), Error> {
    // Test that encoding one field works. The serialization of other fields is unit tested elsewhere.
    let params = AdvertisingParameters {
        data: Some(AdvertisingData { name: Some("hello".to_string()), ..empty_advertising_data() }),
        ..default_parameters()
    };
    let (_handle, handle_remote) = create_endpoints::<AdvertisingHandleMarker>()?;
    let result = start_advertising(&harness, params, handle_remote).await?;
    expect_ok(result, "failed to start advertising")?;

    let expected = LegacyAdvertisingState {
        enabled: true,
        advertising_data: Some(vec![
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
        ]),
        ..LegacyAdvertisingState::default()
    };
    let _ = harness
        .when_satisfied(expectation::emulator_advertising_state_is(expected), test_timeout())
        .await?;

    Ok(())
}

pub async fn test_scan_response(harness: PeripheralHarness) -> Result<(), Error> {
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

    let expected = LegacyAdvertisingState {
        enabled: true,
        advertising_data: Some(vec![
            0x02, 0x01, 0x02, // Flags (General discoverable mode)
            0x03, 0x02, 0x0d, 0x18, // Incomplete list of service UUIDs
        ]),
        scan_response: Some(vec![
            // The local name, as above.
            0x06,
            0x09,
            ('h' as u8),
            ('e' as u8),
            ('l' as u8),
            ('l' as u8),
            ('o' as u8),
        ]),
        ..LegacyAdvertisingState::default()
    };
    let _ = harness
        .when_satisfied(expectation::emulator_advertising_state_is(expected), test_timeout())
        .await?;

    Ok(())
}

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::Error,
    fidl_fuchsia_bluetooth::Status,
    fidl_fuchsia_bluetooth_control::TechnologyType,
    fidl_fuchsia_bluetooth_host::{
        AddressType, BondingData, LeData, Ltk, RemoteKey, SecurityProperties,
    },
    fuchsia_bluetooth::expectation,
    futures::TryFutureExt,
};

use crate::harness::host_driver::{expect_eq, expect_remote_device, HostDriverHarness};

// TODO(armansito|xow): Add tests for BR/EDR and dual mode bond data.

fn new_le_bond_data(id: &str, address: &str, has_ltk: bool) -> BondingData {
    BondingData {
        identifier: id.to_string(),
        local_address: "AA:BB:CC:DD:EE:FF".to_string(),
        name: None,
        le: Some(Box::new(LeData {
            address: address.to_string(),
            address_type: AddressType::LeRandom,
            connection_parameters: None,
            services: vec![],
            ltk: if has_ltk {
                Some(Box::new(Ltk {
                    key: RemoteKey {
                        security_properties: SecurityProperties {
                            authenticated: true,
                            secure_connections: false,
                            encryption_key_size: 16,
                        },
                        value: [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16],
                    },
                    key_size: 16,
                    ediv: 1,
                    rand: 2,
                }))
            } else {
                None
            },
            irk: None,
            csrk: None,
        })),
        bredr: None,
    }
}

async fn add_bonds(
    state: &HostDriverHarness,
    mut bonds: Vec<BondingData>,
) -> Result<(Status), Error> {
    await!(state.host_proxy().add_bonded_devices(&mut bonds.iter_mut()).err_into())
}

const TEST_ID1: &str = "1234";
const TEST_ID2: &str = "2345";
const TEST_ADDR1: &str = "01:02:03:04:05:06";
const TEST_ADDR2: &str = "06:05:04:03:02:01";

// Tests initializing bonded LE devices.
pub async fn test_add_bonded_devices_success(test_state: HostDriverHarness) -> Result<(), Error> {
    // Devices should be initially empty.
    let devices = await!(test_state.host_proxy().list_devices())?;
    expect_eq!(vec![], devices)?;

    let bond_data1 = new_le_bond_data(TEST_ID1, TEST_ADDR1, true /* has LTK */);
    let bond_data2 = new_le_bond_data(TEST_ID2, TEST_ADDR2, true /* has LTK */);
    let status = await!(add_bonds(&test_state, vec![bond_data1, bond_data2]))?;
    expect_true!(status.error.is_none())?;

    // We should receive notifications for the newly added devices.
    let expected1 = expectation::peer::address(TEST_ADDR1)
        .and(expectation::peer::technology(TechnologyType::LowEnergy))
        .and(expectation::peer::bonded(true));

    let expected2 = expectation::peer::address(TEST_ADDR2)
        .and(expectation::peer::technology(TechnologyType::LowEnergy))
        .and(expectation::peer::bonded(true));

    await!(test_state.expect_peer(None, expected1))?;
    await!(test_state.expect_peer(None, expected2))?;

    let devices = await!(test_state.host_proxy().list_devices())?;
    expect_eq!(2, devices.len())?;
    expect_true!(devices.iter().any(|dev| dev.address == TEST_ADDR1))?;
    expect_true!(devices.iter().any(|dev| dev.address == TEST_ADDR2))?;

    Ok(())
}

pub async fn test_add_bonded_devices_no_ltk_fails(
    test_state: HostDriverHarness,
) -> Result<(), Error> {
    // Devices should be initially empty.
    let devices = await!(test_state.host_proxy().list_devices())?;
    expect_eq!(vec![], devices)?;

    // Inserting a bonded device without a LTK should fail.
    let bond_data = new_le_bond_data(TEST_ID1, TEST_ADDR1, false /* no LTK */);
    let status = await!(add_bonds(&test_state, vec![bond_data]))?;
    expect_true!(status.error.is_some())?;

    let devices = await!(test_state.host_proxy().list_devices())?;
    expect_eq!(vec![], devices)?;

    Ok(())
}

pub async fn test_add_bonded_devices_duplicate_entry(
    test_state: HostDriverHarness,
) -> Result<(), Error> {
    // Devices should be initially empty.
    let devices = await!(test_state.host_proxy().list_devices())?;
    expect_eq!(vec![], devices)?;

    // Initialize one entry.
    let bond_data = new_le_bond_data(TEST_ID1, TEST_ADDR1, true /* with LTK */);
    let status = await!(add_bonds(&test_state, vec![bond_data]))?;
    expect_true!(status.error.is_none())?;

    // We should receive a notification for the newly added device.
    let expected = expectation::peer::address(TEST_ADDR1)
        .and(expectation::peer::technology(TechnologyType::LowEnergy))
        .and(expectation::peer::bonded(true));

    await!(test_state.expect_peer(None, expected.clone()))?;
    let devices = await!(test_state.host_proxy().list_devices())?;
    expect_eq!(1, devices.len())?;

    // Adding an entry with the existing id should fail.
    let bond_data = new_le_bond_data(TEST_ID1, TEST_ADDR2, true /* with LTK */);
    let status = await!(add_bonds(&test_state, vec![bond_data]))?;
    expect_true!(status.error.is_some())?;

    // Adding an entry with a different ID but existing address should fail.
    let bond_data = new_le_bond_data(TEST_ID2, TEST_ADDR1, true /* with LTK */);
    let status = await!(add_bonds(&test_state, vec![bond_data]))?;
    expect_true!(status.error.is_some())?;

    Ok(())
}

// Tests that adding a list of bonding data with malformed content succeeds for the valid entries
// but reports an error.
pub async fn test_add_bonded_devices_invalid_entry(
    test_state: HostDriverHarness,
) -> Result<(), Error> {
    // Devices should be initially empty.
    let devices = await!(test_state.host_proxy().list_devices())?;
    expect_eq!(vec![], devices)?;

    // Add one entry with no LTK (invalid) and one with (valid). This should create an entry for the
    // valid device but report an error for the invalid entry.
    let no_ltk = new_le_bond_data(TEST_ID1, TEST_ADDR1, false);
    let with_ltk = new_le_bond_data(TEST_ID2, TEST_ADDR2, true);
    let status = await!(add_bonds(&test_state, vec![no_ltk, with_ltk]))?;
    expect_true!(status.error.is_some())?;

    let expected = expectation::peer::address(TEST_ADDR2)
        .and(expectation::peer::technology(TechnologyType::LowEnergy))
        .and(expectation::peer::bonded(true));

    await!(test_state.expect_peer(None, expected.clone()))?;
    let devices = await!(test_state.host_proxy().list_devices())?;
    expect_eq!(1, devices.len())?;
    expect_remote_device(&test_state, TEST_ADDR2, &expected)?;

    Ok(())
}

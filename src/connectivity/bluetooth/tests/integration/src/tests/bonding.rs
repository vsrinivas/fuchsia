// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{err_msg, Error},
    fidl_fuchsia_bluetooth::Status,
    fidl_fuchsia_bluetooth_control::{
        AddressType, BondingData, LeData, Ltk, RemoteKey, SecurityProperties, TechnologyType,
    },
    fuchsia_bluetooth::expectation,
    futures::TryFutureExt,
};

use crate::harness::{
    expect::expect_eq,
    host_driver::{expect_host_peer, expect_remote_device, HostDriverHarness},
};

// TODO(armansito|xow): Add tests for BR/EDR and dual mode bond data.

fn new_le_bond_data(id: &str, address: &str, name: &str, has_ltk: bool) -> BondingData {
    BondingData {
        identifier: id.to_string(),
        local_address: "AA:BB:CC:DD:EE:FF".to_string(),
        name: Some(name.to_string()),
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
) -> Result<Status, Error> {
    let fut = state.aux().proxy().add_bonded_devices(&mut bonds.iter_mut()).err_into();
    fut.await
}

const TEST_ID1: &str = "1234";
const TEST_ID2: &str = "2345";
const TEST_ADDR1: &str = "01:02:03:04:05:06";
const TEST_ADDR2: &str = "06:05:04:03:02:01";
const TEST_NAME1: &str = "Name1";
const TEST_NAME2: &str = "Name2";

// Tests initializing bonded LE devices.
async fn test_add_bonded_devices_success(test_state: HostDriverHarness) -> Result<(), Error> {
    // Devices should be initially empty.
    let fut = test_state.aux().proxy().list_devices();
    let devices = fut.await?;
    expect_eq!(vec![], devices)?;

    let bond_data1 = new_le_bond_data(TEST_ID1, TEST_ADDR1, TEST_NAME1, true /* has LTK */);
    let bond_data2 = new_le_bond_data(TEST_ID2, TEST_ADDR2, TEST_NAME2, true /* has LTK */);
    let status = add_bonds(&test_state, vec![bond_data1, bond_data2]).await?;
    expect_true!(status.error.is_none())?;

    // We should receive notifications for the newly added devices.
    let expected1 = expectation::peer::address(TEST_ADDR1)
        .and(expectation::peer::technology(TechnologyType::LowEnergy))
        .and(expectation::peer::bonded(true));

    let expected2 = expectation::peer::address(TEST_ADDR2)
        .and(expectation::peer::technology(TechnologyType::LowEnergy))
        .and(expectation::peer::bonded(true));

    expect_host_peer(&test_state, expected1).await?;
    expect_host_peer(&test_state, expected2).await?;

    let fut = test_state.aux().proxy().list_devices();
    let devices = fut.await?;
    expect_eq!(2, devices.len())?;
    expect_true!(devices.iter().any(|dev| dev.address == TEST_ADDR1))?;
    expect_true!(devices.iter().any(|dev| dev.address == TEST_ADDR2))?;

    expect_true!(devices.iter().any(|dev| dev.name == Some(TEST_NAME1.to_string())))?;
    expect_true!(devices.iter().any(|dev| dev.name == Some(TEST_NAME2.to_string())))?;

    Ok(())
}

async fn test_add_bonded_devices_no_ltk_fails(test_state: HostDriverHarness) -> Result<(), Error> {
    // Devices should be initially empty.
    let fut = test_state.aux().proxy().list_devices();
    let devices = fut.await?;
    expect_eq!(vec![], devices)?;

    // Inserting a bonded device without a LTK should fail.
    let bond_data = new_le_bond_data(TEST_ID1, TEST_ADDR1, TEST_NAME1, false /* no LTK */);
    let status = add_bonds(&test_state, vec![bond_data]).await?;
    expect_true!(status.error.is_some())?;

    let fut = test_state.aux().proxy().list_devices();
    let devices = fut.await?;
    expect_eq!(vec![], devices)?;

    Ok(())
}

async fn test_add_bonded_devices_duplicate_entry(
    test_state: HostDriverHarness,
) -> Result<(), Error> {
    // Devices should be initially empty.
    let fut = test_state.aux().proxy().list_devices();
    let devices = fut.await?;
    expect_eq!(vec![], devices)?;

    // Initialize one entry.
    let bond_data = new_le_bond_data(TEST_ID1, TEST_ADDR1, TEST_NAME1, true /* with LTK */);
    let status = add_bonds(&test_state, vec![bond_data]).await?;
    expect_true!(status.error.is_none())?;

    // We should receive a notification for the newly added device.
    let expected = expectation::peer::address(TEST_ADDR1)
        .and(expectation::peer::technology(TechnologyType::LowEnergy))
        .and(expectation::peer::bonded(true));

    expect_host_peer(&test_state, expected.clone()).await?;
    let fut = test_state.aux().proxy().list_devices();
    let devices = fut.await?;
    expect_eq!(1, devices.len())?;

    // Adding an entry with the existing id should fail.
    let bond_data = new_le_bond_data(TEST_ID1, TEST_ADDR2, TEST_NAME2, true /* with LTK */);
    let status = add_bonds(&test_state, vec![bond_data]).await?;
    expect_true!(status.error.is_some())?;

    // Adding an entry with a different ID but existing address should fail.
    let bond_data = new_le_bond_data(TEST_ID2, TEST_ADDR1, TEST_NAME1, true /* with LTK */);
    let status = add_bonds(&test_state, vec![bond_data]).await?;
    expect_true!(status.error.is_some())?;

    Ok(())
}

// Tests that adding a list of bonding data with malformed content succeeds for the valid entries
// but reports an error.
async fn test_add_bonded_devices_invalid_entry(test_state: HostDriverHarness) -> Result<(), Error> {
    // Devices should be initially empty.
    let fut = test_state.aux().proxy().list_devices();
    let devices = fut.await?;
    expect_eq!(vec![], devices)?;

    // Add one entry with no LTK (invalid) and one with (valid). This should create an entry for the
    // valid device but report an error for the invalid entry.
    let no_ltk = new_le_bond_data(TEST_ID1, TEST_ADDR1, TEST_NAME1, false);
    let with_ltk = new_le_bond_data(TEST_ID2, TEST_ADDR2, TEST_NAME2, true);
    let status = add_bonds(&test_state, vec![no_ltk, with_ltk]).await?;
    expect_true!(status.error.is_some())?;

    let expected = expectation::peer::address(TEST_ADDR2)
        .and(expectation::peer::technology(TechnologyType::LowEnergy))
        .and(expectation::peer::bonded(true));

    expect_host_peer(&test_state, expected.clone()).await?;
    let fut = test_state.aux().proxy().list_devices();
    let devices = fut.await?;
    expect_eq!(1, devices.len())?;
    expect_remote_device(&test_state, TEST_ADDR2, &expected)?;

    Ok(())
}

/// Run all test cases.
pub fn run_all() -> Result<(), Error> {
    run_suite!(
        "bt-host driver bonding",
        [
            test_add_bonded_devices_success,
            test_add_bonded_devices_no_ltk_fails,
            test_add_bonded_devices_duplicate_entry,
            test_add_bonded_devices_invalid_entry
        ]
    )
}

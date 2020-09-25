// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl_fuchsia_bluetooth_sys as sys,
    fuchsia_bluetooth::{
        expectation,
        types::{Address, BondingData, LeData, OneOrBoth, PeerId},
    },
};

use crate::harness::{
    emulator::EmulatorHarness,
    expect::expect_eq,
    host_driver::{expect_peer, HostDriverHarness},
};

// TODO(armansito|xow): Add tests for BR/EDR and dual mode bond data.

fn new_le_bond_data(id: &PeerId, address: &Address, name: &str, has_ltk: bool) -> BondingData {
    BondingData {
        identifier: (*id).into(),
        local_address: Address::Public([0xFF, 0xEE, 0xDD, 0xCC, 0xBB, 0xAA]),
        address: (*address).clone(),
        name: Some(name.to_string()),
        data: OneOrBoth::Left(LeData {
            connection_parameters: None,
            services: vec![],
            local_ltk: None,
            peer_ltk: if has_ltk {
                Some(sys::Ltk {
                    key: sys::PeerKey {
                        security: sys::SecurityProperties {
                            authenticated: true,
                            secure_connections: false,
                            encryption_key_size: 16,
                        },
                        data: sys::Key {
                            value: [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16],
                        },
                    },
                    ediv: 1,
                    rand: 2,
                })
            } else {
                None
            },
            irk: None,
            csrk: None,
        }),
    }
}

async fn restore_bonds(
    state: &HostDriverHarness,
    bonds: Vec<BondingData>,
) -> Result<Vec<BondingData>, Error> {
    use std::convert::TryFrom;

    let fut = state.aux().proxy().restore_bonds(&mut bonds.into_iter().map(sys::BondingData::from));
    let errors = fut.await?;
    Ok(errors.into_iter().map(BondingData::try_from).collect::<Result<Vec<_>, _>>()?)
}

const TEST_ID1: PeerId = PeerId(0x1234);
const TEST_ID2: PeerId = PeerId(0x5678);
const TEST_ADDR1: Address = Address::Public([6, 5, 4, 3, 2, 1]);
const TEST_ADDR2: Address = Address::Public([1, 2, 3, 4, 5, 6]);
const TEST_NAME1: &str = "Name1";
const TEST_NAME2: &str = "Name2";

async fn test_restore_no_bonds_succeeds(harness: HostDriverHarness) -> Result<(), Error> {
    let errors = restore_bonds(&harness, vec![]).await?;
    expect_eq!(vec![], errors)
}

// Tests initializing bonded LE devices.
async fn test_restore_bonded_devices_success(harness: HostDriverHarness) -> Result<(), Error> {
    // Peers should be initially empty.
    expect_eq!(0, harness.state().peers().len())?;

    let bond_data1 = new_le_bond_data(&TEST_ID1, &TEST_ADDR1, TEST_NAME1, true /* has LTK */);
    let bond_data2 = new_le_bond_data(&TEST_ID2, &TEST_ADDR2, TEST_NAME2, true /* has LTK */);
    let errors = restore_bonds(&harness, vec![bond_data1, bond_data2]).await?;
    expect_eq!(vec![], errors)?;

    // We should receive notifications for the newly added devices.
    let expected1 = expectation::peer::address(TEST_ADDR1)
        .and(expectation::peer::technology(fidl_fuchsia_bluetooth_sys::TechnologyType::LowEnergy))
        .and(expectation::peer::name(TEST_NAME1))
        .and(expectation::peer::bonded(true));

    let expected2 = expectation::peer::address(TEST_ADDR2)
        .and(expectation::peer::technology(fidl_fuchsia_bluetooth_sys::TechnologyType::LowEnergy))
        .and(expectation::peer::name(TEST_NAME2))
        .and(expectation::peer::bonded(true));

    expect_peer(&harness, expected1).await?;
    expect_peer(&harness, expected2).await?;

    Ok(())
}

async fn test_restore_bonded_devices_no_ltk_fails(harness: HostDriverHarness) -> Result<(), Error> {
    // Peers should be initially empty.
    expect_eq!(0, harness.state().peers().len())?;

    // Inserting a bonded device without a LTK should fail.
    let bond_data = new_le_bond_data(&TEST_ID1, &TEST_ADDR1, TEST_NAME1, false /* no LTK */);
    let errors = restore_bonds(&harness, vec![bond_data.clone()]).await?;
    expect_eq!(vec![bond_data], errors)?;
    expect_eq!(0, harness.state().peers().len())?;

    Ok(())
}

async fn test_restore_bonded_devices_duplicate_entry(
    harness: HostDriverHarness,
) -> Result<(), Error> {
    // Peers should be initially empty.
    expect_eq!(0, harness.state().peers().len())?;

    // Initialize one entry.
    let bond_data = new_le_bond_data(&TEST_ID1, &TEST_ADDR1, TEST_NAME1, true /* with LTK */);
    let errors = restore_bonds(&harness, vec![bond_data]).await?;
    expect_eq!(vec![], errors)?;

    // We should receive a notification for the newly added device.
    let expected = expectation::peer::address(TEST_ADDR1)
        .and(expectation::peer::technology(fidl_fuchsia_bluetooth_sys::TechnologyType::LowEnergy))
        .and(expectation::peer::bonded(true));
    expect_peer(&harness, expected.clone()).await?;

    // Adding an entry with the existing id should fail.
    let bond_data = new_le_bond_data(&TEST_ID1, &TEST_ADDR2, TEST_NAME2, true /* with LTK */);
    let errors = restore_bonds(&harness, vec![bond_data.clone()]).await?;
    expect_eq!(vec![bond_data], errors)?;

    // Adding an entry with a different ID but existing address should fail.
    let bond_data = new_le_bond_data(&TEST_ID2, &TEST_ADDR1, TEST_NAME1, true /* with LTK */);
    let errors = restore_bonds(&harness, vec![bond_data.clone()]).await?;
    expect_eq!(vec![bond_data], errors)?;

    Ok(())
}

// Tests that adding a list of bonding data with malformed content succeeds for the valid entries
// but reports an error.
async fn test_restore_bonded_devices_invalid_entry(
    harness: HostDriverHarness,
) -> Result<(), Error> {
    // Peers should be initially empty.
    expect_eq!(0, harness.state().peers().len())?;

    // Add one entry with no LTK (invalid) and one with (valid). This should create an entry for the
    // valid device but report an error for the invalid entry.
    let no_ltk = new_le_bond_data(&TEST_ID1, &TEST_ADDR1, TEST_NAME1, false);
    let with_ltk = new_le_bond_data(&TEST_ID2, &TEST_ADDR2, TEST_NAME2, true);
    let errors = restore_bonds(&harness, vec![no_ltk.clone(), with_ltk]).await?;
    expect_eq!(vec![no_ltk], errors)?;

    let expected = expectation::peer::address(TEST_ADDR2)
        .and(expectation::peer::technology(fidl_fuchsia_bluetooth_sys::TechnologyType::LowEnergy))
        .and(expectation::peer::bonded(true));
    expect_peer(&harness, expected.clone()).await?;

    Ok(())
}

/// Run all test cases.
pub fn run_all() -> Result<(), Error> {
    run_suite!(
        "bt-host driver bonding",
        [
            test_restore_no_bonds_succeeds,
            test_restore_bonded_devices_success,
            test_restore_bonded_devices_no_ltk_fails,
            test_restore_bonded_devices_duplicate_entry,
            test_restore_bonded_devices_invalid_entry
        ]
    )
}

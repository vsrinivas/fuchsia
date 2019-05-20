// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro)]

use {failure::Error, std::io::Write};

use crate::{
    harness::run_test,
    tests::{
        bonding::{
            test_add_bonded_devices_duplicate_entry, test_add_bonded_devices_invalid_entry,
            test_add_bonded_devices_no_ltk_fails, test_add_bonded_devices_success,
        },
        control::set_active_host,
        host_driver::{
            test_bd_addr, test_close, test_connect, test_discoverable, test_discovery,
            test_list_devices, test_set_local_name,
        },
        lifecycle::lifecycle_test,
        low_energy_central::{enable_and_disable_scan, enable_scan},
        profile::{
            add_fake_profile, add_remove_profile, connect_unknown_peer, same_psm_twice_fails,
        },
    },
};

#[macro_use]
mod harness;
#[macro_use]
mod tests;

/// Collect a Vector of Results into a Result of a Vector. If all results are
/// `Ok`, then return `Ok` of the results. Otherwise return the first `Err`.
fn collect_results<T, E>(results: Vec<Result<T, E>>) -> Result<Vec<T>, E> {
    results.into_iter().collect()
}

fn main() -> Result<(), Error> {
    println!("TEST BEGIN");

    // TODO(BT-805): Add test cases for LE privacy
    // TODO(BT-806): Add test cases for LE auto-connect/background-scan
    // TODO(BT-20): Add test cases for GATT client role
    // TODO(BT-20): Add test cases for GATT server role
    // TODO(BT-20): Add test cases for BR/EDR and dual-mode connections
    // TODO(BT-20): Add test cases for BR/EDR Profiles

    collect_results(vec![
        run_test!(lifecycle_test),
        // Host Driver tests
        run_test!(test_bd_addr),
        run_test!(test_set_local_name),
        run_test!(test_discoverable),
        run_test!(test_discovery),
        run_test!(test_close),
        run_test!(test_list_devices),
        run_test!(test_connect),
        // Bonding tests
        run_test!(test_add_bonded_devices_success),
        run_test!(test_add_bonded_devices_no_ltk_fails),
        run_test!(test_add_bonded_devices_duplicate_entry),
        run_test!(test_add_bonded_devices_invalid_entry),
        // Control tests
        run_test!(set_active_host),
        // le.Central tests
        run_test!(enable_scan),
        run_test!(enable_and_disable_scan),
        // Profile tests
        run_test!(add_fake_profile),
        run_test!(same_psm_twice_fails),
        run_test!(add_remove_profile),
        run_test!(connect_unknown_peer),
    ])?;
    Ok(())
}

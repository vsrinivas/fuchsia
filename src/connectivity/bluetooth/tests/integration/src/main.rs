// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro, futures_api)]

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

    collect_results(vec![
        run_test!(lifecycle_test),
        run_test!(test_bd_addr),
        run_test!(test_set_local_name),
        run_test!(test_discoverable),
        run_test!(test_discovery),
        run_test!(test_close),
        run_test!(test_list_devices),
        run_test!(test_connect),
        run_test!(test_add_bonded_devices_success),
        run_test!(test_add_bonded_devices_no_ltk_fails),
        run_test!(test_add_bonded_devices_duplicate_entry),
        run_test!(test_add_bonded_devices_invalid_entry),
        run_test!(set_active_host),
        run_test!(enable_scan),
        run_test!(enable_and_disable_scan),
    ])?;
    Ok(())
}

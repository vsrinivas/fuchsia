// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {anyhow::Error, fuchsia_bluetooth::util::CollectExt};

#[macro_use]
mod harness;
#[macro_use]
mod tests;

fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&[]).expect("Initializing syslog should not fail");

    // TODO(fxbug.dev/1398): Add test cases for LE privacy
    // TODO(fxbug.dev/1399): Add test cases for LE auto-connect/background-scan
    // TODO(fxbug.dev/613): Add test cases for GATT client role
    // TODO(fxbug.dev/613): Add test cases for GATT server role
    // TODO(fxbug.dev/613): Add test cases for BR/EDR and dual-mode connections
    vec![
        // Tests that require bt-gap.cmx to not be running. Run these tests first as bt-gap
        // interferes with their operation.
        tests::host_driver::run_all(),
        tests::bonding::run_all(),
        // Tests that trigger bt-gap.cmx.
        tests::inspect::run_all(),
        tests::bootstrap::run_all(),
        tests::access::run_all(),
        tests::control::run_all(),
        tests::profile::run_all(),
        tests::low_energy_central::run_all(),
        tests::low_energy_peripheral::run_all(),
    ]
    .into_iter()
    .collect_results()?;
    Ok(())
}

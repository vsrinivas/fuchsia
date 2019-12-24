// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context as _, Error},
    fidl_fuchsia_bluetooth_sys as sys,
    fuchsia_bluetooth::expectation::{asynchronous::ExpectableStateExt, Predicate as P},
    fuchsia_bluetooth::types::{Address, BondingData, Identity, LeData, OneOrBoth, PeerId},
    fuchsia_zircon::{Duration, DurationNum},
    std::collections::HashSet,
};

use crate::harness::{
    bootstrap::BootstrapHarness,
    control::{ControlHarness, ControlState},
};

const TIMEOUT_SECONDS: i64 = 60;

fn bootstrap_timeout() -> Duration {
    TIMEOUT_SECONDS.seconds()
}

/// An example identity for an Hci Emulator backed host
fn example_emulator_identity() -> Identity {
    // Hci Emulators currently default to address 0
    let emulator_address = Address::Public([0, 0, 0, 0, 0, 0]);

    let host_data = sys::HostData {
        irk: Some(sys::Key { value: [16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1] }),
    };
    let csrk = sys::PeerKey {
        data: sys::Key { value: [0; 16] },
        security: sys::SecurityProperties {
            authenticated: false,
            secure_connections: true,
            encryption_key_size: 0,
        },
    };
    let le_data = LeData {
        address: emulator_address,
        connection_parameters: None,
        services: vec![],
        ltk: None,
        irk: None,
        csrk: Some(csrk),
    };
    let bonds = vec![BondingData {
        identifier: PeerId(42),
        local_address: emulator_address,
        name: None,
        data: OneOrBoth::Left(le_data),
    }];
    Identity { host: host_data, bonds }
}

async fn test_add_and_commit_identities(
    harnesses: (BootstrapHarness, ControlHarness),
) -> Result<(), Error> {
    let (bootstrap, control) = harnesses;
    let bootstrap = bootstrap.aux().clone();

    let control_proxy = control.aux().clone();
    // We avoid using an assert here because the unwinding causes the ActivatedFakeHost to
    // panic during the unwindw which then just aborts. This provides a much clearer and more
    // graceful failure case
    let initial_devices = control_proxy.get_known_remote_devices().await.ok();
    if initial_devices != Some(vec![]) {
        return Err(format_err!(
            "Failed: Initial devices not empty! Expected empty, found: {:?}",
            initial_devices
        ));
    }

    let identity = example_emulator_identity();

    let expected_devices: HashSet<String> =
        identity.bonds.iter().map(|b| b.identifier.to_string()).collect();

    let identities: Vec<sys::Identity> = vec![identity.into()];
    bootstrap.add_identities(&mut identities.into_iter()).context("Error adding identities")?;
    bootstrap.commit().await?.map_err(|e| format_err!("Error committing bonds: {:?}", e))?;

    let pred = P::<ControlState>::new(
        move |control| expected_devices == control.peers.keys().cloned().collect(),
        Some("known device identifiers == expected device identifiers"),
    );
    control.when_satisfied(pred, bootstrap_timeout()).await?;

    Ok(())
}

pub fn run_all() -> Result<(), Error> {
    run_suite!("sys.Bootstrap", [test_add_and_commit_identities])
}

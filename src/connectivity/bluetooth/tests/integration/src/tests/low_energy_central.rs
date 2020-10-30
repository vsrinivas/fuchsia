// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    bt_test_harness::{
        emulator::{add_le_peer, default_le_peer},
        low_energy_central::CentralHarness,
    },
    fuchsia_async::{DurationExt, TimeoutExt},
    fuchsia_bluetooth::{
        error::Error as BTError,
        expectation::asynchronous::{ExpectableExt, ExpectableStateExt},
        types::Address,
    },
    futures::TryFutureExt,
    test_harness::run_suite,
};

use crate::tests::timeout_duration;

mod expect {
    use bt_test_harness::low_energy_central::{CentralState, ScanStateChange};
    use fuchsia_bluetooth::expectation::Predicate;
    use fuchsia_bluetooth::types::le::RemoteDevice;

    pub fn scan_enabled() -> Predicate<CentralState> {
        Predicate::equal(Some(ScanStateChange::ScanEnabled)).over_value(
            |state: &CentralState| state.scan_state_changes.last().cloned(),
            ".scan_state_changes.last()",
        )
    }
    pub fn scan_disabled() -> Predicate<CentralState> {
        Predicate::equal(Some(ScanStateChange::ScanDisabled)).over_value(
            |state: &CentralState| state.scan_state_changes.last().cloned(),
            ".scan_state_changes.last()",
        )
    }
    pub fn device_found(expected_name: &str) -> Predicate<CentralState> {
        let expected_name = expected_name.to_string();
        let has_expected_name = Predicate::equal(Some(expected_name)).over_value(
            |peer: &RemoteDevice| {
                peer.advertising_data.as_ref().and_then(|ad| ad.name.as_ref().cloned())
            },
            ".advertising_data.name",
        );

        Predicate::any(has_expected_name)
            .over(|state: &CentralState| &state.remote_devices, ".remote_devices")
    }
}

async fn start_scan(central: &CentralHarness) -> Result<(), Error> {
    let fut = central
        .aux()
        .central
        .start_scan(None)
        .map_err(|e| e.into())
        .on_timeout(timeout_duration().after_now(), move || Err(format_err!("Timed out")));
    let status = fut.await?;
    if let Some(e) = status.error {
        return Err(BTError::from(*e).into());
    }
    Ok(())
}

async fn test_enable_scan(central: CentralHarness) -> Result<(), Error> {
    let address = Address::Random([1, 0, 0, 0, 0, 0]);
    let fut = add_le_peer(central.aux().as_ref(), default_le_peer(&address));
    let _peer = fut.await?;
    start_scan(&central).await?;
    let _ = central
        .when_satisfied(
            expect::scan_enabled().and(expect::device_found("Fake")),
            timeout_duration(),
        )
        .await?;
    Ok(())
}

async fn test_enable_and_disable_scan(central: CentralHarness) -> Result<(), Error> {
    start_scan(&central).await?;
    let _ = central.when_satisfied(expect::scan_enabled(), timeout_duration()).await?;
    let _ = central.aux().central.stop_scan()?;
    let _ = central.when_satisfied(expect::scan_disabled(), timeout_duration()).await?;
    Ok(())
}

/// Run all test cases.
pub fn run_all() -> Result<(), Error> {
    run_suite!("le.Central", [test_enable_scan, test_enable_and_disable_scan])
}

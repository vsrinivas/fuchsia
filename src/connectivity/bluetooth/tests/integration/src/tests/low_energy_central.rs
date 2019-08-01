// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{err_msg, Error, Fail, ResultExt},
    fuchsia_async::{DurationExt, TimeoutExt},
    fuchsia_bluetooth::{error::Error as BTError, expectation::asynchronous::ExpectableStateExt},
    fuchsia_zircon::{Duration, DurationNum},
    futures::TryFutureExt,
};

use crate::harness::low_energy_central::CentralHarness;

mod central_expectation {
    use crate::harness::low_energy_central::{CentralState, ScanStateChange};
    use fuchsia_bluetooth::expectation::Predicate;
    use fuchsia_bluetooth::types::le::RemoteDevice;

    pub fn scan_enabled() -> Predicate<CentralState> {
        Predicate::new(
            |state: &CentralState| -> bool {
                !state.scan_state_changes.is_empty()
                    && state.scan_state_changes.last() == Some(&ScanStateChange::ScanEnabled)
            },
            Some("Scan was enabled"),
        )
    }
    pub fn scan_disabled() -> Predicate<CentralState> {
        Predicate::new(
            |state: &CentralState| -> bool {
                !state.scan_state_changes.is_empty()
                    && state.scan_state_changes.last() == Some(&ScanStateChange::ScanDisabled)
            },
            Some("Scan was disabled"),
        )
    }
    pub fn device_found(expected_name: &str) -> Predicate<CentralState> {
        let expected_name = expected_name.to_string();
        let msg = format!("Peer '{}' has been discovered", expected_name);
        let has_expected_name = move |peer: &RemoteDevice| -> bool {
            peer.advertising_data
                .as_ref()
                .and_then(|ad| ad.name.as_ref())
                .iter()
                .any(|&name| name == &expected_name)
        };

        Predicate::new(
            move |state: &CentralState| -> bool {
                !state.remote_devices.is_empty()
                    && state.remote_devices.iter().any(&has_expected_name)
            },
            Some(&msg),
        )
    }
}

fn scan_timeout() -> Duration {
    10.seconds()
}

async fn start_scan(central: &CentralHarness) -> Result<(), Error> {
    let status = central
        .aux()
        .start_scan(None)
        .map_err(|e| e.context("FIDL error sending command").into())
        .on_timeout(scan_timeout().after_now(), move || Err(err_msg("Timed out")))
        .await
        .context("Could not initialize scan")?;
    if let Some(e) = status.error {
        return Err(BTError::from(*e).into());
    }
    Ok(())
}

pub async fn enable_scan(central: CentralHarness) -> Result<(), Error> {
    start_scan(&central).await?;
    let _ = central
        .when_satisfied(
            central_expectation::scan_enabled().and(central_expectation::device_found("Fake")),
            scan_timeout(),
        )
        .await?;
    Ok(())
}

pub async fn enable_and_disable_scan(central: CentralHarness) -> Result<(), Error> {
    start_scan(&central).await?;
    let _ = central.when_satisfied(central_expectation::scan_enabled(), scan_timeout()).await?;
    let _ = central.aux().stop_scan()?;
    let _ = central.when_satisfied(central_expectation::scan_disabled(), scan_timeout()).await?;
    Ok(())
}

// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{anyhow, Context, Error};
use fidl_componentmanager_test as ftest;
use fidl_fuchsia_time as ftime;
use fuchsia_async as fasync;
use fuchsia_component::client;
use fuchsia_zircon::ClockUpdate;
use log::*;

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init().unwrap();
    fuchsia_syslog::set_severity(fuchsia_syslog::levels::WARN);

    let test_proxy = client::connect_to_service::<ftest::TestOutcomeReportMarker>()?;

    let result: Result<i64, Error> = async {
        debug!("requesting fuchsia.time.Maintenance");
        let time_maintenance_proxy = client::connect_to_service::<ftime::MaintenanceMarker>()
            .context("failed to connect to fuchsia.time.Maintenance")?;
        debug!("retrieving UTC clock");
        let clock = time_maintenance_proxy
            .get_writable_utc_clock()
            .await
            .context("failed to get UTC clock")?;
        debug!("received clock");
        clock
            .update(ClockUpdate::new().error_bounds(100))
            .map_err(|s| anyhow!("failed to write to clock: {}", s))?;
        debug!("updated clock");
        let details =
            clock.get_details().map_err(|s| anyhow!("failed to get clock details: {}", s))?;
        debug!("got clock details");
        Ok(details.backstop.into_nanos())
    }
    .await;
    match result {
        Ok(backstop_nanos) => {
            test_proxy
                .report(&mut ftest::TestOutcome::Success(ftest::SuccessOutcome { backstop_nanos }))
                .await
                .expect("failed to report success");
            Ok(())
        }
        Err(e) => {
            test_proxy
                .report(&mut ftest::TestOutcome::Failed(ftest::FailedOutcome {
                    message: e.to_string(),
                }))
                .await
                .expect("failed to report failure");
            Err(e)
        }
    }
}

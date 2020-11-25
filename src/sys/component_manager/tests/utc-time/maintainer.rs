// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{anyhow, Context, Error};
use fidl_componentmanager_test as ftest;
use fidl_fuchsia_time as ftime;
use fuchsia_async::{self as fasync, TimeoutExt};
use fuchsia_component::client;
use fuchsia_zircon::{ClockUpdate, Duration, Signals, Status, Time};
use log::*;

/// Time to set in the UTC clock, as an offset above backstop time.
const TEST_OFFSET: Duration = Duration::from_minutes(2);

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init().unwrap();
    fuchsia_syslog::set_severity(fuchsia_syslog::levels::WARN);

    let test_proxy = client::connect_to_service::<ftest::TestOutcomeReportMarker>()?;

    let result: Result<_, Error> = async {
        debug!("requesting fuchsia.time.Maintenance");
        let time_maintenance_proxy = client::connect_to_service::<ftime::MaintenanceMarker>()
            .context("failed to connect to fuchsia.time.Maintenance")?;
        debug!("retrieving UTC clock");
        let clock = time_maintenance_proxy
            .get_writable_utc_clock()
            .await
            .context("failed to get UTC clock")?;
        debug!("received clock");
        match fasync::OnSignals::new(&clock, Signals::CLOCK_STARTED)
            .on_timeout(Time::after(Duration::from_millis(10)), || Err(Status::TIMED_OUT))
            .await
        {
            Err(Status::TIMED_OUT) => (),
            res => return Err(anyhow!("expected CLOCK_STARTED is not asserted but got {:?}", res)),
        }
        debug!("checked clock signals");
        let details =
            clock.get_details().map_err(|s| anyhow!("failed to get clock details: {}", s))?;
        debug!("got clock details");
        let set_time = details.backstop + TEST_OFFSET;
        clock
            .update(ClockUpdate::new().value(set_time).error_bounds(100))
            .map_err(|s| anyhow!("failed to update the clock: {}", s))?;
        debug!("updated clock");
        Ok((details.backstop.into_nanos(), set_time.into_nanos()))
    }
    .await;
    match result {
        Ok((backstop, current_time)) => {
            test_proxy
                .report(&mut ftest::TestOutcome::Success(ftest::SuccessOutcome {
                    backstop,
                    current_time,
                }))
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

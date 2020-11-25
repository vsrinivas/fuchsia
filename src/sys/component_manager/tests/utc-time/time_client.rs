// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{anyhow, Context, Error};
use fidl_componentmanager_test as ftest;
use fuchsia_async as fasync;
use fuchsia_component::client;
use fuchsia_zircon::{Rights, Signals};
use log::*;

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init().unwrap();
    fuchsia_syslog::set_severity(fuchsia_syslog::levels::WARN);

    let test_proxy = client::connect_to_service::<ftest::TestOutcomeReportMarker>()?;

    let result: Result<_, Error> = async {
        let clock = fuchsia_runtime::duplicate_utc_clock_handle(Rights::READ | Rights::WAIT)
            .context("Failed to get clock from runtime")?;
        fasync::OnSignals::new(&clock, Signals::CLOCK_STARTED)
            .await
            .context("Failed to wait for clock to start")?;
        let details =
            clock.get_details().map_err(|s| anyhow!("failed to get clock details: {}", s))?;
        debug!("got clock details");
        let time = clock.read().context("Failed to read clock")?;
        Ok((details.backstop.into_nanos(), time.into_nanos()))
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

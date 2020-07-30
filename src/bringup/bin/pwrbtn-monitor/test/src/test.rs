// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl_fuchsia_test_pwrbtn as test_pwrbtn, fuchsia_async as fasync,
    fuchsia_component::client as fclient,
    fuchsia_syslog::{self as syslog, macros::*},
};

#[fasync::run_singlethreaded(test)]
async fn run() -> Result<(), Error> {
    syslog::init_with_tags(&["pwrbtn-monitor-integration-test"])?;
    fx_log_info!("started");

    let tests_proxy = fclient::connect_to_service::<test_pwrbtn::TestsMarker>()?;
    // Run the tests. If this function returns then we know the tests have passed.
    tests_proxy.run().await?;

    fx_log_info!("test success");
    Ok(())
}

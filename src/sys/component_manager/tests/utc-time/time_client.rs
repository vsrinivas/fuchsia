// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_componentmanager_test as ftest;
use fuchsia_async as fasync;
use fuchsia_component::client;
use fuchsia_zircon::{Rights, Signals};
use tracing::*;

#[fuchsia::component(logging_minimum_severity = "warn")]
async fn main() {
    let clock = fuchsia_runtime::duplicate_utc_clock_handle(Rights::READ | Rights::WAIT).unwrap();
    fasync::OnSignals::new(&clock, Signals::CLOCK_STARTED).await.unwrap();
    let details = clock.get_details().unwrap();
    debug!("got clock details");

    let time = clock.read().unwrap();

    let backstop = details.backstop.into_nanos();
    let time = time.into_nanos();

    let test_proxy = client::connect_to_protocol::<ftest::TestOutcomeReportMarker>().unwrap();

    test_proxy.report(backstop, time).await.unwrap();
}

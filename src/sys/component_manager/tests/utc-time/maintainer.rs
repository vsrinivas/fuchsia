// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_componentmanager_test as ftest;
use fidl_fuchsia_time as ftime;
use fuchsia_async::{self as fasync, TimeoutExt};
use fuchsia_component::{client, server};
use fuchsia_zircon::{ClockUpdate, Duration, Signals, Status, Time};
use futures::StreamExt;
use test_util::assert_geq;
use tracing::*;

enum ExposedServices {
    TestOutcome(ftest::TestOutcomeReportRequestStream),
}

// This value must be kept consistent with the value in integration_test.rs
const EXPECTED_BACKSTOP_TIME_NANOS: i64 = 1589910459000000000;

/// Time to set in the UTC clock, as an offset above backstop time.
const TEST_OFFSET: Duration = Duration::from_minutes(2);

#[fuchsia::component(logging_minimum_severity = "warn")]
async fn main() {
    debug!("requesting fuchsia.time.Maintenance");
    let time_maintenance_proxy = client::connect_to_protocol::<ftime::MaintenanceMarker>().unwrap();

    debug!("retrieving UTC clock");
    let clock = time_maintenance_proxy.get_writable_utc_clock().await.unwrap();

    debug!("received clock");
    match fasync::OnSignals::new(&clock, Signals::CLOCK_STARTED)
        .on_timeout(Time::after(Duration::from_millis(10)), || Err(Status::TIMED_OUT))
        .await
    {
        Err(Status::TIMED_OUT) => (),
        res => panic!("expected CLOCK_STARTED is not asserted but got {:?}", res),
    }

    debug!("checked clock signals");

    let details = clock.get_details().unwrap();
    let maintainer_backstop = details.backstop.into_nanos();
    assert_eq!(maintainer_backstop, EXPECTED_BACKSTOP_TIME_NANOS);

    debug!("got clock details");

    let maintainer_set_time = details.backstop + TEST_OFFSET;
    clock
        .update(ClockUpdate::builder().approximate_value(maintainer_set_time).error_bounds(100))
        .unwrap();
    debug!("updated clock");

    let maintainer_set_time = maintainer_set_time.into_nanos();

    // Wait for the client to report its backstop and current time
    debug!("serving ServiceFs");
    let mut service_fs = server::ServiceFs::new_local();
    service_fs.dir("svc").add_fidl_service(ExposedServices::TestOutcome);
    service_fs.take_and_serve_directory_handle().unwrap();

    let ExposedServices::TestOutcome(mut stream) = service_fs.next().await.unwrap();

    let ftest::TestOutcomeReportRequest::Report {
        backstop: client_backstop,
        current_time: client_current_time,
        responder,
    } = stream.next().await.unwrap().unwrap();

    // Check that times reported by the maintainer and client agree.
    assert_eq!(client_backstop, maintainer_backstop);
    assert_geq!(client_current_time, maintainer_set_time);

    responder.send().unwrap();
}

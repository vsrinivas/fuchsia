// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{Error, ResultExt},
    fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service,
};

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let work_scheduler_control = connect_to_service::<fsys::WorkSchedulerControlMarker>()
        .context("error connecting to WorkSchedulerControl")?;
    work_scheduler_control
        .set_batch_period(1)
        .await
        .expect("connection error setting batch period")
        .expect("error setting batch period");

    let work_scheduler = connect_to_service::<fsys::WorkSchedulerMarker>()
        .context("error connecting to WorkScheduler")?;
    work_scheduler
        .schedule_work(
            "TEST",
            fsys::WorkRequest { start: Some(fsys::Start::MonotonicTime(0)), period: None },
        )
        .await
        .expect("connection error scheduling work item")
        .expect("error scheduling work item");
    Ok(())
}

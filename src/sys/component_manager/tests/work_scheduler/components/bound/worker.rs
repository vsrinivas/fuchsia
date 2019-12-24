// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    fidl_fuchsia_sys2 as fsys, fidl_fuchsia_test_workscheduler as fws, fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service,
    fuchsia_component::server::ServiceFs,
    futures::{StreamExt, TryStreamExt},
};

fn main() -> Result<(), Error> {
    let mut executor = fasync::Executor::new().expect("error creating executor");
    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(move |stream| {
        fasync::spawn_local(run_worker_service(stream));
    });
    fs.take_and_serve_directory_handle().expect("failed to serve outgoing directory");

    let work_scheduler_control = connect_to_service::<fsys::WorkSchedulerControlMarker>()
        .context("error connecting to WorkSchedulerControl")?;
    let work_scheduler = connect_to_service::<fsys::WorkSchedulerMarker>()
        .context("error connecting to WorkScheduler")?;
    fasync::spawn_local(async move {
        work_scheduler_control
            .set_batch_period(1)
            .await
            .expect("connection error setting batch period")
            .expect("error setting batch period");
        work_scheduler
            .schedule_work(
                "TEST",
                fsys::WorkRequest { start: Some(fsys::Start::MonotonicTime(0)), period: None },
            )
            .await
            .expect("connection error scheduling work item")
            .expect("error scheduling work item");
    });

    executor.run_singlethreaded(fs.collect::<()>());

    Ok(())
}

async fn run_worker_service(mut stream: fsys::WorkerRequestStream) {
    let work_scheduler_dispath_reporter =
        connect_to_service::<fws::WorkSchedulerDispatchReporterMarker>()
            .context("error connecting to WorkSchedulerDispatchReporter")
            .unwrap();
    if let Some(event) = stream.try_next().await.expect("failed to serve Worker service") {
        let fsys::WorkerRequest::DoWork { work_id, responder } = event;
        responder.send(&mut Ok(())).expect("failed to send DoWork response");
        work_scheduler_dispath_reporter
            .on_do_work_called(&work_id)
            .await
            .expect("error reporting dispatched work");
    }
}

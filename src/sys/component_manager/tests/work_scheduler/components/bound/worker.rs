// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync,
    fuchsia_component::client::connect_to_protocol, fuchsia_component::server::ServiceFs,
    futures::StreamExt,
};

enum ExposedServices {
    Worker(fsys::WorkerRequestStream),
}

#[fasync::run_singlethreaded]
async fn main() {
    // Schedule some work (against ourselves)
    let work_scheduler_control = connect_to_protocol::<fsys::WorkSchedulerControlMarker>().unwrap();
    let work_scheduler = connect_to_protocol::<fsys::WorkSchedulerMarker>().unwrap();

    work_scheduler_control
        .set_batch_period(1)
        .await
        .expect("connection error setting batch period")
        .expect("error setting batch period");
    work_scheduler
        .schedule_work(
            "TEST",
            fsys::WorkRequest {
                start: Some(fsys::Start::MonotonicTime(0)),
                period: None,
                ..fsys::WorkRequest::EMPTY
            },
        )
        .await
        .expect("connection error scheduling work item")
        .expect("error scheduling work item");

    // Serve the worker protocol and receive the scheduled work
    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(ExposedServices::Worker);
    fs.take_and_serve_directory_handle().expect("failed to serve outgoing directory");

    let ExposedServices::Worker(mut stream) = fs.next().await.unwrap();

    let fsys::WorkerRequest::DoWork { work_id, responder } = stream.next().await.unwrap().unwrap();

    assert_eq!(work_id, "TEST");
    responder.send(&mut Ok(())).unwrap();
}

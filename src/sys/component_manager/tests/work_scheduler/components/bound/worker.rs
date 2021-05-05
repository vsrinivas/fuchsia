// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync,
    fuchsia_component::client::connect_to_protocol, fuchsia_component::server::ServiceFs,
    futures::channel::mpsc, futures::StreamExt,
};

#[fasync::run_singlethreaded]
async fn main() {
    let (tx, mut rx) = mpsc::unbounded();

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

    let mut fs = ServiceFs::new();
    fs.dir("svc").add_fidl_service(move |mut stream: fsys::WorkerRequestStream| {
        let tx = tx.clone();
        fasync::Task::spawn(async move {
            // Get the next work request
            while let Some(Ok(fsys::WorkerRequest::DoWork { work_id, responder })) =
                stream.next().await
            {
                // Send the work request to the main task
                let _ = tx.unbounded_send(work_id);

                // Respond gracefully to the client
                responder.send(&mut Ok(())).unwrap();
            }
        })
        .detach();
    });
    fs.take_and_serve_directory_handle().expect("failed to serve outgoing directory");

    // Serve the outgoing directory
    fasync::Task::spawn(async move {
        fs.collect::<()>().await;
    })
    .detach();

    // Wait for the first work request
    let work_id = rx.next().await.unwrap();
    assert_eq!(work_id, "TEST");
}

// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl_fidl_test_components as ftest, fidl_fuchsia_io as fio, fidl_fuchsia_sys2 as fsys,
    fuchsia_async as fasync,
    fuchsia_component::{client::connect_to_service, server::ServiceFs},
    futures::{StreamExt, TryStreamExt},
};

async fn run_trigger_service(mut stream: ftest::TriggerRequestStream) {
    // Called by the reporter. Starts 3 children under this realm tree. The reporter should see the
    // started events for these events given that it has been offered that event.
    if let Some(event) = stream.try_next().await.expect("failed to serve Trigger") {
        let ftest::TriggerRequest::Run { responder } = event;

        // Notify early to unblock the reporter and then start the children. Otherwise this could
        // deadlock as component manager would be waiting for the reporter to call resume().
        responder.send().expect("respond");

        let realm = connect_to_service::<fsys::RealmMarker>().expect("connect to realm");
        for id in &["a", "b", "c"] {
            let mut child_ref = fsys::ChildRef { name: format!("child_{}", id), collection: None };

            let (_, server_end) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
            realm
                .bind_child(&mut child_ref, server_end)
                .await
                .expect("failed to bind child")
                .unwrap();
        }
    }
}

fn main() -> Result<(), Error> {
    let mut executor = fasync::Executor::new().expect("error creating executor");
    let mut fs = ServiceFs::new_local();

    fs.dir("svc").add_fidl_service(move |stream| {
        fasync::spawn(run_trigger_service(stream));
    });
    fs.take_and_serve_directory_handle()?;

    executor.run_singlethreaded(fs.collect::<()>());

    Ok(())
}

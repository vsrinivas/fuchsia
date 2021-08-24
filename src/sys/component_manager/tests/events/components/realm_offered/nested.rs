// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl_fidl_test_components as ftest, fidl_fuchsia_component as fcomponent,
    fidl_fuchsia_io as fio, fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync,
    fuchsia_component::{
        client::{connect_to_protocol, connect_to_protocol_at_dir_root},
        server::ServiceFs,
    },
    futures::{StreamExt, TryStreamExt},
};

async fn run_trigger_service(mut stream: ftest::TriggerRequestStream) {
    // Called by the reporter. Starts 3 children under this realm tree. The reporter should see the
    // started events for these events given that it has been offered that event.
    if let Some(event) = stream.try_next().await.expect("failed to serve Trigger") {
        let ftest::TriggerRequest::Run { responder } = event;

        // Notify early to unblock the reporter and then start the children. Otherwise this could
        // deadlock as component manager would be waiting for the reporter to call resume().
        responder.send("").expect("respond");

        let realm = connect_to_protocol::<fsys::RealmMarker>().expect("connect to realm");
        for id in &["a", "b", "c"] {
            let mut child_ref = fsys::ChildRef { name: format!("child_{}", id), collection: None };

            let (exposed_dir, server_end) =
                fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
            realm
                .open_exposed_dir(&mut child_ref, server_end)
                .await
                .expect("failed to open exposed dir")
                .unwrap();

            let _ = connect_to_protocol_at_dir_root::<fcomponent::BinderMarker>(&exposed_dir)
                .expect("failed to connect fuchsia.component.Binder");
        }
    }
}

fn main() -> Result<(), Error> {
    let mut executor = fasync::LocalExecutor::new().expect("error creating executor");
    let mut fs = ServiceFs::new_local();

    fs.dir("svc").add_fidl_service(move |stream| {
        fasync::Task::spawn(run_trigger_service(stream)).detach();
    });
    fs.take_and_serve_directory_handle()?;

    executor.run_singlethreaded(fs.collect::<()>());

    Ok(())
}

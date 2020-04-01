// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl_fidl_test_components as ftest, fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    futures::{StreamExt, TryStreamExt},
};

async fn run_trigger_service(mut stream: ftest::TriggerRequestStream) {
    if let Some(event) = stream.try_next().await.expect("failed to serve Trigger") {
        let ftest::TriggerRequest::Run { responder } = event;
        responder.send().expect("respond");
    }
}

/// This component serves the `Trigger` service on three places: /foo, /bar/baz and /qux
#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let mut fs = ServiceFs::new_local();

    fs.dir("foo").add_fidl_service(move |stream| {
        fasync::spawn(run_trigger_service(stream));
    });
    fs.dir("qux").add_fidl_service(move |stream| {
        fasync::spawn(run_trigger_service(stream));
    });
    fs.dir("bar").dir("baz").add_fidl_service(move |stream| {
        fasync::spawn(run_trigger_service(stream));
    });

    fs.take_and_serve_directory_handle().expect("failed to serve outgoing directory");

    fs.collect::<()>().await;
    Ok(())
}

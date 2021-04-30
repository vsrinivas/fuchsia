// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// This program hosts the `Trigger` service, which echoes the command-line arguments when invoked.
use {
    fidl_fidl_examples_routing_echo as fecho, fidl_fidl_test_components as ftest,
    fuchsia_async as fasync,
    fuchsia_component::{client, server::ServiceFs},
    futures::{StreamExt, TryStreamExt},
};

#[fasync::run_singlethreaded]
async fn main() {
    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(move |stream| {
        fasync::Task::local(async move {
            run_trigger_service(stream).await;
        })
        .detach();
    });
    fs.take_and_serve_directory_handle().expect("failed to serve outgoing directory");
    fs.collect::<()>().await;
}

async fn run_trigger_service(mut stream: ftest::TriggerRequestStream) {
    let echo = client::connect_to_protocol::<fecho::EchoMarker>().expect("error connecting to echo");
    while let Some(event) = stream.try_next().await.expect("failed to serve trigger service") {
        let ftest::TriggerRequest::Run { responder } = event;
        let args: Vec<_> = std::env::args().collect();
        let echo_str = args[1..].join(" ");
        let out = echo.echo_string(Some(&echo_str)).await.expect("echo_string failed");
        let out = out.expect("empty echo result");
        responder.send(&out).expect("failed to send trigger response");
    }
}

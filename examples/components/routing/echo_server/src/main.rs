// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fidl_examples_routing_echo as fecho, fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    futures::{StreamExt, TryStreamExt},
};

fn main() {
    let mut executor = fasync::Executor::new().expect("error creating executor");
    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(move |stream| {
        fasync::Task::local(async move {
            run_echo_service(stream).await;
        })
        .detach();
    });
    fs.take_and_serve_directory_handle().expect("failed to serve outgoing directory");
    executor.run_singlethreaded(fs.collect::<()>());
}

async fn run_echo_service(mut stream: fecho::EchoRequestStream) {
    while let Some(event) = stream.try_next().await.expect("failed to serve echo service") {
        let fecho::EchoRequest::EchoString { value, responder } = event;
        responder.send(value.as_ref().map(|s| &**s)).expect("failed to send echo response");
    }
}

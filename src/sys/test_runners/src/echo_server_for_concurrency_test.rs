// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This implementation will only reply back after every 5 requests. It tests that test cases can be
// executed in parallel. IF tests are not executed in parallel, the tests will hang as this
// implementation will never reply back

use {
    fidl_fidl_examples_routing_echo as fecho, fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    futures::{StreamExt, TryStreamExt},
    std::sync::{Arc, Mutex},
};

#[fasync::run_singlethreaded]
async fn main() {
    let mut fs = ServiceFs::new_local();
    let responders = Arc::new(Mutex::new(vec![]));
    fs.dir("svc").add_fidl_service(move |stream| {
        let responders = responders.clone();
        fasync::Task::local(async move {
            run_echo_service(stream, responders).await;
        })
        .detach();
    });
    fs.take_and_serve_directory_handle().expect("failed to serve outgoing directory");
    fs.collect::<()>().await;
}

async fn run_echo_service(
    mut stream: fecho::EchoRequestStream,
    responders: Arc<Mutex<Vec<(fecho::EchoEchoStringResponder, Option<String>)>>>,
) {
    while let Some(event) = stream.try_next().await.expect("failed to serve echo service") {
        let fecho::EchoRequest::EchoString { value, responder } = event;
        let mut responders = responders.lock().unwrap();
        responders.push((responder, value));
        if responders.len() == 5 {
            while let Some((r, v)) = responders.pop() {
                r.send(v.as_ref().map(|s| &**s)).expect("failed to send echo response");
            }
        }
    }
}

// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_test_ping::{PingRequest, PingRequestStream},
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    futures::prelude::*,
};

enum IncomingRequest {
    Ping(PingRequestStream),
}

#[fasync::run_singlethreaded]
async fn main() {
    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(IncomingRequest::Ping);
    fs.take_and_serve_directory_handle().expect("failed to take startup handle");
    fs.for_each_concurrent(0, |IncomingRequest::Ping(mut stream)| async move {
        while let Some(PingRequest::Ping { ping, responder }) =
            stream.try_next().await.expect("failed to read request")
        {
            responder.send(&format!("{} pong", ping)).expect("failed to send pong");
        }
    })
    .await;
}

// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fidl_test_components as test_protocol, fuchsia_component::server::ServiceFs,
    futures_util::StreamExt, tracing::info,
};

enum Protocols {
    Trigger(test_protocol::TriggerRequestStream),
}

/// Connects to the Trigger protocol, sends a request, and exits.
#[fuchsia::main]
async fn main() {
    info!("Rendezvous starting");

    let mut fs = ServiceFs::new();
    fs.dir("svc").add_fidl_service(Protocols::Trigger);
    fs.take_and_serve_directory_handle().unwrap();

    let Protocols::Trigger(mut stream) = fs.next().await.unwrap();
    let test_protocol::TriggerRequest::Run { responder } = stream.next().await.unwrap().unwrap();
    responder.send("Rendezvous complete!").unwrap();

    info!("Rendezvous complete, exiting");
}

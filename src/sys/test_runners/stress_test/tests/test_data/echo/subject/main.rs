// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{self, Context};
use fidl_fidl_examples_routing_echo::{EchoRequest, EchoRequestStream};
use fuchsia_component::server::ServiceFs;
use futures::prelude::*;

// Wrap protocol requests being served.
enum IncomingRequest {
    Echo(EchoRequestStream),
}

#[fuchsia::component(logging = false)]
async fn main() -> Result<(), anyhow::Error> {
    let mut service_fs = ServiceFs::new_local();
    service_fs.dir("svc").add_fidl_service(IncomingRequest::Echo);
    service_fs.take_and_serve_directory_handle().context("failed to serve outgoing namespace")?;
    service_fs
        .for_each_concurrent(None, |_request: IncomingRequest| async move {
            match _request {
                IncomingRequest::Echo(stream) => handle_echo_request(stream).await,
            }
        })
        .await;

    Ok(())
}

async fn handle_echo_request(mut stream: EchoRequestStream) {
    while let Some(event) = stream.try_next().await.expect("failed to serve echo service") {
        let EchoRequest::EchoString { value, responder } = event;
        responder.send(value.as_ref().map(|s| &**s)).expect("failed to send echo response");
    }
}

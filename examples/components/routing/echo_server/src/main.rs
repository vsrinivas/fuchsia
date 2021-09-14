// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// [START imports]
use anyhow::{self, Context};
use fidl_fidl_examples_routing_echo::{EchoRequest, EchoRequestStream};
use fuchsia_component::server::ServiceFs;
use fuchsia_inspect::{component, health::Reporter};
use futures::prelude::*;
// [END imports]

// [START main_body]
// Wrap protocol requests being served.
enum IncomingRequest {
    Echo(EchoRequestStream),
}

#[fuchsia::component(logging = false)]
async fn main() -> Result<(), anyhow::Error> {
    let mut service_fs = ServiceFs::new_local();

    // Initialize inspect
    component::health().set_starting_up();
    inspect_runtime::serve(component::inspector(), &mut service_fs)?;

    // Serve the Echo protocol
    service_fs.dir("svc").add_fidl_service(IncomingRequest::Echo);
    service_fs.take_and_serve_directory_handle().context("failed to serve outgoing namespace")?;

    // Component is serving and ready to handle incoming requests
    component::health().set_ok();

    // Attach request handler for incoming requests
    service_fs
        .for_each_concurrent(None, |_request: IncomingRequest| async move {
            match _request {
                IncomingRequest::Echo(stream) => handle_echo_request(stream).await,
            }
        })
        .await;

    Ok(())
}
// [END main_body]

// [START handler]
// Handler for incoming service requests
async fn handle_echo_request(mut stream: EchoRequestStream) {
    while let Some(event) = stream.try_next().await.expect("failed to serve echo service") {
        let EchoRequest::EchoString { value, responder } = event;
        responder.send(value.as_ref().map(|s| &**s)).expect("failed to send echo response");
    }
}
// [END handler]

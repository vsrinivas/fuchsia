// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{self, Context};
use fuchsia_component::server::ServiceFs;
use fuchsia_inspect::{component, health::Reporter};
use futures::prelude::*;

// [START server_declarations]
use fidl_fidl_examples_routing_echo::{EchoRequest, EchoRequestStream};
use std::sync::atomic::{AtomicU64, Ordering};

static REQUEST_COUNT: AtomicU64 = AtomicU64::new(1);
enum IncomingRequest {
    Echo(EchoRequestStream),
}
// [END server_declarations]

// [START main_body]
#[fuchsia::component(logging = false)]
async fn main() -> Result<(), anyhow::Error> {
    let mut service_fs = ServiceFs::new_local();

    // Initialize inspect
    inspect_runtime::serve(component::inspector(), &mut service_fs)?;
    component::health().set_starting_up();

    // Add echo service
    service_fs.dir("svc").add_fidl_service(IncomingRequest::Echo);
    service_fs.take_and_serve_directory_handle().context("failed to serve outgoing namespace")?;

    // Begin serving to handle incoming requests
    // [START server_inspect]
    service_fs
        .for_each_concurrent(None, |_request: IncomingRequest| async move {
            match _request {
                IncomingRequest::Echo(stream) => handle_echo_request(stream).await,
            }

            // Record the request in Inspect
            let request_count = REQUEST_COUNT.fetch_add(1, Ordering::SeqCst);
            component::inspector().root().record_uint("request_count", request_count);
        })
        .await;

    component::health().set_ok();
    // [END server_inspect]

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

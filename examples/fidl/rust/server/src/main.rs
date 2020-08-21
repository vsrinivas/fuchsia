// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Note: this file must be kept in sync with
// docs/development/languages/fidl/tutorials/rust/basic/server.md

use anyhow::{Context as _, Error};
use fidl_fuchsia_examples::{EchoRequest, EchoRequestStream};
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use futures::prelude::*;

// [START impl]
// An implementation of the Echo stream, which handles a stream of EchoRequests
async fn run_echo_server(stream: EchoRequestStream) -> Result<(), Error> {
    stream
        .map(|result| result.context("failed request"))
        .try_for_each(|request| async move {
            match request {
                // Handle each EchoString request by responding with the request
                // value
                EchoRequest::EchoString { value, responder } => {
                    println!("Received EchoString request for string {:?}", value);
                    responder.send(&value).context("error sending response")?;
                    println!("Response sent successfully");
                }
                // Handle each SendString request by sending a single OnString
                // event with the request value
                EchoRequest::SendString { value, control_handle } => {
                    println!("Received SendString request for string {:?}", value);
                    control_handle.send_on_string(&value).context("error sending event")?;
                    println!("Event sent successfully");
                }
            }
            Ok(())
        })
        .await
}
// [END impl]

// [START enum]
enum IncomingService {
    // Host a service protocol.
    Echo(EchoRequestStream),
    // ... more services here
}
// [END enum]

// [START main]
#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    // Initialize the outgoing services provided by this component
    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(IncomingService::Echo);

    // Serve the outgoing services
    fs.take_and_serve_directory_handle()?;

    // Listen for incoming requests to connect to Echo, and call run_echo_server
    // on each one
    println!("Listening for incoming connections...");
    const MAX_CONCURRENT: usize = 10_000;
    fs.for_each_concurrent(MAX_CONCURRENT, |IncomingService::Echo(stream)| {
        run_echo_server(stream).unwrap_or_else(|e| println!("{:?}", e))
    })
    .await;

    Ok(())
}
// [END main]

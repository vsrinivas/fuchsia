// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context as _, Error};
use fidl::endpoints::create_endpoints;
use fidl_fuchsia_examples::{
    EchoLauncherRequest, EchoLauncherRequestStream, EchoMarker, EchoRequest, EchoRequestStream,
};
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use futures::prelude::*;

// [START echo-impl]
// An Echo implementation that adds a prefix to every response
async fn run_echo_server(stream: EchoRequestStream, prefix: &str) -> Result<(), Error> {
    stream
        .map(|result| result.context("failed request"))
        .try_for_each(|request| async move {
            match request {
                // The SendString request is not used in this example, so just
                // ignore it
                EchoRequest::SendString { value: _, control_handle: _ } => {}
                EchoRequest::EchoString { value, responder } => {
                    println!("Got echo request for prefix {}", prefix);
                    let response = format!("{}: {}", prefix, value);
                    responder.send(&response).context("error sending response")?;
                }
            }
            Ok(())
        })
        .await
}
// [END echo-impl]

// [START launcher-impl]
// The EchoLauncher implementation that launches Echo servers with the specified
// prefix
async fn run_echo_launcher_server(stream: EchoLauncherRequestStream) -> Result<(), Error> {
    // Currently the client only connects at most two Echo clients for each EchoLauncher
    stream
        .map(|result| result.context("request error"))
        .try_for_each_concurrent(2, |request| async move {
            let (echo_prefix, server_end) = match request {
                // In the non pipelined case, we need to initialize the
                // communication channel ourselves
                EchoLauncherRequest::GetEcho { echo_prefix, responder } => {
                    println!("Got non pipelined request");
                    let (client_end, server_end) = create_endpoints::<EchoMarker>()?;
                    responder.send(client_end)?;
                    (echo_prefix, server_end)
                }
                // In the pipelined case, the client is responsible for
                // initializing the channel, and passes the server its end of
                // the channel
                EchoLauncherRequest::GetEchoPipelined {
                    echo_prefix,
                    request,
                    control_handle: _,
                } => {
                    println!("Got pipelined request");
                    (echo_prefix, request)
                }
            };
            // Run the Echo server with the specified prefix
            run_echo_server(server_end.into_stream()?, &echo_prefix).await
        })
        .await
}
// [END launcher-impl]

// [START main]
enum IncomingService {
    EchoLauncher(EchoLauncherRequestStream),
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(IncomingService::EchoLauncher);
    fs.take_and_serve_directory_handle()?;

    const MAX_CONCURRENT: usize = 1000;
    let fut = fs.for_each_concurrent(MAX_CONCURRENT, |IncomingService::EchoLauncher(stream)| {
        run_echo_launcher_server(stream).unwrap_or_else(|e| println!("{:?}", e))
    });

    println!("Running echo launcher server");
    fut.await;
    Ok(())
}
// [END main]

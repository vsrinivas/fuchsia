// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// [START import_declarations]
use anyhow::{Context as _, Error};
use fidl_fidl_examples_echo::{EchoRequest, EchoRequestStream, EchoServiceRequest};
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use futures::prelude::*;
// [END import_declarations]

// [START run_echo_server]
async fn run_echo_server(
    mut stream: EchoRequestStream,
    quiet: bool,
    prefix: Option<&str>,
) -> Result<(), Error> {
    while let Some(EchoRequest::EchoString { value, responder }) =
        stream.try_next().await.context("error running echo server")?
    {
        if !quiet {
            println!("Received echo request for string {:?}", value);
        }
        let response = match (&prefix, value.as_ref()) {
            (Some(prefix), Some(value)) => Some(format!("{}: {}", prefix, value)),
            _ => value,
        };
        responder.send(response.as_ref().map(|s| s.as_str())).context("error sending response")?;
        if !quiet {
            println!("echo response sent successfully");
        }
    }
    Ok(())
}
// [END run_echo_server]

enum IncomingService {
    // Host a service protocol.
    Echo(EchoRequestStream),
    // Host a unified service.
    Svc(EchoServiceRequest),
    // ... more services here
}

// [START main]
#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let quiet = std::env::args().any(|arg| arg == "-q");

    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(IncomingService::Echo).add_unified_service(IncomingService::Svc);

    fs.take_and_serve_directory_handle()?;

    const MAX_CONCURRENT: usize = 10_000;
    let fut = fs.for_each_concurrent(MAX_CONCURRENT, |request| {
        match request {
            IncomingService::Echo(stream) => run_echo_server(stream, quiet, None),
            IncomingService::Svc(EchoServiceRequest::Foo(stream)) => {
                run_echo_server(stream, quiet, Some("foo"))
            }
            IncomingService::Svc(EchoServiceRequest::Bar(stream)) => {
                run_echo_server(stream, quiet, Some("bar"))
            }
        }
        .unwrap_or_else(|e| println!("{:?}", e))
    });

    fut.await;
    Ok(())
}
// [END main]

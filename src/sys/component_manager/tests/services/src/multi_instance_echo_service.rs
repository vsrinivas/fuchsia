// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context as _, Error};
use fidl_fuchsia_examples::{EchoRequest, EchoRequestStream, EchoServiceRequest};
use fuchsia_component::server::ServiceFs;
use futures::prelude::*;
use tracing::*;

async fn run_echo_server(
    mut stream: EchoRequestStream,
    prefix: String,
    reverse: bool,
) -> Result<(), Error> {
    while let Some(EchoRequest::EchoString { value, responder }) =
        stream.try_next().await.context("error running echo server")?
    {
        println!("Received EchoString request for string {:?}", value);
        let echo_string = if reverse { value.chars().rev().collect() } else { value.clone() };
        let resp = vec![prefix.clone(), echo_string].join("");
        responder.send(&resp).context("error sending response")?;
        println!("Response sent successfully");
    }
    Ok(())
}

enum IncomingService {
    Default(EchoServiceRequest),
    Hello(EchoServiceRequest),
    Goodbye(EchoServiceRequest),
}

// This component hosts multiple implementations of the same service with different implementations.
#[fuchsia::main]
async fn main() -> Result<(), Error> {
    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_unified_service(IncomingService::Default);
    fs.dir("svc").add_unified_service_instance("hello", IncomingService::Hello);
    fs.dir("svc").add_unified_service_instance("goodbye", IncomingService::Goodbye);
    fs.take_and_serve_directory_handle()?;

    fs.for_each_concurrent(None, |request| {
        match request {
            IncomingService::Default(EchoServiceRequest::RegularEcho(stream)) => {
                run_echo_server(stream, "".to_string(), false)
            }
            IncomingService::Default(EchoServiceRequest::ReversedEcho(stream)) => {
                run_echo_server(stream, "".to_string(), true)
            }
            IncomingService::Hello(EchoServiceRequest::RegularEcho(stream)) => {
                run_echo_server(stream, "hello".to_string(), false)
            }
            IncomingService::Hello(EchoServiceRequest::ReversedEcho(stream)) => {
                run_echo_server(stream, "hello".to_string(), true)
            }
            IncomingService::Goodbye(EchoServiceRequest::RegularEcho(stream)) => {
                run_echo_server(stream, "goodbye".to_string(), false)
            }
            IncomingService::Goodbye(EchoServiceRequest::ReversedEcho(stream)) => {
                run_echo_server(stream, "goodbye".to_string(), true)
            }
        }
        .unwrap_or_else(|e| {
            info!("Error serving multi instance echo service {:?}", e);
            error!("{:?}", e)
        })
    })
    .await;

    Ok(())
}

// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context as _, Error};
use fidl_fuchsia_examples::{EchoRequest, EchoRequestStream, EchoServiceRequest};
use fuchsia_component::server::ServiceFs;
use futures::prelude::*;

async fn run_echo_server(mut stream: EchoRequestStream, reverse: bool) -> Result<(), Error> {
    while let Some(request) = stream.try_next().await.context("error running echo server")? {
        match request {
            EchoRequest::SendString { value: _, control_handle: _ } => {
                println!("Received SendString");
            }
            EchoRequest::EchoString { value, responder } => {
                println!("Received EchoString request for string {:?}", value);
                let resp = if reverse { value.chars().rev().collect() } else { value.clone() };
                responder.send(&resp).context("error sending response")?;
                println!("Response sent successfully");
            }
        }
    }
    Ok(())
}

enum IncomingService {
    // Host a unified service.
    Svc(EchoServiceRequest),
    // ... more services here
}

#[fuchsia::component]
async fn main() -> Result<(), Error> {
    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_unified_service(IncomingService::Svc);
    fs.take_and_serve_directory_handle()?;

    const MAX_CONCURRENT: usize = 10_000;
    fs.for_each_concurrent(MAX_CONCURRENT, |request| {
        match request {
            IncomingService::Svc(EchoServiceRequest::RegularEcho(stream)) => {
                run_echo_server(stream, false)
            }
            IncomingService::Svc(EchoServiceRequest::ReversedEcho(stream)) => {
                run_echo_server(stream, true)
            }
        }
        .unwrap_or_else(|e| println!("{:?}", e))
    })
    .await;
    Ok(())
}

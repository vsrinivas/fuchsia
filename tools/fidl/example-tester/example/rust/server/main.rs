// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context as _, Error};
use fidl_test_exampletester::{SimpleRequest, SimpleRequestStream};
use fuchsia_component::server::ServiceFs;
use futures::prelude::*;

async fn run_server(stream: SimpleRequestStream) -> Result<(), Error> {
    stream
        .map(|result| result.context("failed request"))
        .try_for_each(|request| async move {
            match request {
                SimpleRequest::Add { augend, addend, responder } => {
                    println!("Request received");
                    responder.send((&augend + &addend) as u16).context("error sending response")?;
                    println!("Response sent");
                }
            }
            Ok(())
        })
        .await
}

enum IncomingService {
    Simple(SimpleRequestStream),
}

#[fuchsia::main]
async fn main() -> Result<(), Error> {
    println!("Started");
    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(IncomingService::Simple);
    fs.take_and_serve_directory_handle()?;
    println!("Listening for incoming connections");
    const MAX_CONCURRENT: usize = 10_000;
    fs.for_each_concurrent(MAX_CONCURRENT, |IncomingService::Simple(stream)| {
        run_server(stream).unwrap_or_else(|e| println!("{:?}", e))
    })
    .await;

    Ok(())
}

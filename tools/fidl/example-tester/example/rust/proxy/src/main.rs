// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context as _, Error};
use fidl_test_exampletester::{SimpleMarker, SimpleProxy, SimpleRequest, SimpleRequestStream};
use fuchsia_component::client::connect_to_protocol;
use fuchsia_component::server::ServiceFs;
use futures::prelude::*;

// Proxy through a single connection
async fn serve_proxy(client: &SimpleProxy, stream: SimpleRequestStream) -> Result<(), Error> {
    stream
        .map(|result| result.context("failed request"))
        .try_for_each(|request| async move {
            match request {
                SimpleRequest::Add { augend, addend, responder } => {
                    println!("Request received");
                    let sum = &client.add(augend, addend).await?;
                    responder.send(sum.to_owned()).context("error sending response")?;
                    println!("Response sent");
                }
            }
            Ok(())
        })
        .await
}

fn create_async_client() -> Result<SimpleProxy, Error> {
    connect_to_protocol::<SimpleMarker>().context("Failed to connect to server")
}

enum IncomingService {
    Simple(SimpleRequestStream),
}

#[fuchsia::main]
async fn main() -> Result<(), Error> {
    println!("Started");
    println!("trim me (Rust)");

    let client = create_async_client()?;
    println!("Outgoing connection enabled");

    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(IncomingService::Simple);
    fs.take_and_serve_directory_handle()?;
    println!("Listening for incoming connections");

    const MAX_CONCURRENT: usize = 10_000;
    fs.for_each_concurrent(MAX_CONCURRENT, |IncomingService::Simple(stream)| {
        serve_proxy(&client, stream).unwrap_or_else(|e| println!("{:?}", e))
    })
    .await;

    Ok(())
}

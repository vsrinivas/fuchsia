// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    fidl_examples_keyvaluestore::{Item, StoreRequest, StoreRequestStream, WriteError},
    fuchsia_component::server::ServiceFs,
    futures::prelude::*,
    lazy_static::lazy_static,
    regex::Regex,
    std::cell::RefCell,
    std::collections::HashMap,
};

lazy_static! {
    static ref KEY_VALIDATION_REGEX: Regex =
        Regex::new(r"^[A-Za-z][A-Za-z0-9_\./]{2,62}[A-Za-z0-9]$")
            .expect("Key validation regex failed to compile");
}

/// Handler for the `WriteItem` method.
fn write_item(store: &mut HashMap<String, Vec<u8>>, attempt: Item) -> Result<(), WriteError> {
    // Validate the key.
    if !KEY_VALIDATION_REGEX.is_match(attempt.key.as_str()) {
        println!("Write error: INVALID_KEY, For key: {}", attempt.key);
        return Err(WriteError::InvalidKey);
    }

    // Validate the value.
    if attempt.value.is_empty() {
        println!("Write error: INVALID_VALUE, For key: {}", attempt.key);
        return Err(WriteError::InvalidValue);
    }

    // Write to the store, validating that the key did not already exist.
    match store.insert(attempt.key.clone(), attempt.value) {
        Some(_) => {
            println!("Write error: ALREADY_EXISTS, For key: {}", attempt.key);
            Err(WriteError::AlreadyExists)
        }
        None => {
            println!("Wrote value at key: {}", attempt.key);
            Ok(())
        }
    }
}

/// Creates a new instance of the server. Each server has its own bespoke, per-connection instance
/// of the key-value store.
async fn run_server(stream: StoreRequestStream) -> Result<(), Error> {
    // Create a new in-memory key-value store. The store will live for the lifetime of the
    // connection between the server and this particular client.
    let store = RefCell::new(HashMap::<String, Vec<u8>>::new());

    // Serve all requests on the protocol sequentially - a new request is not handled until its
    // predecessor has been processed.
    stream
        .map(|result| result.context("failed request"))
        .try_for_each(|request| async {
            // Match based on the method being invoked.
            match request {
                StoreRequest::WriteItem { attempt, responder } => {
                    println!("WriteItem request received");

                    // The `responder` parameter is a special struct that manages the outgoing reply
                    // to this method call. Calling `send` on the responder exactly once will send
                    // the reply.
                    responder
                        .send(&mut write_item(&mut store.borrow_mut(), attempt))
                        .context("error sending reply")?;
                    println!("WriteItem response sent");
                }
            }
            Ok(())
        })
        .await
}

// A helper enum that allows us to treat a `Store` service instance as a value.
enum IncomingService {
    Store(StoreRequestStream),
}

#[fuchsia::main]
async fn main() -> Result<(), Error> {
    println!("Started");

    // Add a discoverable instance of our `Store` protocol - this will allow the client to see the
    // server and connect to it.
    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(IncomingService::Store);
    fs.take_and_serve_directory_handle()?;
    println!("Listening for incoming connections");

    // The maximum number of concurrent clients that may be served by this process.
    const MAX_CONCURRENT: usize = 10;

    // Serve each connection simultaneously, up to the `MAX_CONCURRENT` limit.
    fs.for_each_concurrent(MAX_CONCURRENT, |IncomingService::Store(stream)| {
        run_server(stream).unwrap_or_else(|e| println!("{:?}", e))
    })
    .await;

    Ok(())
}

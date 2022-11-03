// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    fuchsia_component::server::ServiceFs,
    futures::prelude::*,
    lazy_static::lazy_static,
    regex::Regex,
    std::cell::RefCell,
    std::collections::hash_map::Entry,
    std::collections::HashMap,
};

// [START diff_1]
use {
    fidl::{encoding::encode_persistent, Vmo},
    fidl_examples_keyvaluestore_supportexports::{
        ExportError, Exportable, Item, StoreRequest, StoreRequestStream, WriteError,
    },
};
// [END diff_1]

lazy_static! {
    static ref KEY_VALIDATION_REGEX: Regex =
        Regex::new(r"^[A-Za-z]\w+[A-Za-z0-9]$").expect("Key validation regex failed to compile");
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
    match store.entry(attempt.key) {
        Entry::Occupied(entry) => {
            println!("Write error: ALREADY_EXISTS, For key: {}", entry.key());
            Err(WriteError::AlreadyExists)
        }
        Entry::Vacant(entry) => {
            println!("Wrote value at key: {}", entry.key());
            entry.insert(attempt.value);
            Ok(())
        }
    }
}

// [START diff_2]
/// Handler for the `Export` method.
fn export(store: &mut HashMap<String, Vec<u8>>, vmo: Vmo) -> Result<Vmo, ExportError> {
    // Empty stores cannot be exported.
    if store.is_empty() {
        return Err(ExportError::Empty);
    }

    // Build the `Exportable` vector locally. That means iterating over the map, and turning it into
    // a vector of items instead.
    let mut exportable = Exportable::EMPTY;
    let mut items = store
        .iter()
        .map(|entry| return Item { key: entry.0.clone(), value: entry.1.clone() })
        .collect::<Vec<Item>>();
    items.sort_by(|a, b| a.key.cmp(&b.key));
    exportable.items = Some(items);

    // Encode the bytes - there is a bug in persistent FIDL if this operation fails. Even if it
    // succeeds, make sure to check that the VMO has enough space to handle the encoded export data.
    let encoded_bytes = encode_persistent(&mut exportable).map_err(|_| ExportError::Unknown)?;
    if encoded_bytes.len() as u64 > vmo.get_content_size().map_err(|_| ExportError::Unknown)? {
        return Err(ExportError::StorageTooSmall);
    }

    // Write the (now encoded) persistent FIDL data to the VMO.
    vmo.set_content_size(&(encoded_bytes.len() as u64)).map_err(|_| ExportError::Unknown)?;
    vmo.write(&encoded_bytes, 0).map_err(|_| ExportError::Unknown)?;
    Ok(vmo)
}
// [END diff_2]

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
                // [START diff_3]
                StoreRequest::Export { empty, responder } => {
                    println!("Export request received");

                    responder
                        .send(&mut export(&mut store.borrow_mut(), empty))
                        .context("error sending reply")?;
                    println!("Export response sent");
                } //
                  // [END diff_3]
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

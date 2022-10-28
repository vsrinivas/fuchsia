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
    fidl_examples_keyvaluestore_usegenericvalues::{
        Item, StoreRequest, StoreRequestStream, Value, WriteError, WriteOptions,
    },
    std::collections::hash_map::OccupiedEntry,
    std::ops::Add,
};
// [END diff_1]

lazy_static! {
    static ref KEY_VALIDATION_REGEX: Regex =
        Regex::new(r"^[A-Za-z]\w+[A-Za-z0-9]$").expect("Key validation regex failed to compile");
}

// [START diff_2]
/// Sums any numeric type.
fn sum<T: Add + Add<Output = T> + Copy>(operands: [T; 2]) -> T {
    operands[0] + operands[1]
}

/// Clones and inserts an entry, so that the original (now concatenated) copy may be returned in the
/// response.
fn write(inserting: Value, mut entry: OccupiedEntry<'_, String, Value>) -> Value {
    entry.insert(inserting.clone());
    println!("Wrote key: {}, value: {:?}", entry.key(), &inserting);
    inserting
}

/// Handler for the `WriteItem` method.
fn write_item(
    store: &mut HashMap<String, Value>,
    attempt: Item,
    options: &WriteOptions,
) -> Result<Value, WriteError> {
    // [END diff_2]
    // Validate the key.
    if !KEY_VALIDATION_REGEX.is_match(attempt.key.as_str()) {
        println!("Write error: INVALID_KEY for key: {}", attempt.key);
        return Err(WriteError::InvalidKey);
    }

    // [START diff_3]
    match store.entry(attempt.key) {
        Entry::Occupied(entry) => {
            // The `CONCAT` flag supersedes the `OVERWRITE` flag, so check it first.
            if options.contains(WriteOptions::CONCAT) {
                match entry.get() {
                    Value::Bytes(old) => {
                        if let Value::Bytes(new) = attempt.value {
                            let mut combined = old.clone();
                            combined.extend(new);
                            return Ok(write(Value::Bytes(combined), entry));
                        }
                    }
                    Value::String(old) => {
                        if let Value::String(new) = attempt.value {
                            return Ok(write(Value::String(format!("{}{}", old, &new)), entry));
                        }
                    }
                    Value::Uint64(old) => {
                        if let Value::Uint64(new) = attempt.value {
                            return Ok(write(Value::Uint64(sum([*old, new])), entry));
                        }
                    }
                    Value::Int64(old) => {
                        if let Value::Int64(new) = attempt.value {
                            return Ok(write(Value::Int64(sum([*old, new])), entry));
                        }
                    }
                    // Note: only works on the uint64 range in practice.
                    Value::Uint128(old) => {
                        if let Value::Uint128(new) = attempt.value {
                            return Ok(write(Value::Uint128([0, sum([old[1], new[1]])]), entry));
                        }
                    }
                    _ => {
                        panic!("actively unsupported type!")
                    }
                }

                // Only reachable if the type of the would be concatenated value did not match the
                // value already occupying this entry.
                println!("Write error: INVALID_VALUE for key: {}", entry.key());
                return Err(WriteError::InvalidValue);
            }

            // If we're not doing CONCAT, check for OVERWRITE next.
            if options.contains(WriteOptions::OVERWRITE) {
                return Ok(write(attempt.value, entry));
            }

            println!("Write error: ALREADY_EXISTS for key: {}", entry.key());
            Err(WriteError::AlreadyExists)
        }
        Entry::Vacant(entry) => {
            println!("Wrote key: {}, value: {:?}", entry.key(), &attempt.value);
            entry.insert(attempt.value.clone());
            Ok(attempt.value)
        }
    }
    // [END diff_3]
}

/// Creates a new instance of the server. Each server has its own bespoke, per-connection instance
/// of the key-value store.
async fn run_server(stream: StoreRequestStream) -> Result<(), Error> {
    // Create a new in-memory key-value store. The store will live for the lifetime of the
    // connection between the server and this particular client.
    let store = RefCell::new(HashMap::<String, Value>::new());

    // Serve all requests on the protocol sequentially - a new request is not handled until its
    // predecessor has been processed.
    stream
        .map(|result| result.context("failed request"))
        .try_for_each(|request| async {
            // Match based on the method being invoked.
            match request {
                // [START diff_4]
                // Because we are using a table payload, there is an extra level of indirection. The
                // top-level container for the table itself is always called "payload".
                StoreRequest::WriteItem { payload, responder } => {
                    println!("WriteItem request received");

                    // Error out if either of the request table's members are not set.
                    let attempt = payload.attempt.context("required field 'attempt' is unset")?;
                    let options = payload.options.context("required field 'options' is unset")?;

                    // The `responder` parameter is a special struct that manages the outgoing reply
                    // to this method call. Calling `send` on the responder exactly once will send
                    // the reply.
                    responder
                        .send(&mut write_item(&mut store.borrow_mut(), attempt, &options))
                        .context("error sending reply")?;
                    println!("WriteItem response sent");
                } //
                  // [END diff_4]
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

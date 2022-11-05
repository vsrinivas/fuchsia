// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    fuchsia_component::server::ServiceFs,
    futures::prelude::*,
    lazy_static::lazy_static,
    regex::Regex,
};

// [START diff_1]
use {
    fidl_examples_keyvaluestore_additerator::{
        Item, IterateConnectionError, IteratorRequest, IteratorRequestStream, StoreRequest,
        StoreRequestStream, WriteError,
    },
    fuchsia_async as fasync,
    std::collections::btree_map::Entry,
    std::collections::BTreeMap,
    std::ops::Bound::*,
    std::sync::Arc,
    std::sync::Mutex,
};
// [END diff_1]

lazy_static! {
    static ref KEY_VALIDATION_REGEX: Regex =
        Regex::new(r"^[A-Za-z]\w+[A-Za-z0-9]$").expect("Key validation regex failed to compile");
}

/// Handler for the `WriteItem` method.
// [START diff_2]
fn write_item(store: &mut BTreeMap<String, Vec<u8>>, attempt: Item) -> Result<(), WriteError> {
    // [END diff_2]
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

// [START diff_3]
/// Handler for the `Iterate` method, which deals with validating that the requested start position
/// exists, and then sets up the asynchronous side channel for the actual iteration to occur over.
fn iterate(
    store: Arc<Mutex<BTreeMap<String, Vec<u8>>>>,
    starting_at: Option<String>,
    stream: IteratorRequestStream,
) -> Result<(), IterateConnectionError> {
    // Validate that the starting key, if supplied, actually exists.
    if let Some(start_key) = starting_at.clone() {
        if !store.lock().unwrap().contains_key(&start_key) {
            return Err(IterateConnectionError::UnknownStartAt);
        }
    }

    // Spawn a detached task. This allows the method call to return while the iteration continues in
    // a separate, unawaited task.
    fasync::Task::spawn(async move {
        // Serve the iteration requests. Note that access to the underlying store is behind a
        // contended `Mutex`, meaning that the iteration is not atomic: page contents could shift,
        // change, or disappear entirely between `Get()` requests.
        stream
            .map(|result| result.context("failed request"))
            .try_fold(
                match starting_at {
                    Some(start_key) => Included(start_key),
                    None => Unbounded,
                },
                |mut lower_bound, request| async {
                    match request {
                        IteratorRequest::Get { responder } => {
                            println!("Iterator page request received");

                            // The `page_size` should be kept in sync with the size constraint on
                            // the iterator's response, as defined in the FIDL protocol.
                            static PAGE_SIZE: usize = 10;

                            // An iterator, beginning at `lower_bound` and tracking the pagination's
                            // progress through iteration as each page is pulled by a client-sent
                            // `Get()` request.
                            let held_store = store.lock().unwrap();
                            let mut entries = held_store.range((lower_bound.clone(), Unbounded));
                            let mut current_page = vec![];
                            for _ in 0..PAGE_SIZE {
                                match entries.next() {
                                    Some(entry) => {
                                        current_page.push(entry.0.clone());
                                    }
                                    None => break,
                                }
                            }

                            // Update the `lower_bound` - either inclusive of the next item in the
                            // iteration, or exclusive of the last seen item if the iteration has
                            // finished. This `lower_bound` will be passed to the next request
                            // handler as its starting point.
                            lower_bound = match entries.next() {
                                Some(next) => Included(next.0.clone()),
                                None => match current_page.last() {
                                    Some(tail) => Excluded(tail.clone()),
                                    None => lower_bound,
                                },
                            };

                            // Send the page. At the end of this scope, the `held_store` lock gets
                            // dropped, and therefore released.
                            responder
                                .send(&mut current_page.iter().map(String::as_str))
                                .context("error sending reply")?;
                            println!("Iterator page sent");
                        }
                    }
                    Ok(lower_bound)
                },
            )
            .await
            .ok();
    })
    .detach();

    Ok(())
}
// [END diff_3]

/// Creates a new instance of the server. Each server has its own bespoke, per-connection instance
/// of the key-value store.
async fn run_server(stream: StoreRequestStream) -> Result<(), Error> {
    // [START diff_4]
    // Create a new in-memory key-value store. The store will live for the lifetime of the
    // connection between the server and this particular client.
    //
    // Note that we now use an `Arc<Mutex<BTreeMap>>`, replacing the previous `RefCell<HashMap>`.
    // The `BTreeMap` is used because we want an ordered map, to better facilitate iteration. The
    // `Arc<Mutex<...>>` is used because there are now multiple async tasks accessing the: one main
    // task which handles communication over the protocol, and one additional task per iterator
    // protocol. `Arc<Mutex<...>>` is the simplest way to synchronize concurrent access between
    // these racing tasks.
    let store = &Arc::new(Mutex::new(BTreeMap::<String, Vec<u8>>::new()));
    // [END diff_4]

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
                        // [START diff_5]
                        .send(&mut write_item(&mut store.clone().lock().unwrap(), attempt))
                        // [END diff_5]
                        .context("error sending reply")?;
                    println!("WriteItem response sent");
                }
                // [START diff_6]
                StoreRequest::Iterate { starting_at, iterator, responder } => {
                    println!("Iterate request received");

                    // The `iterate` handler does a quick check to see that the request is valid,
                    // then spins up a separate worker task to serve the newly minted `Iterator`
                    // protocol instance, allowing this call to return immediately and continue the
                    // request stream with other work.
                    responder
                        .send(&mut iterate(store.clone(), starting_at, iterator.into_stream()?))
                        .context("error sending reply")?;
                    println!("Iterate response sent");
                } //
                  // [END diff_6]
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

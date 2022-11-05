// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    config::Config,
    fuchsia_component::client::connect_to_protocol,
    std::{thread, time},
};

// [START diff_1]
use {
    fidl::endpoints::create_proxy,
    fidl_examples_keyvaluestore_additerator::{Item, IteratorMarker, StoreMarker},
    futures::join,
};
// [END diff_1]

#[fuchsia::main]
async fn main() -> Result<(), Error> {
    println!("Started");

    // Load the structured config values passed to this component at startup.
    let config = Config::take_from_startup_handle();

    // Use the Component Framework runtime to connect to the newly spun up server component. We wrap
    // our retained client end in a proxy object that lets us asynchronously send `Store` requests
    // across the channel.
    let store = connect_to_protocol::<StoreMarker>()?;
    println!("Outgoing connection enabled");

    // This client's structured config has one parameter, a vector of strings. Each string is the
    // path to a resource file whose filename is a key and whose contents are a value. We iterate
    // over them and try to write each key-value pair to the remote store.
    for key in config.write_items.into_iter() {
        let path = format!("/pkg/data/{}.txt", key);
        let value = std::fs::read_to_string(path.clone())
            .with_context(|| format!("Failed to load {path}"))?;
        match store.write_item(&mut Item { key: key, value: value.into_bytes() }).await? {
            Ok(_) => println!("WriteItem Success"),
            Err(err) => println!("WriteItem Error: {}", err.into_primitive()),
        }
    }

    // [START diff_2]
    if !config.iterate_from.is_empty() {
        // This helper creates a channel, and returns two protocol ends: the `client_end` is already
        // conveniently bound to the correct FIDL protocol, `Iterator`, while the `server_end` is
        // unbound and ready to be sent over the wire.
        let (iterator, server_end) = create_proxy::<IteratorMarker>()?;

        // There is no need to wait for the iterator to connect before sending the first `Get()`
        // request - since we already hold the `client_end` of the connection, we can start queuing
        // requests on it immediately.
        let connect_to_iterator = store.iterate(Some(config.iterate_from.as_str()), server_end);
        let first_get = iterator.get();

        // Wait until both the connection and the first request resolve - an error in either case
        // triggers an immediate resolution of the combined future.
        let (connection, first_page) = join!(connect_to_iterator, first_get);

        // Handle any connection error. If this has occurred, it is impossible for the first `Get()`
        // call to have resolved successfully, so check this error first.
        if let Err(err) = connection.context("Could not connect to Iterator")? {
            println!("Iterator Connection Error: {}", err.into_primitive());
        } else {
            println!("Iterator Connection Success");

            // Consecutively repeat the `Get()` request if the previous response was not empty.
            let mut entries = first_page.context("Could not get page from Iterator")?;
            while !&entries.is_empty() {
                for entry in entries.iter() {
                    println!("Iterator Entry: {}", entry);
                }
                entries = iterator.get().await.context("Could not get page from Iterator")?;
            }
        }
    }
    // [END diff_2]

    // TODO(fxbug.dev/76579): We need to sleep here to make sure all logs get drained. Once the
    // referenced bug has been resolved, we can remove the sleep.
    thread::sleep(time::Duration::from_secs(2));
    Ok(())
}

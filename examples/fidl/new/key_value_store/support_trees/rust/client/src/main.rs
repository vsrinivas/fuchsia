// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    config::Config,
    // [START diff_1]
    fidl_examples_keyvaluestore_supporttrees::{Item, NestedStore, StoreMarker, Value},
    // [END diff_1]
    fuchsia_component::client::connect_to_protocol,
    std::{thread, time},
};

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
        let res = store
            // [START diff_2]
            .write_item(&mut Item {
                key: key.clone(),
                value: Some(Box::new(Value::Bytes(value.into_bytes()))),
            })
            // [END diff_2]
            .await;
        match res? {
            Ok(_) => println!("WriteItem Success at key: {}", key),
            Err(err) => println!("WriteItem Error: {}", err.into_primitive()),
        }
    }

    // [START diff_3]
    // Add nested entries to the key-value store as well. The entries are strings, where the first
    // line is the key of the entry, and each subsequent entry should be a pointer to a text file
    // containing the string value. The name of that text file (without the `.txt` suffix) will
    // serve as the entries key.
    for spec in config.write_nested.into_iter() {
        let mut items = vec![];
        let mut nested_store = NestedStore::EMPTY;
        let mut lines = spec.split("\n");
        let key = lines.next().unwrap();

        // For each entry, make a new entry in the `NestedStore` being built.
        for entry in lines {
            let path = format!("/pkg/data/{}.txt", entry);
            let contents = std::fs::read_to_string(path.clone())
                .with_context(|| format!("Failed to load {path}"))?;
            items.push(Some(Box::new(Item {
                key: entry.to_string(),
                value: Some(Box::new(Value::Bytes(contents.into()))),
            })));
        }
        nested_store.items = Some(items);

        // Send the `NestedStore`, represented as a vector of values.
        let res = store
            .write_item(&mut Item {
                key: key.to_string(),
                value: Some(Box::new(Value::Store(nested_store))),
            })
            .await;
        match res? {
            Ok(_) => println!("WriteItem Success at key: {}", key),
            Err(err) => println!("WriteItem Error: {}", err.into_primitive()),
        }
    }

    // Each entry in this list is a null value in the store.
    for key in config.write_null.into_iter() {
        match store.write_item(&mut Item { key: key.to_string(), value: None }).await? {
            Ok(_) => println!("WriteItem Success at key: {}", key),
            Err(err) => println!("WriteItem Error: {}", err.into_primitive()),
        }
    }
    // [END diff_3]

    // TODO(fxbug.dev/76579): We need to sleep here to make sure all logs get drained. Once the
    // referenced bug has been resolved, we can remove the sleep.
    thread::sleep(time::Duration::from_secs(2));
    Ok(())
}

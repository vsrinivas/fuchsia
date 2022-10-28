// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    config::Config,
    // [START diff_1]
    fidl_examples_keyvaluestore_usegenericvalues::{
        Item, StoreMarker, StoreProxy, StoreWriteItemRequest, Value, WriteOptions,
    },
    // [END diff_1]
    fuchsia_component::client::connect_to_protocol,
    std::{thread, time},
};

// [START diff_2]
// A helper function to sequentially write a single item to the key-value store and print a log when
// successful.
async fn write_next_item(
    store: &StoreProxy,
    key: &str,
    value: Value,
    options: WriteOptions,
) -> Result<(), Error> {
    // Create an empty request payload using `::EMPTY`.
    let mut req = StoreWriteItemRequest::EMPTY;
    req.options = Some(options);

    // Fill in the `Item` we will be attempting to write.
    println!("WriteItem request sent: key: {}, value: {:?}", &key, &value);
    req.attempt = Some(Item { key: key.to_string(), value: value });

    // Send and async `WriteItem` request to the server.
    let res = store.write_item(req).await.context("Error sending request");
    match res? {
        Ok(value) => println!("WriteItem response received: {:?}", &value),
        Err(err) => println!("WriteItem Error: {}", err.into_primitive()),
    }
    Ok(())
}
// [END diff_2]

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

    // [START diff_3]
    // All of our requests will have the same bitflags set. Pull these settings from the config.
    let mut options = WriteOptions::empty();
    options.set(WriteOptions::OVERWRITE, config.set_overwrite_option);
    options.set(WriteOptions::CONCAT, config.set_concat_option);

    // The structured config provides one input for most data types that can be stored in the data
    // store. Iterate through those inputs in the order we see them in the FIDL file.
    //
    // Note that FIDL unions are rendered as enums in Rust; for example, the `Value` union has now
    // become a `Value` Rust enum, with each member taking exactly one argument.
    for value in config.write_bytes.into_iter() {
        write_next_item(&store, "bytes", Value::Bytes(value.into()), options).await?;
    }
    for value in config.write_strings.into_iter() {
        write_next_item(&store, "string", Value::String(value), options).await?;
    }
    for value in config.write_uint64s.into_iter() {
        write_next_item(&store, "uint64", Value::Uint64(value), options).await?;
    }
    for value in config.write_int64s.into_iter() {
        write_next_item(&store, "int64", Value::Int64(value), options).await?;
    }
    for value in config.write_uint128s.into_iter() {
        write_next_item(&store, "uint128", Value::Uint128([0, value]), options).await?;
    }
    // [END diff_3]

    // TODO(fxbug.dev/76579): We need to sleep here to make sure all logs get drained. Once the
    // referenced bug has been resolved, we can remove the sleep.
    thread::sleep(time::Duration::from_secs(2));
    Ok(())
}

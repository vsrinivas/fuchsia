// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    config::Config,
    fidl_examples_keyvaluestore_baseline::{Item, StoreMarker},
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
        match store.write_item(&mut Item { key: key, value: value.into_bytes() }).await? {
            Ok(_) => println!("WriteItem Success"),
            Err(err) => println!("WriteItem Error: {}", err.into_primitive()),
        }
    }

    // TODO(fxbug.dev/76579): We need to sleep here to make sure all logs get drained. Once the
    // referenced bug has been resolved, we can remove the sleep.
    thread::sleep(time::Duration::from_secs(2));
    Ok(())
}

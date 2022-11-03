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
    fidl::encoding::decode_persistent,
    fidl_examples_keyvaluestore_supportexports::{Exportable, Item, StoreMarker},
    fuchsia_zircon::Vmo,
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
    // If the `max_export_size` is 0, no export is possible, so just ignore this block. This check
    // isn't strictly necessary, but does avoid extra work down the line.
    if config.max_export_size > 0 {
        // Create a 100Kb VMO to store the resulting export. In a real implementation, we would
        // likely receive the VMO representing the to-be-written file from file system like vfs of
        // fxfs.
        let vmo = Vmo::create(config.max_export_size)?;

        // Send the VMO to the server, to be populated with the current state of the key-value
        // store.
        match store.export(vmo).await? {
            Err(err) => {
                println!("Export Error: {}", err.into_primitive());
            }
            Ok(output) => {
                println!("Export Success");

                // Read the exported data (encoded in byte form as persistent FIDL) from the
                // returned VMO. In a real implementation, instead of reading the VMO, we would
                // merely forward it to some other storage-handling process. Doing this using a VMO,
                // rather than FIDL IPC, would save us frivolous reads and writes at each hop.
                let content_size = output.get_content_size().unwrap();
                let mut encoded_bytes = vec![0; content_size as usize];
                output.read(&mut encoded_bytes, 0)?;

                // Decode the persistent FIDL that was just read from the file.
                let exportable = decode_persistent::<Exportable>(&encoded_bytes).unwrap();
                let items = exportable.items.expect("must always be set");

                // Log some information about the exported data.
                println!("Printing {} exported entries, which are:", items.len());
                for item in items.iter() {
                    println!("  * {}", item.key);
                }
            }
        };
    }
    // [END diff_2]

    // TODO(fxbug.dev/76579): We need to sleep here to make sure all logs get drained. Once the
    // referenced bug has been resolved, we can remove the sleep.
    thread::sleep(time::Duration::from_secs(2));
    Ok(())
}

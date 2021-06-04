// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{self, Error},
    tracing,
};

#[fuchsia::test]
async fn my_component_test_rust_test() -> Result<(), Error> {
    // Connect to the component(s) under test using the Realm protocol, e.g.
    // ```
    // use fuchsia_component::client as fclient;
    //
    // let realm_proxy = fclient::realm()?;
    // let (_component_proxy, component_server_end) = create_proxy::<DirectoryMarker>()?;
    // realm_proxy
    //     .bind_child(
    //         &mut fsys::ChildRef { name: "hello-world".to_string(), collection: None },
    //         component_server_end,
    //     )
    //     .await?
    // ```


    // Use the ArchiveReader to access inspect data, e.g.
    // ```
    // use diagnostics_reader::{ArchiveReader, Inspect};
    //
    // let reader = ArchiveReader::new().add_selector("hello-world:root");
    // let results = reader.snapshot::<Inspect>().await?;
    // ```


    // Add test conditions here, e.g.
    // ```
    // let expected_string = test_function();
    // ```

    tracing::debug!("Initialized.");

    // Assert conditions here, e.g.
    // ```
    // assert_eq!(expected_string, "Hello World!");
    // ```

    Ok(())
}

// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{self, Error};
use diagnostics_reader::{ArchiveReader, Inspect};
use fuchsia_inspect::assert_data_tree;

async fn assert_inspect_config(selector: &str) {
    let inspector = ArchiveReader::new()
        .add_selector(selector)
        .with_minimum_schema_count(1)
        .snapshot::<Inspect>()
        .await
        .unwrap()
        .into_iter()
        .next()
        .and_then(|result| result.payload)
        .unwrap();

    assert_eq!(inspector.children.len(), 1, "selector must return exactly one child");

    assert_data_tree!(inspector, root: {
        config: {
            greeting: "World"
        }
    })
}

#[fuchsia::test]
async fn inspect_cpp() -> Result<(), Error> {
    assert_inspect_config("config_cpp:root").await;
    Ok(())
}

#[fuchsia::test]
async fn inspect_rust() -> Result<(), Error> {
    assert_inspect_config("config_rust:root").await;
    Ok(())
}

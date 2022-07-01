// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{self, Error};
use diagnostics_reader::{ArchiveReader, Inspect};
use fuchsia_component_test::{Capability, ChildOptions, RealmBuilder, Ref, Route};
use fuchsia_inspect::assert_data_tree;

async fn assert_inspect_config(selector: &str, expected_greeting: &'static str) {
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
            greeting: expected_greeting
        }
    })
}

async fn run_test(url: &str, name: &str, replace_config_value: bool) {
    let builder = RealmBuilder::new().await.unwrap();
    let config_component = builder.add_child(name, url, ChildOptions::new().eager()).await.unwrap();

    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                .from(Ref::parent())
                .to(&config_component),
        )
        .await
        .unwrap();

    let expected_greeting = if replace_config_value {
        // [START config_replace]
        builder.set_config_value_string(&config_component, "greeting", "Fuchsia").await.unwrap();
        // [END config_replace]
        "Fuchsia"
    } else {
        "World"
    };

    let _instance = builder.build().await.unwrap();
    let selector = format!("*/{}:root", name);
    assert_inspect_config(&selector, expected_greeting).await;
}

#[fuchsia::test]
async fn inspect_rust() -> Result<(), Error> {
    run_test("#meta/config_example.cm", "config_example", false).await;
    Ok(())
}

#[fuchsia::test]
async fn inspect_rust_replace() -> Result<(), Error> {
    run_test("#meta/config_example.cm", "config_example_replace", true).await;
    Ok(())
}

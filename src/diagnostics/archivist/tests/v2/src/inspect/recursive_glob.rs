// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{constants::*, test_topology};
use diagnostics_hierarchy::{assert_data_tree, testing::AnyProperty};
use diagnostics_reader::{ArchiveReader, Inspect};
use fidl_fuchsia_diagnostics::ArchiveAccessorMarker;
use std::{collections::HashSet, iter::FromIterator};

#[fuchsia::test]
async fn read_components_recursive_glob() {
    let mut builder = test_topology::create(test_topology::Options::default())
        .await
        .expect("create base topology");
    test_topology::add_component(&mut builder, "child_a", COMPONENT_WITH_CHILDREN_URL)
        .await
        .expect("add child a");
    test_topology::add_component(&mut builder, "child_b", COMPONENT_WITH_CHILDREN_URL)
        .await
        .expect("add child b");
    let instance = builder.build().create().await.expect("create instance");

    // Only inspect from descendants of child_a should be reported
    let expected_monikers = HashSet::from_iter(vec![
        "child_a/stub_inspect_1".to_string(),
        "child_a/stub_inspect_2".to_string(),
    ]);

    let accessor =
        instance.root.connect_to_protocol_at_exposed_dir::<ArchiveAccessorMarker>().unwrap();
    let data_vec = ArchiveReader::new()
        .add_selector("child_a/**:root")
        .with_archive(accessor)
        .retry_if_empty(true)
        .with_minimum_schema_count(expected_monikers.len())
        .snapshot::<Inspect>()
        .await
        .expect("got inspect data");

    assert_eq!(data_vec.len(), expected_monikers.len());
    let mut found_monikers = HashSet::new();
    for data in data_vec {
        assert_data_tree!(data.payload.as_ref().unwrap(), root: {
            "fuchsia.inspect.Health": {
                status: "OK",
                start_timestamp_nanos: AnyProperty,
            }
        });
        found_monikers.replace(data.moniker);
    }
    assert_eq!(expected_monikers, found_monikers);
}

#[fuchsia::test]
async fn read_components_subtree_with_recursive_glob() {
    let mut builder = test_topology::create(test_topology::Options::default())
        .await
        .expect("create base topology");
    test_topology::add_component(&mut builder, "child_a", COMPONENT_WITH_CHILDREN_URL)
        .await
        .expect("add child a");
    test_topology::add_component(&mut builder, "child_b", COMPONENT_WITH_CHILDREN_URL)
        .await
        .expect("add child b");
    let instance = builder.build().create().await.expect("create instance");

    // Only inspect from test_app_a, and descendants of test_app_a should be reported
    let expected_monikers = HashSet::from_iter(vec![
        "child_a".to_string(),
        "child_a/stub_inspect_1".to_string(),
        "child_a/stub_inspect_2".to_string(),
    ]);

    let accessor =
        instance.root.connect_to_protocol_at_exposed_dir::<ArchiveAccessorMarker>().unwrap();
    let data_vec = ArchiveReader::new()
        .add_selector("child_a/**:root")
        .add_selector("child_a:root")
        .with_archive(accessor)
        .retry_if_empty(true)
        .with_minimum_schema_count(expected_monikers.len())
        .snapshot::<Inspect>()
        .await
        .expect("got inspect data");

    assert_eq!(data_vec.len(), expected_monikers.len());
    let mut found_monikers = HashSet::new();
    for data in data_vec {
        assert_data_tree!(data.payload.as_ref().unwrap(), root: {
            "fuchsia.inspect.Health": {
                status: "OK",
                start_timestamp_nanos: AnyProperty,
            }
        });
        found_monikers.replace(data.moniker);
    }
    assert_eq!(expected_monikers, found_monikers);
}

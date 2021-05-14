// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use diagnostics_hierarchy::{assert_data_tree, testing::AnyProperty};
use diagnostics_reader::{ArchiveReader, Inspect, Logs, Severity};
use fidl_fuchsia_diagnostics::ArchiveAccessorMarker;
use futures::StreamExt;
use std::{collections::HashSet, iter::FromIterator};

mod test_topology;

#[fuchsia::test]
async fn read_v2_components_inspect() {
    let mut builder = test_topology::create(test_topology::Options::default())
        .await
        .expect("create base topology");
    test_topology::add_test_component(&mut builder, "child").await.expect("add child");

    let instance = builder.build().create().await.expect("create instance");

    let accessor =
        instance.root.connect_to_protocol_at_exposed_dir::<ArchiveAccessorMarker>().unwrap();
    let data = ArchiveReader::new()
        .with_archive(accessor)
        .add_selector("child:root")
        .retry_if_empty(true)
        .with_minimum_schema_count(1)
        .snapshot::<Inspect>()
        .await
        .expect("got inspect data");

    assert_data_tree!(data[0].payload.as_ref().unwrap(), root: {
        "fuchsia.inspect.Health": {
            status: "OK",
            start_timestamp_nanos: AnyProperty,
        }
    });
}

#[fuchsia::test]
async fn read_v2_components_single_selector() {
    let mut builder = test_topology::create(test_topology::Options::default())
        .await
        .expect("create base topology");
    test_topology::add_test_component(&mut builder, "child_a").await.expect("add child a");
    test_topology::add_test_component(&mut builder, "child_b").await.expect("add child b");
    let instance = builder.build().create().await.expect("create instance");

    let accessor =
        instance.root.connect_to_protocol_at_exposed_dir::<ArchiveAccessorMarker>().unwrap();
    let data = ArchiveReader::new()
        .with_archive(accessor)
        .add_selector("child_a:root")
        .snapshot::<Inspect>()
        .await
        .expect("got inspect data");

    // Only inspect from child_a should be reported
    assert_eq!(data.len(), 1);
    assert_data_tree!(data[0].payload.as_ref().unwrap(), root: {
        "fuchsia.inspect.Health": {
            status: "OK",
            start_timestamp_nanos: AnyProperty,
        }
    });
    assert_eq!(data[0].moniker, "child_a");
}

#[fuchsia::test]
async fn read_v2_components_recursive_glob() {
    let mut builder = test_topology::create(test_topology::Options::default())
        .await
        .expect("create base topology");
    test_topology::add_test_component_with_children(&mut builder, "child_a")
        .await
        .expect("add child a");
    test_topology::add_test_component_with_children(&mut builder, "child_b")
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
async fn read_v2_components_subtree_with_recursive_glob() {
    let mut builder = test_topology::create(test_topology::Options::default())
        .await
        .expect("create base topology");
    test_topology::add_test_component_with_children(&mut builder, "child_a")
        .await
        .expect("add child a");
    test_topology::add_test_component_with_children(&mut builder, "child_b")
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

// This test verifies that Archivist knows about logging from this component.
#[fuchsia::test]
async fn log_attribution() {
    let mut builder = test_topology::create(test_topology::Options::default())
        .await
        .expect("create base topology");
    test_topology::add_test_component(&mut builder, "child").await.expect("add child");

    let instance = builder.build().create().await.expect("create instance");

    let accessor =
        instance.root.connect_to_protocol_at_exposed_dir::<ArchiveAccessorMarker>().unwrap();
    let mut result = ArchiveReader::new()
        .with_archive(accessor)
        .snapshot_then_subscribe::<Logs>()
        .expect("snapshot then subscribe");

    for log_str in &["This is a syslog message", "This is another syslog message"] {
        let log_record = result.next().await.expect("received log").expect("log is not an error");

        assert_eq!(log_record.moniker, "child");
        assert_eq!(log_record.metadata.component_url, test_topology::TEST_COMPONENT_URL);
        assert_eq!(log_record.metadata.severity, Severity::Info);
        assert_data_tree!(log_record.payload.unwrap(), root: contains {
            message: {
              value: log_str.to_string(),
            }
        });
    }
}

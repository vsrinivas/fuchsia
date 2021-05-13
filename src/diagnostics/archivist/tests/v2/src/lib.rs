// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    diagnostics_data::Logs,
    diagnostics_hierarchy::{assert_data_tree, testing::AnyProperty},
    diagnostics_reader::{ArchiveReader, Inspect},
    diagnostics_testing::Severity,
    fuchsia_component_test::ScopedInstance,
    futures::stream::StreamExt,
    log::info,
    std::{collections::HashSet, iter::FromIterator},
};

const DRIVER_COMPONENT: &str =
    "fuchsia-pkg://fuchsia.com/archivist-integration-tests-v2#meta/driver.cm";
const TEST_COMPONENT: &str =
    "fuchsia-pkg://fuchsia.com/archivist-integration-tests-v2#meta/stub_inspect_component.cm";
const TEST_COMPONENT_WITH_CHILDREN: &str =
    "fuchsia-pkg://fuchsia.com/archivist-integration-tests-v2#meta/component_with_children.cm";

#[fuchsia::test]
async fn read_v2_components_inspect() {
    let test_app = ScopedInstance::new("coll".to_string(), TEST_COMPONENT.to_string())
        .await
        .expect("Failed to create dynamic component");

    let data = ArchiveReader::new()
        .add_selector(format!("driver/coll\\:{}:root", test_app.child_name()))
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
    let _test_app_a = ScopedInstance::new_with_name(
        "child_a".to_string(),
        "coll".to_string(),
        TEST_COMPONENT.to_string(),
    )
    .await
    .expect("Failed to create dynamic component");
    let _test_app_b = ScopedInstance::new_with_name(
        "child_b".to_string(),
        "coll".to_string(),
        TEST_COMPONENT.to_string(),
    )
    .await
    .expect("Failed to create dynamic component");

    let data = ArchiveReader::new()
        .add_selector("driver/coll\\:child_a:root")
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
    assert_eq!(data[0].moniker, "driver/coll\\:child_a");
}

#[fuchsia::test]
async fn read_v2_components_recursive_glob() {
    let test_app_a =
        ScopedInstance::new("coll".to_string(), TEST_COMPONENT_WITH_CHILDREN.to_string())
            .await
            .expect("Failed to create dynamic component");
    let _test_app_b =
        ScopedInstance::new("coll".to_string(), TEST_COMPONENT_WITH_CHILDREN.to_string())
            .await
            .expect("Failed to create dynamic component");

    // Only inspect from descendants of test_app_a should be reported
    let expected_monikers = HashSet::from_iter(vec![
        format!("driver/coll\\:{}/stub_inspect_1", test_app_a.child_name()),
        format!("driver/coll\\:{}/stub_inspect_2", test_app_a.child_name()),
    ]);

    let data_vec = ArchiveReader::new()
        .add_selector(format!("driver/coll\\:{}/**:root", test_app_a.child_name()))
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
    let test_app_a =
        ScopedInstance::new("coll".to_string(), TEST_COMPONENT_WITH_CHILDREN.to_string())
            .await
            .expect("Failed to create dynamic component");
    let _test_app_b =
        ScopedInstance::new("coll".to_string(), TEST_COMPONENT_WITH_CHILDREN.to_string())
            .await
            .expect("Failed to create dynamic component");

    // Only inspect from test_app_a, and descendants of test_app_a should be reported
    let expected_monikers = HashSet::from_iter(vec![
        format!("driver/coll\\:{}", test_app_a.child_name()),
        format!("driver/coll\\:{}/stub_inspect_1", test_app_a.child_name()),
        format!("driver/coll\\:{}/stub_inspect_2", test_app_a.child_name()),
    ]);

    let data_vec = ArchiveReader::new()
        .add_selector(format!("driver/coll\\:{}/**:root", test_app_a.child_name()))
        .add_selector(format!("driver/coll\\:{}:root", test_app_a.child_name()))
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
    let mut result =
        ArchiveReader::new().snapshot_then_subscribe::<Logs>().expect("snapshot then subscribe");

    for log_str in &["This is a syslog message", "This is another syslog message"] {
        info!("{}", log_str);
        let log_record = result.next().await.expect("received log").expect("log is not an error");

        assert_eq!(log_record.moniker, "driver");
        assert_eq!(log_record.metadata.component_url, DRIVER_COMPONENT);
        assert_eq!(log_record.metadata.severity, Severity::Info);
        assert_data_tree!(log_record.payload.unwrap(), root:{"message": contains {
            "value".to_string() => log_str.to_string(),
        }});
        assert_eq!(log_record.metadata.tags.unwrap()[0], "log_attribution");
    }
}

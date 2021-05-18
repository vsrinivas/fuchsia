// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::constants::*;
use anyhow::Error;
use diagnostics_hierarchy::{assert_data_tree, testing::AnyProperty};
use diagnostics_reader::{ArchiveReader, Data, Inspect, Logs, Severity};
use fidl_fuchsia_diagnostics::ArchiveAccessorMarker;
use fidl_fuchsia_io as fio;
use fuchsia_async as fasync;
use fuchsia_zircon::DurationNum;
use futures::StreamExt;
use std::{collections::HashSet, iter::FromIterator, path::Path};

mod constants;
mod reader;
mod test_topology;

#[fuchsia::test]
async fn read_v2_components_inspect() {
    let mut builder = test_topology::create(test_topology::Options::default())
        .await
        .expect("create base topology");
    test_topology::add_component(&mut builder, "child", STUB_INSPECT_COMPONENT_URL)
        .await
        .expect("add child");

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
    test_topology::add_component(&mut builder, "child_a", STUB_INSPECT_COMPONENT_URL)
        .await
        .expect("add child a");
    test_topology::add_component(&mut builder, "child_b", STUB_INSPECT_COMPONENT_URL)
        .await
        .expect("add child b");
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
async fn read_v2_components_subtree_with_recursive_glob() {
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

// This test verifies that Archivist knows about logging from this component.
#[fuchsia::test]
async fn log_attribution() {
    let mut builder = test_topology::create(test_topology::Options::default())
        .await
        .expect("create base topology");
    test_topology::add_component(&mut builder, "child", STUB_INSPECT_COMPONENT_URL)
        .await
        .expect("add child");

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
        assert_eq!(log_record.metadata.component_url, STUB_INSPECT_COMPONENT_URL);
        assert_eq!(log_record.metadata.severity, Severity::Info);
        assert_data_tree!(log_record.payload.unwrap(), root: contains {
            message: {
              value: log_str.to_string(),
            }
        });
    }
}

#[fuchsia::test]
async fn accessor_truncation_test() {
    let mut builder = test_topology::create(test_topology::Options::default())
        .await
        .expect("create base topology");
    test_topology::add_component(&mut builder, "child_a", IQUERY_TEST_COMPONENT_URL)
        .await
        .expect("add child a");
    test_topology::add_component(&mut builder, "child_b", IQUERY_TEST_COMPONENT_URL)
        .await
        .expect("add child b");

    let instance = builder.build().create().await.expect("create instance");
    let accessor =
        instance.root.connect_to_protocol_at_exposed_dir::<ArchiveAccessorMarker>().unwrap();
    let data = ArchiveReader::new()
        .with_archive(accessor)
        .with_aggregated_result_bytes_limit(1)
        .add_selector("child_a:root")
        .snapshot::<Inspect>()
        .await
        .expect("got inspect data");

    assert_eq!(data.len(), 3);

    assert_eq!(count_dropped_schemas_per_moniker(&data, "child_a"), 3);

    let accessor =
        instance.root.connect_to_protocol_at_exposed_dir::<ArchiveAccessorMarker>().unwrap();
    let data = ArchiveReader::new()
        .with_archive(accessor)
        .with_aggregated_result_bytes_limit(3000)
        .add_selector("child_a:root")
        .snapshot::<Inspect>()
        .await
        .expect("got inspect data");

    assert_eq!(data.len(), 3);

    assert_eq!(count_dropped_schemas_per_moniker(&data, "child_a"), 2);

    let accessor =
        instance.root.connect_to_protocol_at_exposed_dir::<ArchiveAccessorMarker>().unwrap();
    let data = ArchiveReader::new()
        .with_archive(accessor)
        .with_aggregated_result_bytes_limit(10000)
        .add_selector("child_a:root")
        .snapshot::<Inspect>()
        .await
        .expect("got inspect data");

    assert_eq!(data.len(), 3);

    assert_eq!(count_dropped_schemas_per_moniker(&data, "child_a"), 1);

    let accessor =
        instance.root.connect_to_protocol_at_exposed_dir::<ArchiveAccessorMarker>().unwrap();
    let data = ArchiveReader::new()
        .with_archive(accessor)
        .with_aggregated_result_bytes_limit(16000)
        .add_selector("child_a:root")
        .snapshot::<Inspect>()
        .await
        .expect("got inspect data");

    assert_eq!(data.len(), 3);

    assert_eq!(count_dropped_schemas_per_moniker(&data, "child_a"), 0);

    let accessor =
        instance.root.connect_to_protocol_at_exposed_dir::<ArchiveAccessorMarker>().unwrap();
    let data = ArchiveReader::new()
        .with_archive(accessor)
        .with_aggregated_result_bytes_limit(1)
        .add_selector("child_b:root")
        .add_selector("child_a:root")
        .snapshot::<Inspect>()
        .await
        .expect("got inspect data");

    assert_eq!(data.len(), 6);
    assert_eq!(count_dropped_schemas_per_moniker(&data, "child_a"), 3);
    assert_eq!(count_dropped_schemas_per_moniker(&data, "child_b"), 3);

    let accessor =
        instance.root.connect_to_protocol_at_exposed_dir::<ArchiveAccessorMarker>().unwrap();
    let data = ArchiveReader::new()
        .with_archive(accessor)
        .with_aggregated_result_bytes_limit(10000)
        .add_selector("child_b:root")
        .add_selector("child_a:root")
        .snapshot::<Inspect>()
        .await
        .expect("got inspect data");

    assert_eq!(data.len(), 6);
    assert_eq!(count_dropped_schemas_per_moniker(&data, "child_a"), 2);
    assert_eq!(count_dropped_schemas_per_moniker(&data, "child_b"), 2);
}

fn count_dropped_schemas_per_moniker(data: &Vec<Data<Inspect>>, moniker: &str) -> i64 {
    let mut dropped_schema_count = 0;
    for data_entry in data {
        if data_entry.moniker != moniker {
            continue;
        }
        if let Some(errors) = &data_entry.metadata.errors {
            assert!(
                data_entry.payload.is_none(),
                "shouldn't have payloads when errors are present."
            );
            assert_eq!(
                errors[0].message, "Schema failed to fit component budget.",
                "Accessor truncation test should only produce one error."
            );
            dropped_schema_count += 1;
        }
    }
    dropped_schema_count
}

#[fuchsia::test]
async fn out_can_be_read() {
    let builder = test_topology::create(test_topology::Options::default())
        .await
        .expect("create base topology");
    let instance = builder.build().create().await.expect("create instance");
    // Start the archivist by connecting to the accessor.
    let _accessor =
        instance.root.connect_to_protocol_at_exposed_dir::<ArchiveAccessorMarker>().unwrap();

    let hub_out_path_str = format!(
        "/hub/children/fuchsia_component_test_collection:{}/children/test/children/archivist/exec/out/",
        instance.root.child_name()
    );
    let hub_out_path = Path::new(&hub_out_path_str);
    verify_out(&hub_out_path).await.expect("verify - first");

    // Verify again to ensure we can continue to read.
    verify_out(&hub_out_path).await.expect("verify - second");
}

async fn verify_out(hub_out_path: &Path) -> Result<(), Error> {
    // TODO(fxbug.dev/61350): We need to retry since the out dir might not be serving yet. A much
    // cleaner way to do this would be to snapshot_then_subscribe for lifecycle events in the
    // archivist but unfortunately snapshot is the only supported mode at the moment.
    loop {
        if let Ok(mut result) = read_entries(hub_out_path).await {
            let mut expected =
                vec!["archive".to_string(), "diagnostics".to_string(), "svc".to_string()];
            result.sort();
            expected.sort();
            if result == expected {
                break;
            }
        };
        fasync::Timer::new(fasync::Time::after(100.millis())).await;
    }

    let result = read_entries(&hub_out_path.join("diagnostics")).await.unwrap();
    let expected = vec!["fuchsia.inspect.Tree".to_string()];
    assert_eq!(expected, result,);

    let diagnostics_entries = read_entries(&hub_out_path.join("archive")).await.unwrap();
    assert_eq!(diagnostics_entries.len(), 1);

    Ok(())
}

async fn read_entries(path: &Path) -> Result<Vec<String>, Error> {
    let directory =
        io_util::open_directory_in_namespace(path.to_str().unwrap(), fio::OPEN_RIGHT_READABLE)?;
    Ok(files_async::readdir(&directory).await?.into_iter().map(|e| e.name).collect::<Vec<_>>())
}

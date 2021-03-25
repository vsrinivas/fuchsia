// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    diagnostics_data::Logs,
    diagnostics_hierarchy::assert_data_tree,
    diagnostics_reader::{ArchiveReader, Inspect},
    diagnostics_testing::Severity,
    fuchsia_component::client::ScopedInstance,
    fuchsia_inspect::testing::{assert_inspect_tree, AnyProperty},
    futures::stream::StreamExt,
    log::info,
};

const DRIVER_COMPONENT: &str =
    "fuchsia-pkg://fuchsia.com/archivist-integration-tests-v2#meta/driver.cm";
const TEST_COMPONENT: &str =
    "fuchsia-pkg://fuchsia.com/archivist-integration-tests-v2#meta/stub_inspect_component.cm";

#[fuchsia::test]
async fn read_v2_components_inspect() {
    let _test_app = ScopedInstance::new("coll".to_string(), TEST_COMPONENT.to_string())
        .await
        .expect("Failed to create dynamic component");

    let data = ArchiveReader::new()
        .add_selector("driver/coll\\:auto-*:root")
        .snapshot::<Inspect>()
        .await
        .expect("got inspect data");

    assert_inspect_tree!(data[0].payload.as_ref().unwrap(), root: {
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
    assert_inspect_tree!(data[0].payload.as_ref().unwrap(), root: {
        "fuchsia.inspect.Health": {
            status: "OK",
            start_timestamp_nanos: AnyProperty,
        }
    });
    assert_eq!(data[0].moniker, "driver/coll\\:child_a");
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
        assert_data_tree!(log_record.payload.unwrap(), root: contains {
            "message".to_string() => log_str.to_string(),
            "tag".to_string() => "log_attribution".to_string(),
        });
    }
}

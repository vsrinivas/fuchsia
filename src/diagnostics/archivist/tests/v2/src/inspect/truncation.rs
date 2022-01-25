// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{constants::*, test_topology};
use diagnostics_reader::{ArchiveReader, Data, Inspect};
use fidl_fuchsia_diagnostics::ArchiveAccessorMarker;

#[fuchsia::test]
async fn accessor_truncation_test() {
    let (builder, test_realm) = test_topology::create(test_topology::Options::default())
        .await
        .expect("create base topology");
    test_topology::add_eager_child(&test_realm, "child_a", IQUERY_TEST_COMPONENT_URL)
        .await
        .expect("add child a");
    test_topology::add_eager_child(&test_realm, "child_b", IQUERY_TEST_COMPONENT_URL)
        .await
        .expect("add child b");

    let instance = builder.build().await.expect("create instance");
    let accessor =
        instance.root.connect_to_protocol_at_exposed_dir::<ArchiveAccessorMarker>().unwrap();
    let mut reader = ArchiveReader::new();
    reader.with_archive(accessor);
    let data = reader
        .with_aggregated_result_bytes_limit(1)
        .add_selector("child_a:root")
        .with_minimum_schema_count(3)
        .snapshot::<Inspect>()
        .await
        .expect("got inspect data");

    assert_eq!(data.len(), 3);

    assert_eq!(count_dropped_schemas_per_moniker(&data, "child_a"), 3);

    let data = reader
        .with_aggregated_result_bytes_limit(3000)
        .add_selector("child_a:root")
        .with_minimum_schema_count(3)
        .snapshot::<Inspect>()
        .await
        .expect("got inspect data");

    assert_eq!(data.len(), 3);

    assert_eq!(count_dropped_schemas_per_moniker(&data, "child_a"), 2);

    let data = reader
        .with_aggregated_result_bytes_limit(5000)
        .add_selector("child_a:root")
        .with_minimum_schema_count(3)
        .snapshot::<Inspect>()
        .await
        .expect("got inspect data");

    assert_eq!(data.len(), 3);

    assert_eq!(count_dropped_schemas_per_moniker(&data, "child_a"), 1);

    let data = reader
        .with_aggregated_result_bytes_limit(16000)
        .add_selector("child_a:root")
        .with_minimum_schema_count(3)
        .snapshot::<Inspect>()
        .await
        .expect("got inspect data");

    assert_eq!(data.len(), 3);

    assert_eq!(count_dropped_schemas_per_moniker(&data, "child_a"), 0);

    let data = reader
        .with_aggregated_result_bytes_limit(1)
        .add_selector("child_b:root")
        .add_selector("child_a:root")
        .with_minimum_schema_count(6)
        .snapshot::<Inspect>()
        .await
        .expect("got inspect data");

    assert_eq!(data.len(), 6);
    assert_eq!(count_dropped_schemas_per_moniker(&data, "child_a"), 3);
    assert_eq!(count_dropped_schemas_per_moniker(&data, "child_b"), 3);

    let data = reader
        .with_aggregated_result_bytes_limit(5000)
        .add_selector("child_b:root")
        .add_selector("child_a:root")
        .with_minimum_schema_count(6)
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

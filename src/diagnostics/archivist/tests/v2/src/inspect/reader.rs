// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{constants::*, test_topology};
use anyhow::Error;
use diagnostics_reader::{assert_data_tree, AnyProperty, ArchiveReader, Inspect};
use difference::assert_diff;
use fidl_fuchsia_diagnostics::{ArchiveAccessorMarker, ArchiveAccessorProxy};
use fuchsia_component_test::RealmInstance;
use lazy_static::lazy_static;
use serde_json;

const MONIKER_KEY: &str = "moniker";
const METADATA_KEY: &str = "metadata";
const TIMESTAMP_KEY: &str = "timestamp";

const TEST_ARCHIVIST_MONIKER: &str = "archivist";

lazy_static! {
    static ref EMPTY_RESULT_GOLDEN: &'static str =
        include_str!("../../test_data/empty_result_golden.json");
    static ref UNIFIED_SINGLE_VALUE_GOLDEN: &'static str =
        include_str!("../../test_data/unified_reader_single_value_golden.json");
    static ref UNIFIED_ALL_GOLDEN: &'static str =
        include_str!("../../test_data/unified_reader_all_golden.json");
    static ref UNIFIED_FULL_FILTER_GOLDEN: &'static str =
        include_str!("../../test_data/unified_reader_full_filter_golden.json");
    static ref FEEDBACK_SINGLE_VALUE_GOLDEN: &'static str =
        include_str!("../../test_data/feedback_reader_single_value_golden.json");
    static ref FEEDBACK_ALL_GOLDEN: &'static str =
        include_str!("../../test_data/feedback_reader_all_golden.json");
    static ref FEEDBACK_NONOVERLAPPING_SELECTORS_GOLDEN: &'static str =
        include_str!("../../test_data/feedback_reader_nonoverlapping_selectors_golden.json");
    static ref MEMORY_MONITOR_V2_MONIKER_GOLDEN: &'static str =
        include_str!("../../test_data/memory_monitor_v2_moniker_golden.json");
    static ref MEMORY_MONITOR_LEGACY_MONIKER_GOLDEN: &'static str =
        include_str!("../../test_data/memory_monitor_legacy_moniker_golden.json");
}

#[fuchsia::test]
async fn read_components_inspect() {
    let mut builder = test_topology::create(test_topology::Options::default())
        .await
        .expect("create base topology");
    test_topology::add_eager_component(&mut builder, "child", STUB_INSPECT_COMPONENT_URL)
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
async fn read_components_single_selector() {
    let mut builder = test_topology::create(test_topology::Options::default())
        .await
        .expect("create base topology");
    test_topology::add_eager_component(&mut builder, "child_a", STUB_INSPECT_COMPONENT_URL)
        .await
        .expect("add child a");
    test_topology::add_eager_component(&mut builder, "child_b", STUB_INSPECT_COMPONENT_URL)
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
async fn unified_reader() -> Result<(), Error> {
    let mut builder = test_topology::create(test_topology::Options::default())
        .await
        .expect("create base topology");
    test_topology::add_eager_component(&mut builder, "test_component", IQUERY_TEST_COMPONENT_URL)
        .await
        .expect("add child a");

    let instance = builder.build().create().await.expect("create instance");

    // First, retrieve all of the information in our realm to make sure that everything
    // we expect is present.
    let accessor =
        instance.root.connect_to_protocol_at_exposed_dir::<ArchiveAccessorMarker>().unwrap();
    retrieve_and_validate_results(accessor, Vec::new(), &UNIFIED_ALL_GOLDEN, 4).await;

    // Then verify that from the expected data, we can retrieve one specific value.
    let accessor =
        instance.root.connect_to_protocol_at_exposed_dir::<ArchiveAccessorMarker>().unwrap();
    retrieve_and_validate_results(
        accessor,
        vec!["test_component:*:lazy-*"],
        &UNIFIED_SINGLE_VALUE_GOLDEN,
        1,
    )
    .await;

    // Then verify that subtree selection retrieves all trees under and including root.
    let accessor =
        instance.root.connect_to_protocol_at_exposed_dir::<ArchiveAccessorMarker>().unwrap();
    retrieve_and_validate_results(accessor, vec!["test_component:root"], &UNIFIED_ALL_GOLDEN, 3)
        .await;

    // Then verify that a selector with a correct moniker, but no resolved nodes
    // produces an error schema.
    let accessor =
        instance.root.connect_to_protocol_at_exposed_dir::<ArchiveAccessorMarker>().unwrap();
    retrieve_and_validate_results(
        accessor,
        vec!["test_component:root/non-existent-node:bloop"],
        &UNIFIED_FULL_FILTER_GOLDEN,
        3,
    )
    .await;

    Ok(())
}

#[fuchsia::test]
async fn memory_monitor_moniker_rewrite() -> Result<(), Error> {
    let mut builder = test_topology::create(test_topology::Options {
        archivist_url: ARCHIVIST_WITH_LEGACY_METRICS,
    })
    .await
    .expect("create base topology");
    test_topology::add_eager_component(
        &mut builder,
        "core/memory_monitor",
        IQUERY_TEST_COMPONENT_URL,
    )
    .await
    .expect("add child a");
    let instance = builder.build().create().await.expect("create instance");

    // Verify that fetching "core/memory_monitor" from ArchiveAccessor produces results with
    // the correct v2 moniker.
    let accessor =
        instance.root.connect_to_protocol_at_exposed_dir::<ArchiveAccessorMarker>().unwrap();
    retrieve_and_validate_results(
        accessor,
        vec!["core/memory_monitor:*:lazy-*"],
        &MEMORY_MONITOR_V2_MONIKER_GOLDEN,
        1,
    )
    .await;

    // Verify that fetching "memory_monitor.cmx" from ArchiveAccessor does not fetch anything.
    let accessor =
        instance.root.connect_to_protocol_at_exposed_dir::<ArchiveAccessorMarker>().unwrap();
    retrieve_and_validate_results(
        accessor,
        vec!["memory_monitor.cmx:*:lazy-*"],
        &EMPTY_RESULT_GOLDEN,
        0,
    )
    .await;

    // Verify that fetching "core/memory_monitor" from Legacy produces results with the
    // correct v2 moniker.
    let accessor = connect_to_legacy_accessor(&instance);
    retrieve_and_validate_results(
        accessor,
        vec!["core/memory_monitor:*:lazy-*"],
        &MEMORY_MONITOR_V2_MONIKER_GOLDEN,
        1,
    )
    .await;

    // Verify that fetching "memory_monitor.cmx" from Legacy produces results with the
    // correct (rewritten) v1 moniker.
    let accessor = connect_to_legacy_accessor(&instance);
    retrieve_and_validate_results(
        accessor,
        vec!["memory_monitor.cmx:*:lazy-*"],
        &MEMORY_MONITOR_LEGACY_MONIKER_GOLDEN,
        1,
    )
    .await;

    Ok(())
}

#[fuchsia::test]
async fn feedback_canonical_reader_test() -> Result<(), Error> {
    let mut builder = test_topology::create(test_topology::Options {
        archivist_url: ARCHIVIST_WITH_FEEDBACK_FILTERING,
    })
    .await
    .expect("create base topology");
    test_topology::add_eager_component(&mut builder, "test_component", IQUERY_TEST_COMPONENT_URL)
        .await
        .expect("add child a");

    let instance = builder.build().create().await.expect("create instance");

    // First, retrieve all of the information in our realm to make sure that everything
    // we expect is present.
    let accessor = connect_to_feedback_accessor(&instance);
    retrieve_and_validate_results(accessor, Vec::new(), &FEEDBACK_ALL_GOLDEN, 3).await;

    // Then verify that from the expected data, we can retrieve one specific value.
    let accessor = connect_to_feedback_accessor(&instance);
    retrieve_and_validate_results(
        accessor,
        vec!["test_component:*:lazy-*"],
        &FEEDBACK_SINGLE_VALUE_GOLDEN,
        3,
    )
    .await;

    // Then verify that subtree selection retrieves all trees under and including root.
    let accessor = connect_to_feedback_accessor(&instance);
    retrieve_and_validate_results(accessor, vec!["test_component:root"], &FEEDBACK_ALL_GOLDEN, 3)
        .await;

    // Then verify that client selectors dont override the static selectors provided
    // to the archivist.
    let accessor = connect_to_feedback_accessor(&instance);
    retrieve_and_validate_results(
        accessor,
        vec![r#"test_component:root:array\:0x15"#],
        &FEEDBACK_NONOVERLAPPING_SELECTORS_GOLDEN,
        3,
    )
    .await;

    assert!(feedback_pipeline_is_filtered(instance, 3).await);

    Ok(())
}

#[fuchsia::test]
async fn feedback_disabled_pipeline() -> Result<(), Error> {
    let mut builder = test_topology::create(test_topology::Options {
        archivist_url: ARCHIVIST_WITH_FEEDBACK_FILTERING_DISABLED,
    })
    .await
    .expect("create base topology");
    test_topology::add_eager_component(&mut builder, "test_component", IQUERY_TEST_COMPONENT_URL)
        .await
        .expect("add child a");

    let instance = builder.build().create().await.expect("create instance");
    assert!(!feedback_pipeline_is_filtered(instance, 3).await);

    Ok(())
}

#[fuchsia::test]
async fn feedback_pipeline_missing_selectors() -> Result<(), Error> {
    let mut builder = test_topology::create(test_topology::Options::default())
        .await
        .expect("create base topology");
    test_topology::add_eager_component(&mut builder, "test_component", IQUERY_TEST_COMPONENT_URL)
        .await
        .expect("add child a");

    let instance = builder.build().create().await.expect("create instance");

    assert!(!feedback_pipeline_is_filtered(instance, 3).await);

    Ok(())
}

fn connect_to_feedback_accessor(instance: &RealmInstance) -> ArchiveAccessorProxy {
    instance
        .root
        .connect_to_named_protocol_at_exposed_dir::<ArchiveAccessorMarker>(
            "fuchsia.diagnostics.FeedbackArchiveAccessor",
        )
        .unwrap()
}

fn connect_to_legacy_accessor(instance: &RealmInstance) -> ArchiveAccessorProxy {
    instance
        .root
        .connect_to_named_protocol_at_exposed_dir::<ArchiveAccessorMarker>(
            "fuchsia.diagnostics.LegacyMetricsArchiveAccessor",
        )
        .unwrap()
}

// Loop indefinitely snapshotting the archive until we get the expected number of
// hierarchies, and then validate that the ordered json represetionation of these hierarchies
// matches the golden file.
//
// If the expected number of hierarchies is 0, set a 10-second timeout to ensure nothing is
// received.
async fn retrieve_and_validate_results(
    accessor: ArchiveAccessorProxy,
    custom_selectors: Vec<&str>,
    golden: &str,
    expected_results_count: usize,
) {
    let mut reader = ArchiveReader::new();
    reader.with_archive(accessor).add_selectors(custom_selectors.into_iter());
    if expected_results_count == 0 {
        reader.with_timeout(fuchsia_zircon::Duration::from_seconds(10));
    } else {
        reader.with_minimum_schema_count(expected_results_count);
    }
    let results = reader.snapshot_raw::<Inspect>().await.expect("got result");

    // Convert the json struct into a "pretty" string rather than converting the
    // golden file into a json struct because deserializing the golden file into a
    // struct causes serde_json to convert the u64s into exponential form which
    // causes loss of precision.
    let mut pretty_results = serde_json::to_string_pretty(&process_results_for_comparison(results))
        .expect("should be able to format the the results as valid json.");
    let mut expected_string = golden.to_string();
    // Remove whitespace from both strings because text editors will do things like
    // requiring json files end in a newline, while the result string is unbounded by
    // newlines. Also, we don't want this test to fail if the only change is to json
    // format within the reader.
    pretty_results.retain(|c| !c.is_whitespace());
    expected_string.retain(|c| !c.is_whitespace());
    assert_diff!(&expected_string, &pretty_results, "\n", 0);
}

fn process_results_for_comparison(results: serde_json::Value) -> serde_json::Value {
    let mut string_result_array = results
        .as_array()
        .expect("result json is an array of objs.")
        .into_iter()
        .filter_map(|val| {
            let mut val = val.clone();
            // Filter out the results coming from the archivist, and zero out timestamps
            // that we cant golden test.
            val.as_object_mut().map_or(
                None,
                |obj: &mut serde_json::Map<String, serde_json::Value>| match obj.get(MONIKER_KEY) {
                    Some(serde_json::Value::String(moniker_str)) => {
                        if moniker_str != TEST_ARCHIVIST_MONIKER {
                            let metadata_obj =
                                obj.get_mut(METADATA_KEY).unwrap().as_object_mut().unwrap();
                            metadata_obj.insert(TIMESTAMP_KEY.to_string(), serde_json::json!(0));
                            Some(
                                serde_json::to_string(&serde_json::to_value(obj).unwrap())
                                    .expect("All entries in the array are valid."),
                            )
                        } else {
                            None
                        }
                    }
                    _ => None,
                },
            )
        })
        .collect::<Vec<String>>();

    string_result_array.sort();
    let sorted_results_json_string = format!("[{}]", string_result_array.join(","));
    serde_json::from_str(&sorted_results_json_string).unwrap()
}

async fn feedback_pipeline_is_filtered(
    instance: RealmInstance,
    expected_results_count: usize,
) -> bool {
    let feedback_archive_accessor = instance
        .root
        .connect_to_named_protocol_at_exposed_dir::<ArchiveAccessorMarker>(
            "fuchsia.diagnostics.FeedbackArchiveAccessor",
        )
        .unwrap();

    let feedback_results = ArchiveReader::new()
        .with_archive(feedback_archive_accessor)
        .with_minimum_schema_count(expected_results_count)
        .snapshot_raw::<Inspect>()
        .await
        .expect("got result");

    let all_archive_accessor =
        instance.root.connect_to_protocol_at_exposed_dir::<ArchiveAccessorMarker>().unwrap();

    let all_results = ArchiveReader::new()
        .with_archive(all_archive_accessor)
        .with_minimum_schema_count(expected_results_count)
        .snapshot_raw::<Inspect>()
        .await
        .expect("got result");

    process_results_for_comparison(feedback_results) != process_results_for_comparison(all_results)
}

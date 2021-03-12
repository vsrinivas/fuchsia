// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use diagnostics_reader::{ArchiveReader, Data, Inspect};
use fidl_fuchsia_sys::ComponentControllerEvent;
use fuchsia_component::client::{launch, launcher, App};
use futures::StreamExt;

async fn start_and_await_test_component(component_url: String, arguments: Vec<String>) -> App {
    let test_component =
        launch(&launcher().unwrap(), component_url.to_string(), Some(arguments)).unwrap();

    let mut component_stream = test_component.controller().take_event_stream();

    match component_stream
        .next()
        .await
        .expect("component event stream has ended before termination event")
        .unwrap()
    {
        ComponentControllerEvent::OnDirectoryReady {} => {}
        ComponentControllerEvent::OnTerminated { return_code, termination_reason } => {
            panic!(
                "Component terminated unexpectedly. Code: {}. Reason: {:?}",
                return_code, termination_reason
            );
        }
    }

    test_component
}

async fn setup() -> (App, App) {
    let package = "fuchsia-pkg://fuchsia.com/archivist-integration-tests#meta/";
    let test_component_manifest = "test_component.cmx";
    let alternative_test_component_manifest = "alternate_test_component.cmx";

    let test_component_url = format!("{}{}", package, test_component_manifest);

    let alternative_test_component_url =
        format!("{}{}", package, alternative_test_component_manifest);

    let arguments = vec!["--rows=5".to_string(), "--columns=3".to_string()];

    let test_component_1 =
        start_and_await_test_component(test_component_url, arguments.clone()).await;

    let test_component_2 =
        start_and_await_test_component(alternative_test_component_url, arguments).await;

    (test_component_1, test_component_2)
}

/// Runs the Lapis Sampler and a test component that can have its inspect properties
/// manipulated by the test via fidl, and uses cobalt mock and log querier to
/// verify that the sampler observers changes as expected, and logs them to
/// cobalt as expected.
#[fuchsia::test]
async fn accessor_truncation_test() {
    let (_test_component, _alternative_test_component) = setup().await;

    let data = ArchiveReader::new()
        .with_aggregated_result_bytes_limit(1)
        .add_selector("test_component.cmx:root")
        .snapshot::<Inspect>()
        .await
        .expect("got inspect data");

    assert_eq!(data.len(), 3);

    assert_eq!(count_dropped_schemas_per_moniker(&data, "test_component.cmx"), 3);

    let data = ArchiveReader::new()
        .with_aggregated_result_bytes_limit(3000)
        .add_selector("test_component.cmx:root")
        .snapshot::<Inspect>()
        .await
        .expect("got inspect data");

    assert_eq!(data.len(), 3);

    assert_eq!(count_dropped_schemas_per_moniker(&data, "test_component.cmx"), 2);

    let data = ArchiveReader::new()
        .with_aggregated_result_bytes_limit(10000)
        .add_selector("test_component.cmx:root")
        .snapshot::<Inspect>()
        .await
        .expect("got inspect data");

    assert_eq!(data.len(), 3);

    assert_eq!(count_dropped_schemas_per_moniker(&data, "test_component.cmx"), 1);

    let data = ArchiveReader::new()
        .with_aggregated_result_bytes_limit(16000)
        .add_selector("test_component.cmx:root")
        .snapshot::<Inspect>()
        .await
        .expect("got inspect data");

    assert_eq!(data.len(), 3);

    assert_eq!(count_dropped_schemas_per_moniker(&data, "test_component.cmx"), 0);

    let data = ArchiveReader::new()
        .with_aggregated_result_bytes_limit(1)
        .add_selector("alternate_test_component.cmx:root")
        .add_selector("test_component.cmx:root")
        .snapshot::<Inspect>()
        .await
        .expect("got inspect data");

    assert_eq!(data.len(), 6);
    assert_eq!(count_dropped_schemas_per_moniker(&data, "test_component.cmx"), 3);
    assert_eq!(count_dropped_schemas_per_moniker(&data, "alternate_test_component.cmx"), 3);

    let data = ArchiveReader::new()
        .with_aggregated_result_bytes_limit(10000)
        .add_selector("alternate_test_component.cmx:root")
        .add_selector("test_component.cmx:root")
        .snapshot::<Inspect>()
        .await
        .expect("got inspect data");

    assert_eq!(data.len(), 6);
    assert_eq!(count_dropped_schemas_per_moniker(&data, "test_component.cmx"), 2);
    assert_eq!(count_dropped_schemas_per_moniker(&data, "alternate_test_component.cmx"), 2);
}

fn count_dropped_schemas_per_moniker(data: &Vec<Data<Inspect>>, moniker: &str) -> i64 {
    let mut dropped_schema_count = 0;
    for data_entry in data {
        if data_entry.moniker == moniker {
            if let Some(errors) = &data_entry.metadata.errors {
                if data_entry.payload.is_some() {
                    panic!("Shouldn't have payloads when errors are present.");
                }

                if errors[0].message == "Schema failed to fit component budget." {
                    dropped_schema_count += 1;
                } else {
                    panic!("Accessor truncation test should only produce one error.")
                }
            }
        }
    }
    dropped_schema_count
}

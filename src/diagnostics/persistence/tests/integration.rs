// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use diagnostics_data::LifecycleType;
use diagnostics_reader::{ArchiveReader, Lifecycle};
use fidl_fuchsia_diagnostics_persist::{DataPersistenceMarker, PersistResult};
use fidl_fuchsia_sys::ComponentControllerEvent;
use fuchsia_async::futures::StreamExt;
use fuchsia_component::client::{launcher, App, AppBuilder};
use fuchsia_zircon::{Duration, Time};
use inspect_fetcher::InspectFetcher;
use log::*;
use serde_json::{self, Value};
use std::fs::{create_dir, File};
use std::io::Read;
use std::{thread, time};

// When to give up on polling for a change and fail the test. DNS if less than 120 sec.
static GIVE_UP_POLLING_SECS: i64 = 120;

static METADATA_KEY: &str = "metadata";
static TIMESTAMP_KEY: &str = "timestamp";
static PERSISTENCE_INJECTED_PATH: &str = "/cache";
static INJECTED_STORAGE_DIR: &str = "/tmp/injected_storage";

const PERSISTENCE_URL: &str = "fuchsia-pkg://fuchsia.com/diagnostics-persistence-integration-tests#meta/diagnostics-persistence.cmx";
const INSPECT_PROVIDER_URL: &str =
    "fuchsia-pkg://fuchsia.com/diagnostics-persistence-integration-tests#meta/test_component.cmx";

const TEST_PERSISTENCE_SERVICE_NAME: &str =
    "fuchsia.diagnostics.persist.DataPersistence-test-service";

// The capability name for the Inspect reader
const INSPECT_SERVICE_PATH: &str = "/svc/fuchsia.diagnostics.FeedbackArchiveAccessor";

enum Published {
    Nothing,
    Int(i32),
    SizeError,
}

#[derive(PartialEq)]
enum FileState {
    None,
    NoInt,
    Int(i32),
    TooBig,
}

struct FileChange<'a> {
    old: FileState,
    after: Option<Time>,
    new: FileState,
    file_name: &'a str,
}

/// Runs the persistence persistor and a test component that can have its inspect properties
/// manipulated by the test via fidl. Then just trigger persistence on diagnostics-persistence.
#[fuchsia::test]
async fn diagnostics_persistence_integration() {
    setup();
    let persisted_data_path = "/tmp/injected_storage/current/test-service/test-component-metric";
    let persisted_too_big_path =
        "/tmp/injected_storage/current/test-service/test-component-too-big";

    let mut diagnostics_persistence_app = persistence().await;

    let diagnostics_persistence_service = diagnostics_persistence_app
        .connect_to_named_service::<DataPersistenceMarker>(TEST_PERSISTENCE_SERVICE_NAME)
        .unwrap();

    let mut example_app = inspect_source(Some(19i32)).await;
    // Contacting the wrong service, or giving the wrong name to the right service, should return
    // an error and not persist anything.
    assert_eq!(
        diagnostics_persistence_service.persist("wrong-component-metric").await.unwrap(),
        PersistResult::BadName
    );
    expect_file_change(FileChange {
        old: FileState::None,
        new: FileState::None,
        file_name: persisted_data_path,
        after: None,
    });
    // Verify that the backoff mechanism works by observing the time between first and second
    // persistence. The duration should be the same as "repeat_seconds" in test_config.persist.
    let backoff_time = Time::get_monotonic() + Duration::from_seconds(1);
    diagnostics_persistence_service.persist("test-component-metric").await.unwrap();
    expect_file_change(FileChange {
        old: FileState::None,
        new: FileState::Int(19),
        file_name: persisted_data_path,
        after: None,
    });

    // Valid data can be replaced by missing data.
    example_app.kill().unwrap();
    let mut example_app = inspect_source(None).await;
    diagnostics_persistence_service.persist("test-component-metric").await.unwrap();
    expect_file_change(FileChange {
        old: FileState::Int(19),
        new: FileState::NoInt,
        file_name: persisted_data_path,
        after: Some(backoff_time),
    });
    example_app.kill().unwrap();

    // Missing data can be replaced by new data.
    let _example_app = inspect_source(Some(42i32)).await;
    diagnostics_persistence_service.persist("test-component-metric").await.unwrap();
    expect_file_change(FileChange {
        old: FileState::NoInt,
        new: FileState::Int(42),
        file_name: persisted_data_path,
        after: None,
    });

    // The persisted data shouldn't be published until Diagnostics Persistence is killed and restarted.
    verify_diagnostics_persistence_publication(Published::Nothing).await;
    diagnostics_persistence_app.kill().unwrap();
    let mut diagnostics_persistence_app = persistence().await;
    verify_diagnostics_persistence_publication(Published::Int(42)).await;
    expect_file_change(FileChange {
        old: FileState::None,
        new: FileState::None,
        file_name: persisted_data_path,
        after: None,
    });

    // After another restart, no data should be published.
    diagnostics_persistence_app.kill().unwrap();
    let mut diagnostics_persistence_app = persistence().await;
    verify_diagnostics_persistence_publication(Published::Nothing).await;
    // The "too-big" tag should save a short error string instead of the data.
    let diagnostics_persistence_service = diagnostics_persistence_app
        .connect_to_named_service::<DataPersistenceMarker>(TEST_PERSISTENCE_SERVICE_NAME)
        .unwrap();
    diagnostics_persistence_service.persist("test-component-too-big").await.unwrap();
    expect_file_change(FileChange {
        old: FileState::None,
        new: FileState::TooBig,
        file_name: persisted_too_big_path,
        after: None,
    });

    diagnostics_persistence_app.kill().unwrap();
    let mut diagnostics_persistence_app = persistence().await;
    verify_diagnostics_persistence_publication(Published::SizeError).await;
    diagnostics_persistence_app.kill().unwrap();
}

// Starts and returns an Diagnostics Persistence app. Assumes:
//  - The INJECTED_STORAGE_DIR has been created.
//  - No other Diagnostics Persistence app is running in that directory.
async fn persistence() -> App {
    AppBuilder::new(PERSISTENCE_URL)
        .add_dir_to_namespace(
            PERSISTENCE_INJECTED_PATH.into(),
            File::open(INJECTED_STORAGE_DIR).unwrap(),
        )
        .unwrap()
        .spawn(&launcher().unwrap())
        .unwrap()
}

fn setup() {
    // For development it may be convenient to set this to 5. For production, slow virtual devices
    // may cause test flakes even with surprisingly long timeouts.
    assert!(GIVE_UP_POLLING_SECS >= 120);

    create_dir(INJECTED_STORAGE_DIR).unwrap();
}

async fn inspect_source(data: Option<i32>) -> App {
    let mut arguments =
        vec!["--rows=5".to_string(), "--columns=3".to_string(), "--only-new".to_string()];

    if let Some(number) = data {
        arguments.push(format!("--extra-number={}", number));
    }

    let example_app =
        AppBuilder::new(INSPECT_PROVIDER_URL).args(arguments).spawn(&launcher().unwrap()).unwrap();

    let mut component_stream = example_app.controller().take_event_stream();

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

    wait_for_inspect_ready().await;

    example_app
}

async fn wait_for_inspect_ready() {
    loop {
        let results = ArchiveReader::new()
            .snapshot::<Lifecycle>()
            .await
            .unwrap()
            .into_iter()
            .filter(|e| {
                e.moniker.starts_with("test_component")
                    && e.metadata.lifecycle_event_type == LifecycleType::DiagnosticsReady
            })
            .collect::<Vec<_>>();

        if !results.is_empty() {
            break;
        }
        thread::sleep(time::Duration::from_secs(1));
    }
}

// Verifies that the file changes from the old state to the new state within the specified time
// window. This involves polling; the granularity for retries is 100 msec.
fn expect_file_change(rules: FileChange<'_>) {
    // Returns None if the file isn't there. If the file is there but contains "[]" then it tries
    // again (this avoids a file-writing race condition). Any other string will be returned.
    fn file_contents(persisted_data_path: &str) -> Option<String> {
        loop {
            let file = File::open(persisted_data_path);
            if file.is_err() {
                return None;
            }
            let mut contents = String::new();
            file.unwrap().read_to_string(&mut contents).unwrap();
            // Just because the file was present doesn't mean we persisted. Creation and
            // writing isn't atomic, and we sometimes flake as we race between the creation and write.
            if contents == "[]" {
                warn!("No data has been written to the persisted file (yet)");
                thread::sleep(time::Duration::from_millis(100));
                continue;
            }
            let parse_result: Result<Value, serde_json::Error> = serde_json::from_str(&contents);
            if let Err(_) = parse_result {
                warn!("Bad JSON data in the file. Partial writes happen sometimes. Retrying.");
                thread::sleep(time::Duration::from_millis(100));
                continue;
            }
            return Some(contents);
        }
    }

    fn expected_string(state: &FileState) -> Option<String> {
        match state {
            FileState::None => None,
            FileState::NoInt => Some(expected_stored_data(None)),
            FileState::Int(i) => Some(expected_stored_data(Some(*i))),
            FileState::TooBig => Some("\"Data too big: 61 > max length 10\"".to_string()),
        }
    }

    fn strings_match(left: &Option<String>, right: &Option<String>, context: &str) -> bool {
        match (left, right) {
            (None, None) => true,
            (Some(left), Some(right)) => json_strings_match(left, right, context),
            _ => false,
        }
    }

    let start_time = Time::get_monotonic();
    let old_string = expected_string(&rules.old);
    let new_string = expected_string(&rules.new);

    loop {
        assert!(start_time + Duration::from_seconds(GIVE_UP_POLLING_SECS) > Time::get_monotonic());
        let contents = file_contents(rules.file_name);
        if rules.old != rules.new && strings_match(&contents, &old_string, "old file (likely OK)") {
            thread::sleep(time::Duration::from_millis(100));
            continue;
        }
        if strings_match(&contents, &new_string, "new file check") {
            if let Some(after) = rules.after {
                assert!(Time::get_monotonic() > after);
            }
            return;
        }
        error!("File contents don't match old or new target.");
        error!("Old : {:?}", old_string);
        error!("New : {:?}", new_string);
        error!("File: {:?}", contents);
        assert!(false);
        break;
    }
}

fn json_strings_match(observed: &str, expected: &str, context: &str) -> bool {
    fn parse_json(string: &str, context1: &str, context2: &str) -> Value {
        let parse_result: Result<Value, serde_json::Error> = serde_json::from_str(&string);
        match parse_result {
            Ok(json) => json,
            Err(badness) => {
                error!("Error parsing json in {} {}: {}", context1, context2, badness);
                serde_json::json!(string)
            }
        }
    }
    let mut observed_json = parse_json(observed, "observed", context);
    let expected_json = parse_json(expected, "expected", context);

    // Remove health nodes if they exist.
    if let Some(v) = observed_json.as_array_mut() {
        for hierarchy in v.iter_mut() {
            if let Some(Some(root)) =
                hierarchy.pointer_mut("/payload/root").map(|r| r.as_object_mut())
            {
                root.remove("fuchsia.inspect.Health");
            }
        }
    }

    if observed_json != expected_json {
        warn!("Observed != expected in {}", context);
        warn!("Observed: {:?}", observed_json);
        warn!("Expected: {:?}", expected_json);
    }
    observed_json == expected_json
}

fn zero_timestamps(contents: &str) -> String {
    let result_json: Value = serde_json::from_str(contents).expect("parsing json failed.");
    let mut string_result_array = result_json
        .as_array()
        .expect("result json is an array of objs.")
        .into_iter()
        .filter_map(|val| {
            let mut val = val.clone();

            val.as_object_mut().map(|obj: &mut serde_json::Map<String, serde_json::Value>| {
                let metadata_obj = obj.get_mut(METADATA_KEY).unwrap().as_object_mut().unwrap();
                metadata_obj.insert(TIMESTAMP_KEY.to_string(), serde_json::json!(0));
                serde_json::to_string(&serde_json::to_value(obj).unwrap())
                    .expect("All entries in the array are valid.")
            })
        })
        .collect::<Vec<String>>();

    string_result_array.sort();

    format!("[{}]", string_result_array.join(","))
}

async fn verify_diagnostics_persistence_publication(published: Published) {
    let mut inspect_fetcher = InspectFetcher::create(
        INSPECT_SERVICE_PATH,
        vec!["INSPECT:diagnostics-persistence.cmx:root".to_string()],
    )
    .unwrap();
    loop {
        let published_inspect = inspect_fetcher.fetch().await.unwrap();
        if published_inspect != "[]" {
            assert!(json_strings_match(
                &zero_timestamps(&published_inspect),
                &expected_diagnostics_persistence_inspect(published),
                "persistence publication"
            ));
            break;
        }
        thread::sleep(time::Duration::from_millis(100));
    }
}

fn expected_stored_data(number: Option<i32>) -> String {
    let variant = match number {
        None => "".to_string(),
        Some(number) => format!("\"extra_number\": {},", number),
    };
    r#"
  {"test_component.cmx": { %VARIANT% "lazy-double":3.14}
  }
    "#
    .replace("%VARIANT%", &variant)
}

fn expected_diagnostics_persistence_inspect(published: Published) -> String {
    let variant = match published {
        Published::Nothing => "".to_string(),
        Published::SizeError => r#"
            "test-service": {
                "test-component-too-big": "Data too big: 61 > max length 10"
            }
            "#
        .to_string(),
        Published::Int(_) => {
            let number_text = match published {
                Published::Int(number) => format!("\"extra_number\": {},", number),
                _ => "".to_string(),
            };
            r#"
                "test-service": {
                    "test-component-metric": {
                        "test_component.cmx": {
                            %NUMBER_TEXT%
                            "lazy-double": 3.14
                        }
                    }
                }
                "#
            .replace("%NUMBER_TEXT%", &number_text)
        }
    };
    r#"[
  {
    "data_source": "Inspect",
    "metadata": {
      "component_url": "fuchsia-pkg://fuchsia.com/diagnostics-persistence-integration-tests#meta/diagnostics-persistence.cmx",
      "errors": null,
      "filename": "fuchsia.inspect.Tree",
      "timestamp": 0
    },
    "moniker": "diagnostics-persistence.cmx",
    "payload": {
      "root": {
        "persist":{%VARIANT%}
      }
    },
    "version": 1
  }
    ]"#
    .replace("%VARIANT%", &variant)
}

// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use diagnostics_reader::{ArchiveReader, Inspect};
use fidl_fuchsia_component::BinderMarker;
use fidl_fuchsia_diagnostics_persist::{
    DataPersistenceMarker, DataPersistenceProxy, PersistResult,
};
use fidl_fuchsia_samplertestcontroller::{SamplerTestControllerMarker, SamplerTestControllerProxy};
use fuchsia_component_test::RealmInstance;
use fuchsia_zircon::{Duration, Time};
use serde_json::{self, Value};
use std::fs::File;
use std::io::Read;
use std::mem::take;
use std::{thread, time};
use tracing::*;

mod mock_filesystems;
mod test_topology;

// When to give up on polling for a change and fail the test. DNS if less than 120 sec.
static GIVE_UP_POLLING_SECS: i64 = 120;

static METADATA_KEY: &str = "metadata";
static TIMESTAMP_METADATA_KEY: &str = "timestamp";

// Each persisted tag contains a "@timestamps" object with four timestamps that need to be zeroed.
static PAYLOAD_KEY: &str = "payload";
static ROOT_KEY: &str = "root";
static PERSIST_KEY: &str = "persist";
static TIMESTAMP_STRUCT_KEY: &str = "@timestamps";
static BEFORE_MONOTONIC_KEY: &str = "before_monotonic";
static AFTER_MONOTONIC_KEY: &str = "after_monotonic";
static PUBLISHED_TIME_KEY: &str = "published";
static TIMESTAMP_STRUCT_ENTRIES: [&str; 4] =
    ["before_utc", BEFORE_MONOTONIC_KEY, "after_utc", AFTER_MONOTONIC_KEY];
/// If the "single_counter" Inspect source is publishing Inspect data, the stringified JSON
/// version of that data should include this string. Waiting for it to appear avoids a race
/// condition.
static KEY_FROM_INSPECT_SOURCE: &str = "integer_1";

pub(crate) const TEST_PERSISTENCE_SERVICE_NAME: &str =
    "fuchsia.diagnostics.persist.DataPersistence-test-service";

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

struct TestRealm {
    instance: RealmInstance,
    persistence: DataPersistenceProxy,
    inspect: SamplerTestControllerProxy,
}

/// Runs Persistene and a test component that can have its inspect properties
/// manipulated by the test via fidl.
#[fuchsia::test]
async fn diagnostics_persistence_integration() {
    crate::mock_filesystems::setup_backing_directories();
    let persisted_data_path = "/tmp/cache/current/test-service/test-component-metric";
    let persisted_too_big_path = "/tmp/cache/current/test-service/test-component-too-big";

    // Verify that the backoff mechanism works by observing the time between first and second
    // persistence to verify that the change doesn't happen too soon.
    // backoff_time should be the same as "min_seconds_between_fetch" in
    // TEST_CONFIG_CONTENTS.
    let backoff_time = Time::get_monotonic() + Duration::from_seconds(1);

    // For development it may be convenient to set this to 5. For production, slow virtual devices
    // may cause test flakes even with surprisingly long timeouts.
    assert!(GIVE_UP_POLLING_SECS >= 120);

    let realm = TestRealm::create().await;
    realm.set_inspect(Some(19i64)).await;
    wait_for_inspect_source().await;

    assert_eq!(realm.request_persistence("wrong_component_metric").await, PersistResult::BadName);

    expect_file_change(FileChange {
        old: FileState::None,
        new: FileState::None,
        file_name: persisted_data_path,
        after: None,
    });

    assert_eq!(realm.request_persistence("test-component-metric").await, PersistResult::Queued);

    expect_file_change(FileChange {
        old: FileState::None,
        new: FileState::Int(19),
        file_name: persisted_data_path,
        after: None,
    });

    // Valid data can be replaced by missing data.
    realm.set_inspect(None).await;
    assert_eq!(realm.request_persistence("test-component-metric").await, PersistResult::Queued);
    expect_file_change(FileChange {
        old: FileState::Int(19),
        new: FileState::NoInt,
        file_name: persisted_data_path,
        after: Some(backoff_time),
    });

    // Missing data can be replaced by new data.
    realm.set_inspect(Some(42i64)).await;
    assert_eq!(realm.request_persistence("test-component-metric").await, PersistResult::Queued);
    expect_file_change(FileChange {
        old: FileState::NoInt,
        new: FileState::Int(42),
        file_name: persisted_data_path,
        after: None,
    });

    // The persisted data shouldn't be published until Diagnostics Persistence is killed and
    // restarted.
    verify_diagnostics_persistence_publication(Published::Nothing).await;

    let realm = realm.restart().await;
    verify_diagnostics_persistence_publication(Published::Int(42)).await;
    expect_file_change(FileChange {
        old: FileState::None,
        new: FileState::None,
        file_name: persisted_data_path,
        after: None,
    });

    // After another restart, no data should be published.
    let realm = realm.restart().await;
    verify_diagnostics_persistence_publication(Published::Nothing).await;

    // The "too-big" tag should save a short error string instead of the data.
    assert_eq!(realm.request_persistence("test-component-too-big").await, PersistResult::Queued);
    expect_file_change(FileChange {
        old: FileState::None,
        new: FileState::TooBig,
        file_name: persisted_too_big_path,
        after: None,
    });

    let _realm = realm.restart().await;
    verify_diagnostics_persistence_publication(Published::SizeError).await;
}

/// The Inspect source may not publish Inspect (via take_and_serve_directory_handle()) until
/// some time after the FIDL call that woke it up has returned. This function verifies that
/// the Inspect source is actually publishing data to avoid a race condition.
async fn wait_for_inspect_source() {
    let mut inspect_fetcher = ArchiveReader::new();
    inspect_fetcher.retry_if_empty(false);
    inspect_fetcher.add_selector("realm_builder*/single_counter:root");
    let start_time = Time::get_monotonic();

    loop {
        assert!(start_time + Duration::from_seconds(GIVE_UP_POLLING_SECS) > Time::get_monotonic());
        let published_inspect =
            inspect_fetcher.snapshot_raw::<Inspect>().await.unwrap().to_string();
        if published_inspect.contains(KEY_FROM_INSPECT_SOURCE) {
            return;
        }
        thread::sleep(time::Duration::from_millis(100));
    }
}

impl TestRealm {
    async fn create() -> TestRealm {
        let instance = test_topology::create().await.expect("initialized topology");
        // Start up the Persistence component during realm creation - as happens during startup
        // in a real system - so that it can publish the previous boot's stored data, if any.
        let _persistence_binder = instance
            .root
            .connect_to_named_protocol_at_exposed_dir::<BinderMarker>(
                "fuchsia.component.PersistenceBinder",
            )
            .unwrap();
        // `inspect` is the source of Inspect data that Persistence will read and persist.
        let inspect = instance
            .root
            .connect_to_protocol_at_exposed_dir::<SamplerTestControllerMarker>()
            .unwrap();
        // `persistence` is the connection to ask for new data to be read and persisted.
        let persistence = instance
            .root
            .connect_to_named_protocol_at_exposed_dir::<DataPersistenceMarker>(
                TEST_PERSISTENCE_SERVICE_NAME,
            )
            .unwrap();
        TestRealm { instance, persistence, inspect }
    }

    /// Set the `optional` value to a given number, or remove it from the Inspect tree.
    async fn set_inspect(&self, value: Option<i64>) {
        match value {
            Some(value) => {
                self.inspect.set_optional(value).await.expect("set_optional should work")
            }
            None => self.inspect.remove_optional().await.expect("remove_optional should work"),
        };
    }

    /// Ask for a tag's associated data to be persisted.
    async fn request_persistence(&self, tag: &str) -> PersistResult {
        self.persistence.persist(tag).await.unwrap()
    }

    /// Tear down the realm to make sure everything is gone before you restart it.
    /// Then create and return a new realm.
    async fn restart(self) -> TestRealm {
        self.instance.destroy().await.expect("destroy should work");
        TestRealm::create().await
    }
}

/// Given a mut map from a JSON object that's presumably sourced from Inspect, if it contains a
/// timestamp record entry, this function validates that "before" <= "after", then zeros them.
fn clean_and_test_timestamps(map: &mut serde_json::Map<String, Value>) {
    if let Some(Value::Object(map)) = map.get_mut(TIMESTAMP_STRUCT_KEY) {
        if let (Some(Value::Number(before)), Some(Value::Number(after))) =
            (map.get(BEFORE_MONOTONIC_KEY), map.get(AFTER_MONOTONIC_KEY))
        {
            assert!(before.as_u64() <= after.as_u64(), "Monotonic timestamps must increase");
        } else {
            assert!(false, "Timestamp map must contain before/after monotonic values");
        }
        for key in TIMESTAMP_STRUCT_ENTRIES.iter() {
            let key = key.to_string();
            if let Some(Value::Number(_)) = map.get_mut(&key) {
                map.insert(key, serde_json::json!(0));
            }
        }
    }

    *map = take(map)
        .into_iter()
        .map(|(key, value)| {
            (
                test_topology::REALM_NAME_PATTERN
                    .replace(&key, "realm-name")
                    .replace(r"realm_builder\:realm-name", "realm_builder"),
                value,
            )
        })
        .collect();
}

/// The number of bytes reported in the "too big" case may vary. It should be a 2-digit
/// number. Replace with underscores.
fn unbrittle_too_big_message(contents: Option<String>) -> Option<String> {
    match contents {
        None => None,
        Some(contents) => {
            let matcher = regex::Regex::new(r"Data too big: \d{2} > max length 10").unwrap();
            Some(matcher.replace(&contents, "Data too big: __ > max length 10").to_string())
        }
    }
}

// Verifies that the file changes from the old state to the new state within the specified time
// window. This involves polling; the granularity for retries is 100 msec.
#[track_caller]
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

    fn zero_file_timestamps(contents: Option<String>) -> Option<String> {
        match contents {
            None => None,
            Some(contents) => {
                let mut obj: Value = serde_json::from_str(&contents).expect("parsing json failed.");
                if let Value::Object(ref mut map) = obj {
                    clean_and_test_timestamps(map);
                }
                Some(obj.to_string())
            }
        }
    }

    fn expected_string(state: &FileState) -> Option<String> {
        match state {
            FileState::None => None,
            FileState::NoInt => Some(expected_stored_data(None)),
            FileState::Int(i) => Some(expected_stored_data(Some(*i))),
            FileState::TooBig => Some(expected_size_error()),
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
        let contents =
            unbrittle_too_big_message(zero_file_timestamps(file_contents(rules.file_name)));
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
        error!("Old : {:?}", old_string);
        error!("New : {:?}", new_string);
        error!("File: {:?}", contents);
        panic!("File contents don't match old or new target.");
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

fn zero_and_test_timestamps(contents: &str) -> String {
    fn for_all_entries<F>(map: &mut serde_json::Map<String, Value>, func: F)
    where
        F: Fn(&mut serde_json::Map<String, Value>),
    {
        for (_key, value) in map.iter_mut() {
            if let Value::Object(inner_map) = value {
                func(inner_map);
            }
        }
    }

    let result_json: Value = serde_json::from_str(contents).expect("parsing json failed.");
    let mut string_result_array = result_json
        .as_array()
        .expect("result json is an array of objs.")
        .into_iter()
        .filter_map(|val| {
            let mut val = val.clone();

            val.as_object_mut().map(|obj: &mut serde_json::Map<String, serde_json::Value>| {
                let metadata_obj = obj.get_mut(METADATA_KEY).unwrap().as_object_mut().unwrap();
                metadata_obj.insert(TIMESTAMP_METADATA_KEY.to_string(), serde_json::json!(0));
                let payload_obj = obj.get_mut(PAYLOAD_KEY).unwrap();
                if let Value::Object(map) = payload_obj {
                    if let Some(Value::Object(map)) = map.get_mut(ROOT_KEY) {
                        if map.contains_key(PUBLISHED_TIME_KEY) {
                            map.insert(PUBLISHED_TIME_KEY.to_string(), serde_json::json!(0));
                        }
                        if let Some(Value::Object(persist_contents)) = map.get_mut(PERSIST_KEY) {
                            for_all_entries(persist_contents, |service_contents| {
                                for_all_entries(service_contents, clean_and_test_timestamps);
                            });
                        }
                    }
                }
                serde_json::to_string(&serde_json::to_value(obj).unwrap())
                    .expect("All entries in the array are valid.")
            })
        })
        .collect::<Vec<String>>();

    string_result_array.sort();

    format!("[{}]", string_result_array.join(","))
}

fn collapse_realm_builder_strings(data: &str) -> String {
    let matcher = regex::Regex::new("realm([_-])builder.*?/persistence").unwrap();
    matcher.replace_all(data, "realm${1}builder/persistence").to_string()
}

/// Verify that the expected data is published by Persistence in its Inspect hierarchy.
async fn verify_diagnostics_persistence_publication(published: Published) {
    let mut inspect_fetcher = ArchiveReader::new();
    inspect_fetcher.retry_if_empty(false);
    inspect_fetcher.add_selector("realm_builder*/persistence:root");
    loop {
        let published_inspect =
            inspect_fetcher.snapshot_raw::<Inspect>().await.unwrap().to_string();
        if published_inspect.contains(PUBLISHED_TIME_KEY) {
            assert!(json_strings_match(
                &collapse_realm_builder_strings(
                    &unbrittle_too_big_message(Some(zero_and_test_timestamps(&published_inspect)),)
                        .unwrap()
                ),
                &expected_diagnostics_persistence_inspect(published),
                "persistence publication"
            ));
            break;
        }
        thread::sleep(time::Duration::from_millis(100));
    }
}

fn expected_stored_data(number: Option<i32>) -> String {
    const BASE_SIZE: usize = 86;
    let (persist_size, variant) = match number {
        None => (BASE_SIZE, "".to_string()),
        Some(number) => {
            let variant = format!("\"optional\": {},", number);
            (BASE_SIZE + variant.len() - 1, variant)
        }
    };
    r#"
  {"realm_builder/single_counter": { "samples" : { %VARIANT% "integer_1": 10 } },
   "@persist_size": %PERSIST_SIZE%,
   "@timestamps": {"before_utc":0, "after_utc":0, "before_monotonic":0, "after_monotonic":0}
  }
    "#
    .replace("%VARIANT%", &variant)
    .replace("%PERSIST_SIZE%", &persist_size.to_string())
}

fn expected_size_error() -> String {
    // unbrittle_too_big_message() will replace a 2-digit number after "big: " with __
    r#"{
        ":error": {
            "description": "Data too big: __ > max length 10"
        },
        "@timestamps": {
            "before_utc":0, "after_utc":0, "before_monotonic":0, "after_monotonic":0
        }
    }"#
    .to_string()
}

fn expected_diagnostics_persistence_inspect(published: Published) -> String {
    let variant = match published {
        Published::Nothing => "".to_string(),
        Published::SizeError => r#"
            "test-service": {
                "test-component-too-big": %SIZE_ERROR%
            }
            "#
        .replace("%SIZE_ERROR%", &expected_size_error()),
        Published::Int(number) => {
            let number_str = number.to_string();
            r#"
                "test-service": {
                    "test-component-metric": {
                        "@timestamps": {
                            "before_utc":0,
                            "after_utc":0,
                            "before_monotonic":0,
                            "after_monotonic":0
                        },
                        "@persist_size": 100,
                        "realm_builder/single_counter": {
                            "samples": {
                                "optional": %NUMBER%,
                                "integer_1": 10
                            }
                        }
                    }
                }
                "#
            .replace("%NUMBER%", &number_str)
        }
    };
    r#"[
  {
    "data_source": "Inspect",
    "metadata": {
      "component_url": "realm-builder/persistence",
      "filename": "fuchsia.inspect.Tree",
      "timestamp": 0
    },
    "moniker": "realm_builder/persistence",
    "payload": {
      "root": {
        "published":0,
        "persist":{%VARIANT%},
        "startup_delay_seconds": 1
      }
    },
    "version": 1
  }
    ]"#
    .replace("%VARIANT%", &variant)
}

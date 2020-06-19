// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_inspect_node_hierarchy::NodeHierarchy,
    fuchsia_zircon::Time,
    lazy_static::lazy_static,
    serde::{self, ser::SerializeSeq, Serialize, Serializer},
};

lazy_static! {
    static ref SCHEMA_VERSION: u64 = 1;
}

#[derive(Serialize, Debug, PartialEq, Eq)]
pub enum DataSource {
    Inspect,
    LifecycleEvent,
}

#[derive(Serialize, Debug, PartialEq, Eq)]
pub struct Error {
    pub message: String,
}

#[derive(Serialize, Debug, PartialEq, Eq)]
pub enum LifecycleEventType {
    Started,
    Existing,
    DiagnosticsReady,
}

#[derive(Serialize, Debug)]
#[serde(untagged)]
pub enum Metadata {
    Inspect(InspectMetadata),
    LifecycleEvent(LifecycleEventMetadata),
}

#[derive(Serialize, Debug)]
pub struct LifecycleEventMetadata {
    /// Optional vector of errors encountered by platform.
    #[serde(serialize_with = "serialize_errors")]
    pub errors: Option<Vec<Error>>,

    /// Type of lifecycle event being encoded in the schema.
    pub lifecycle_event_type: LifecycleEventType,

    /// The url with which the component was launched.
    pub component_url: String,

    /// Monotonic time in nanos.
    #[serde(serialize_with = "serialize_time")]
    pub timestamp: Time,
}

#[derive(Serialize, Debug)]
pub struct InspectMetadata {
    /// Optional vector of errors encountered by platform.
    #[serde(serialize_with = "serialize_errors")]
    pub errors: Option<Vec<Error>>,
    /// Name of diagnostics file producing data.
    pub filename: String,
    /// The url with which the component was launched.
    pub component_url: String,
    /// Monotonic time in nanos.
    #[serde(serialize_with = "serialize_time")]
    pub timestamp: Time,
}

#[derive(Serialize, Debug)]
pub struct Schema<Key: AsRef<str>> {
    /// Enum specifying that this schema is encoding data.
    pub data_source: DataSource,

    /// The metadata for the diagnostics payload.
    pub metadata: Metadata,

    /// Moniker of the component that generated the payload.
    pub moniker: String,

    /// Payload containing diagnostics data, if the payload exists, else None.
    pub payload: Option<NodeHierarchy<Key>>,

    /// Schema version.
    pub version: u64,
}

fn serialize_time<S>(time: &Time, serializer: S) -> Result<S::Ok, S::Error>
where
    S: Serializer,
{
    serializer.serialize_i64(time.into_nanos())
}

fn serialize_errors<S>(errors: &Option<Vec<Error>>, serializer: S) -> Result<S::Ok, S::Error>
where
    S: Serializer,
{
    match errors {
        Some(errors) => {
            let mut seq = serializer.serialize_seq(Some(errors.len()))?;
            for element in errors {
                seq.serialize_element(&element.message)?;
            }
            seq.end()
        }
        None => serializer.serialize_none(),
    }
}

pub type InspectSchema = Schema<String>;

impl Schema<String> {
    /// Creates a new schema for a lifecycle event.
    pub fn for_lifecycle_event(
        moniker: impl Into<String>,
        lifecycle_event_type: LifecycleEventType,
        timestamp: Time,
        component_url: impl Into<String>,
        errors: Vec<Error>,
    ) -> Schema<String> {
        let errors_opt = if errors.is_empty() { None } else { Some(errors) };

        let lifecycle_event_metadata = LifecycleEventMetadata {
            timestamp,
            component_url: component_url.into(),
            lifecycle_event_type,
            errors: errors_opt,
        };

        Schema {
            moniker: moniker.into(),
            version: *SCHEMA_VERSION,
            data_source: DataSource::LifecycleEvent,
            payload: None,
            metadata: Metadata::LifecycleEvent(lifecycle_event_metadata),
        }
    }

    /// Creates a new schema for inspect data.
    pub fn for_inspect(
        moniker: impl Into<String>,
        inspect_hierarchy: Option<NodeHierarchy>,
        timestamp: Time,
        component_url: impl Into<String>,
        filename: impl Into<String>,
        errors: Vec<Error>,
    ) -> InspectSchema {
        let errors_opt = if errors.is_empty() { None } else { Some(errors) };

        let inspect_metadata = InspectMetadata {
            timestamp,
            component_url: component_url.into(),
            filename: filename.into(),
            errors: errors_opt,
        };

        Schema {
            moniker: moniker.into(),
            version: *SCHEMA_VERSION,
            data_source: DataSource::Inspect,
            payload: inspect_hierarchy,
            metadata: Metadata::Inspect(inspect_metadata),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fuchsia_inspect_node_hierarchy::Property;
    use pretty_assertions;
    use serde_json::json;

    const TEST_URL: &'static str = "fuchsia-pkg://test";

    #[test]
    fn test_canonical_json_inspect_formatting() {
        let mut hierarchy = NodeHierarchy::new(
            "root",
            vec![Property::String("x".to_string(), "foo".to_string())],
            vec![],
        );

        hierarchy.sort();
        let json_schema = Schema::for_inspect(
            "a/b/c/d",
            Some(hierarchy),
            Time::from_nanos(123456),
            TEST_URL,
            "test_file_plz_ignore.inspect",
            Vec::new(),
        );

        let result_json =
            serde_json::to_value(&json_schema).expect("serialization should succeed.");

        let expected_json = json!({
          "moniker": "a/b/c/d",
          "version": 1,
          "data_source": "Inspect",
          "payload": {
            "root": {
              "x": "foo"
            }
          },
          "metadata": {
            "component_url": TEST_URL,
            "errors": null,
            "filename": "test_file_plz_ignore.inspect",
            "timestamp": 123456,
          }
        });

        pretty_assertions::assert_eq!(result_json, expected_json, "golden diff failed.");
    }

    #[test]
    fn test_errorful_json_inspect_formatting() {
        let json_schema = Schema::for_inspect(
            "a/b/c/d",
            None,
            Time::from_nanos(123456),
            TEST_URL,
            "test_file_plz_ignore.inspect",
            vec![Error { message: "too much fun being had.".to_string() }],
        );

        let result_json =
            serde_json::to_value(&json_schema).expect("serialization should succeed.");

        let expected_json = json!({
          "moniker": "a/b/c/d",
          "version": 1,
          "data_source": "Inspect",
          "payload": null,
          "metadata": {
            "component_url": TEST_URL,
            "errors": ["too much fun being had."],
            "filename": "test_file_plz_ignore.inspect",
            "timestamp": 123456,
          }
        });

        pretty_assertions::assert_eq!(result_json, expected_json, "golden diff failed.");
    }

    #[test]
    fn test_canonical_json_lifecycle_event_formatting() {
        let json_schema = Schema::for_lifecycle_event(
            "a/b/c/d",
            LifecycleEventType::DiagnosticsReady,
            Time::from_nanos(123456),
            TEST_URL,
            Vec::new(),
        );

        let result_json =
            serde_json::to_value(&json_schema).expect("serialization should succeed.");

        let expected_json = json!({
          "moniker": "a/b/c/d",
          "version": 1,
          "data_source": "LifecycleEvent",
          "payload": null,
          "metadata": {
            "component_url": TEST_URL,
            "errors": null,
            "lifecycle_event_type": "DiagnosticsReady",
            "timestamp": 123456,
          }
        });

        pretty_assertions::assert_eq!(result_json, expected_json, "golden diff failed.");
    }

    #[test]
    fn test_errorful_json_lifecycle_event_formatting() {
        let json_schema = Schema::for_lifecycle_event(
            "a/b/c/d",
            LifecycleEventType::DiagnosticsReady,
            Time::from_nanos(123456),
            TEST_URL,
            vec![Error { message: "too much fun being had.".to_string() }],
        );

        let result_json =
            serde_json::to_value(&json_schema).expect("serialization should succeed.");

        let expected_json = json!({
          "moniker": "a/b/c/d",
          "version": 1,
          "data_source": "LifecycleEvent",
          "payload": null,
          "metadata": {
            "errors": ["too much fun being had."],
            "lifecycle_event_type": "DiagnosticsReady",
            "component_url": TEST_URL,
            "timestamp": 123456,
          }
        });

        pretty_assertions::assert_eq!(result_json, expected_json, "golden diff failed.");
    }
}

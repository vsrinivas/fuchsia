// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_inspect_node_hierarchy::{
        serialization::{HierarchySerializer, RawJsonNodeHierarchySerializer},
        NodeHierarchy,
    },
    fuchsia_zircon::Time,
    lazy_static::lazy_static,
    serde::{ser::SerializeSeq, Serialize, Serializer},
    serde_json::Value,
};

lazy_static! {
    static ref SCHEMA_VERSION: i64 = 1;
}

#[derive(Serialize, Debug, PartialEq, Eq)]
pub enum DataSource {
    Inspect,
}

#[derive(Serialize, Debug, PartialEq, Eq)]
pub struct Error {
    pub message: String,
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

#[derive(Serialize, Debug)]
pub struct InspectMetadata {
    /// Optional vector of errors encountered by platform.
    #[serde(serialize_with = "serialize_errors")]
    pub errors: Option<Vec<Error>>,
    /// Name of diagnostics file producing data.
    pub filename: String,
    /// Monotonic time in nanos.
    #[serde(serialize_with = "serialize_time")]
    pub timestamp: Time,
}

#[derive(Serialize, Debug)]
pub struct JsonInspectSchema {
    /// Enum specifying that this schema is encoding inspect data.
    pub data_source: DataSource,

    /// The inspect metadata for the diagnostics payload.
    pub metadata: InspectMetadata,

    /// Moniker of the component that generated the payload.
    pub moniker: String,

    /// Payload containing diagnostics data, if the payload exists, else None.
    pub payload: Option<Value>,

    /// Schema version.
    pub version: i64,
}

impl JsonInspectSchema {
    pub fn new(
        moniker: String,
        inspect_hierarchy: Option<NodeHierarchy>,
        timestamp: Time,
        filename: String,
        errors: Vec<Error>,
    ) -> JsonInspectSchema {
        let node_hierarchy_json_encoding_opt = inspect_hierarchy
            .map(|inspect_hierarchy| RawJsonNodeHierarchySerializer::serialize(inspect_hierarchy));
        let errors_opt = if errors.is_empty() { None } else { Some(errors) };

        let inspect_metadata = InspectMetadata { timestamp, filename, errors: errors_opt };

        JsonInspectSchema {
            moniker,
            version: *SCHEMA_VERSION,
            data_source: DataSource::Inspect,
            payload: node_hierarchy_json_encoding_opt,
            metadata: inspect_metadata,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fuchsia_inspect_node_hierarchy::Property;
    use serde_json::json;
    #[test]
    fn test_canonicol_formatting() {
        let mut hierarchy = NodeHierarchy::new(
            "root",
            vec![Property::String("x".to_string(), "foo".to_string())],
            vec![],
        );

        hierarchy.sort();
        let json_schema = JsonInspectSchema::new(
            "a/b/c/d".to_string(),
            Some(hierarchy),
            Time::from_nanos(123456),
            "test_file_plz_ignore.inspect".to_string(),
            Vec::new(),
        );

        let pretty_json_string =
            serde_json::to_string_pretty(&json_schema).expect("serialization should succeed.");

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
            "timestamp": 123456,
            "errors": null,
            "filename": "test_file_plz_ignore.inspect"
          }
        });

        let expect_json_pretty_string = serde_json::to_string_pretty(&expected_json)
            .expect("serialization should be successful.");

        assert_eq!(pretty_json_string, expect_json_pretty_string, "golden diff failed.");
    }

    #[test]
    fn test_errorful_formatting() {
        let json_schema = JsonInspectSchema::new(
            "a/b/c/d".to_string(),
            None,
            Time::from_nanos(123456),
            "test_file_plz_ignore.inspect".to_string(),
            vec![Error { message: "too much fun being had.".to_string() }],
        );

        let pretty_json_string =
            serde_json::to_string_pretty(&json_schema).expect("serialization should succeed.");

        let expected_json = json!({
          "moniker": "a/b/c/d",
          "version": 1,
          "data_source": "Inspect",
          "payload": null,
          "metadata": {
            "timestamp": 123456,
            "errors": ["too much fun being had."],
            "filename": "test_file_plz_ignore.inspect"
          }
        });

        let expect_json_pretty_string = serde_json::to_string_pretty(&expected_json)
            .expect("serialization should be successful.");

        assert_eq!(pretty_json_string, expect_json_pretty_string, "golden diff failed.");
    }
}

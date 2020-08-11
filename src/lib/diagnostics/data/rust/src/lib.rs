// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_inspect_node_hierarchy::NodeHierarchy,
    serde::{self, de::Deserializer, Deserialize, Serialize, Serializer},
    std::{
        borrow::Borrow,
        fmt,
        hash::Hash,
        ops::{Deref, DerefMut},
        str::FromStr,
    },
};

const SCHEMA_VERSION: u64 = 1;

#[derive(Deserialize, Serialize, Clone, Debug, PartialEq, Eq)]
pub enum DataSource {
    Unknown,
    Inspect,
    LifecycleEvent,
}

impl Default for DataSource {
    fn default() -> Self {
        DataSource::Unknown
    }
}

#[derive(Debug, PartialEq, Clone, Eq)]
pub struct Error {
    pub message: String,
}

#[derive(Deserialize, Serialize, Clone, Debug, PartialEq, Eq)]
pub enum LifecycleType {
    Started,
    Stopped,
    Running,
    DiagnosticsReady,
}

#[derive(Deserialize, Serialize, Clone, Debug, PartialEq)]
#[serde(untagged)]
pub enum Metadata {
    Empty,
    Inspect(InspectMetadata),
    LifecycleEvent(LifecycleEventMetadata),
}

impl Default for Metadata {
    fn default() -> Self {
        Metadata::Empty
    }
}

/// Wraps a time for serialization and deserialization purposes.
#[derive(Clone, Debug, Serialize, Deserialize, PartialEq)]
pub struct Timestamp(u64);

impl fmt::Display for Timestamp {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", self.0)
    }
}

impl From<u64> for Timestamp {
    fn from(nanos: u64) -> Timestamp {
        Timestamp(nanos)
    }
}

impl From<i64> for Timestamp {
    fn from(nanos: i64) -> Timestamp {
        Timestamp(nanos as u64)
    }
}

impl Deref for Timestamp {
    type Target = u64;

    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

impl DerefMut for Timestamp {
    fn deref_mut(&mut self) -> &mut Self::Target {
        &mut self.0
    }
}

#[derive(Deserialize, Serialize, Clone, Debug, PartialEq)]
pub struct LifecycleEventMetadata {
    /// Optional vector of errors encountered by platform.
    pub errors: Option<Vec<Error>>,

    /// Type of lifecycle event being encoded in the payload.
    pub lifecycle_event_type: LifecycleType,

    /// The url with which the component was launched.
    pub component_url: String,

    /// Monotonic time in nanos.
    pub timestamp: Timestamp,
}

#[derive(Deserialize, Serialize, Clone, Debug, PartialEq)]
pub struct InspectMetadata {
    /// Optional vector of errors encountered by platform.
    pub errors: Option<Vec<Error>>,
    /// Name of diagnostics file producing data.
    pub filename: String,
    /// The url with which the component was launched.
    pub component_url: String,
    /// Monotonic time in nanos.
    pub timestamp: Timestamp,
}

/// An instance of diagnostics data with typed metadata and an optional nested payload.
#[derive(Deserialize, Serialize, Debug, Clone, PartialEq)]
pub struct Data<Key, Metadata> {
    /// The sourde of the data.
    #[serde(default)]
    // TODO(fxb/58033) remove this once the Metadata enum is gone everywhere
    pub data_source: DataSource,

    /// The metadata for the diagnostics payload.
    pub metadata: Metadata,

    /// Moniker of the component that generated the payload.
    pub moniker: String,

    /// Payload containing diagnostics data, if the payload exists, else None.
    #[serde(bound(
        deserialize = "Key: AsRef<str> + Clone + Eq + FromStr + Hash",
        serialize = "Key: AsRef<str>",
    ))]
    pub payload: Option<NodeHierarchy<Key>>,

    /// Schema version.
    #[serde(default)]
    pub version: u64,
}

pub type InspectData = Data<String, InspectMetadata>;
pub type LifecycleData = Data<String, LifecycleEventMetadata>;

impl Data<String, LifecycleEventMetadata> {
    /// Creates a new data instance for a lifecycle event.
    pub fn for_lifecycle_event(
        moniker: impl Into<String>,
        lifecycle_event_type: LifecycleType,
        payload: Option<NodeHierarchy>,
        component_url: impl Into<String>,
        timestamp: impl Into<Timestamp>,
        errors: Vec<Error>,
    ) -> LifecycleData {
        let errors_opt = if errors.is_empty() { None } else { Some(errors) };

        Data {
            moniker: moniker.into(),
            version: SCHEMA_VERSION,
            data_source: DataSource::LifecycleEvent,
            payload,
            metadata: LifecycleEventMetadata {
                timestamp: timestamp.into(),
                component_url: component_url.into(),
                lifecycle_event_type,
                errors: errors_opt,
            },
        }
    }
}

impl Data<String, InspectMetadata> {
    /// Creates a new data instance for inspect.
    pub fn for_inspect(
        moniker: impl Into<String>,
        inspect_hierarchy: Option<NodeHierarchy>,
        timestamp_nanos: impl Into<Timestamp>,
        component_url: impl Into<String>,
        filename: impl Into<String>,
        errors: Vec<Error>,
    ) -> InspectData {
        let errors_opt = if errors.is_empty() { None } else { Some(errors) };

        Data {
            moniker: moniker.into(),
            version: SCHEMA_VERSION,
            data_source: DataSource::Inspect,
            payload: inspect_hierarchy,
            metadata: InspectMetadata {
                timestamp: timestamp_nanos.into(),
                component_url: component_url.into(),
                filename: filename.into(),
                errors: errors_opt,
            },
        }
    }
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", self.message)
    }
}

impl Borrow<str> for Error {
    fn borrow(&self) -> &str {
        &self.message
    }
}

impl Serialize for Error {
    fn serialize<S: Serializer>(&self, ser: S) -> Result<S::Ok, S::Error> {
        self.message.serialize(ser)
    }
}

impl<'de> Deserialize<'de> for Error {
    fn deserialize<D>(de: D) -> Result<Self, D::Error>
    where
        D: Deserializer<'de>,
    {
        let message = String::deserialize(de)?;
        Ok(Error { message })
    }
}

impl Metadata {
    pub fn inspect(&self) -> Option<&InspectMetadata> {
        match self {
            Metadata::Inspect(m) => Some(m),
            _ => None,
        }
    }

    pub fn lifecycle_event(&self) -> Option<&LifecycleEventMetadata> {
        match self {
            Metadata::LifecycleEvent(m) => Some(m),
            _ => None,
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
        let json_schema = Data::for_inspect(
            "a/b/c/d",
            Some(hierarchy),
            123456u64,
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
        let json_schema = Data::for_inspect(
            "a/b/c/d",
            None,
            123456u64,
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
        let json_schema = Data::for_lifecycle_event(
            "a/b/c/d",
            LifecycleType::DiagnosticsReady,
            None,
            TEST_URL,
            123456u64,
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
        let json_schema = Data::for_lifecycle_event(
            "a/b/c/d",
            LifecycleType::DiagnosticsReady,
            None,
            TEST_URL,
            123456u64,
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

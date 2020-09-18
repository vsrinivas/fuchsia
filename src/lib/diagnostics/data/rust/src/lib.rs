// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_diagnostics::{DataType, Severity as FidlSeverity};
use fuchsia_inspect_node_hierarchy::{NodeHierarchy, Property};
use serde::{
    self,
    de::{DeserializeOwned, Deserializer},
    Deserialize, Serialize, Serializer,
};
use std::{
    borrow::Borrow,
    fmt,
    hash::Hash,
    ops::{Deref, DerefMut},
    str::FromStr,
};

const SCHEMA_VERSION: u64 = 1;

#[derive(Deserialize, Serialize, Clone, Debug, PartialEq, Eq)]
pub enum DataSource {
    Unknown,
    Inspect,
    LifecycleEvent,
    Logs,
}

impl Default for DataSource {
    fn default() -> Self {
        DataSource::Unknown
    }
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
    Logs(LogsMetadata),
}

impl Default for Metadata {
    fn default() -> Self {
        Metadata::Empty
    }
}

/// A trait implemented by marker types which denote "kinds" of diagnostics data.
pub trait DiagnosticsData {
    /// The type of metadata included in results of this type.
    type Metadata: DeserializeOwned + Serialize + Clone + Send + 'static;

    /// The type of key used for indexing node hierarchies in the payload.
    type Key: AsRef<str> + Clone + DeserializeOwned + Eq + FromStr + Hash + Send + 'static;

    /// The type of error returned in this metadata.
    type Error: Clone;

    /// Used to query for this kind of metadata in the ArchiveAccessor.
    const DATA_TYPE: DataType;

    /// Returns the component URL which generated this value.
    fn component_url(metadata: &Self::Metadata) -> &str;

    /// Returns the timestamp at which this value was recorded.
    fn timestamp(metadata: &Self::Metadata) -> Timestamp;

    /// Returns the errors recorded with this value, if any.
    fn errors(metadata: &Self::Metadata) -> &Option<Vec<Self::Error>>;

    /// Returns whether any errors are recorded on this value.
    fn has_errors(metadata: &Self::Metadata) -> bool {
        Self::errors(metadata).as_ref().map(|e| !e.is_empty()).unwrap_or_default()
    }
}

/// Lifecycle events track the start, stop, and diagnostics directory readiness of components.
#[derive(Deserialize, Serialize, Debug, Clone, PartialEq)]
pub struct Lifecycle;

impl DiagnosticsData for Lifecycle {
    type Metadata = LifecycleEventMetadata;
    type Key = String;
    type Error = Error;
    const DATA_TYPE: DataType = DataType::Lifecycle;

    fn component_url(metadata: &Self::Metadata) -> &str {
        &metadata.component_url
    }

    fn timestamp(metadata: &Self::Metadata) -> Timestamp {
        metadata.timestamp
    }

    fn errors(metadata: &Self::Metadata) -> &Option<Vec<Self::Error>> {
        &metadata.errors
    }
}

/// Inspect carries snapshots of metrics trees hosted by components.
#[derive(Deserialize, Serialize, Debug, Clone, PartialEq)]
pub struct Inspect;

impl DiagnosticsData for Inspect {
    type Metadata = InspectMetadata;
    type Key = String;
    type Error = Error;
    const DATA_TYPE: DataType = DataType::Inspect;

    fn component_url(metadata: &Self::Metadata) -> &str {
        &metadata.component_url
    }

    fn timestamp(metadata: &Self::Metadata) -> Timestamp {
        metadata.timestamp
    }

    fn errors(metadata: &Self::Metadata) -> &Option<Vec<Self::Error>> {
        &metadata.errors
    }
}

/// Logs carry streams of structured events from components.
#[derive(Deserialize, Serialize, Debug, Clone, PartialEq)]
pub struct Logs;

impl DiagnosticsData for Logs {
    type Metadata = LogsMetadata;
    type Key = LogsField;
    type Error = LogError;
    const DATA_TYPE: DataType = DataType::Logs;

    fn component_url(metadata: &Self::Metadata) -> &str {
        &metadata.component_url
    }

    fn timestamp(metadata: &Self::Metadata) -> Timestamp {
        metadata.timestamp
    }

    fn errors(metadata: &Self::Metadata) -> &Option<Vec<Self::Error>> {
        &metadata.errors
    }
}

/// Wraps a time for serialization and deserialization purposes.
#[derive(Clone, Copy, Debug, Deserialize, Eq, Ord, PartialEq, PartialOrd, Serialize)]
pub struct Timestamp(u64);

impl fmt::Display for Timestamp {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", self.0)
    }
}

impl From<usize> for Timestamp {
    fn from(nanos: usize) -> Timestamp {
        Timestamp(nanos as u64)
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

impl Into<i64> for Timestamp {
    fn into(self) -> i64 {
        self.0 as _
    }
}

#[cfg(target_os = "fuchsia")]
mod zircon {
    use super::*;
    use fuchsia_zircon as zx;

    impl From<zx::Time> for Timestamp {
        fn from(t: zx::Time) -> Timestamp {
            Timestamp(t.into_nanos() as u64)
        }
    }

    impl Into<zx::Time> for Timestamp {
        fn into(self) -> zx::Time {
            zx::Time::from_nanos(self.0 as i64)
        }
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

#[derive(Deserialize, Serialize, Clone, Debug, PartialEq)]
pub struct LogsMetadata {
    // TODO(fxbug.dev/58369) figure out exact spelling of pid/tid context and severity
    /// Optional vector of errors encountered by platform.
    pub errors: Option<Vec<LogError>>,

    /// The url with which the component was launched.
    pub component_url: String,

    /// Monotonic time in nanos.
    pub timestamp: Timestamp,

    /// Severity of the message.
    pub severity: Severity,

    /// Size of the original message on the wire, in bytes.
    pub size_bytes: usize,
}

/// Severities a log message can have, often called the log's "level".
// NOTE: this is only duplicated because we can't get Serialize/Deserialize on the FIDL type
#[derive(Clone, Copy, Debug, Deserialize, PartialEq, Serialize)]
pub enum Severity {
    /// Trace records include detailed information about program execution.
    Trace,

    /// Debug records include development-facing information about program execution.
    Debug,

    /// Info records include general information about program execution. (default)
    Info,

    /// Warning records include information about potentially problematic operations.
    Warn,

    /// Error records include information about failed operations.
    Error,

    /// Fatal records convey information about operations which cause a program's termination.
    Fatal,
}

impl From<FidlSeverity> for Severity {
    fn from(severity: FidlSeverity) -> Self {
        match severity {
            FidlSeverity::Trace => Severity::Trace,
            FidlSeverity::Debug => Severity::Debug,
            FidlSeverity::Info => Severity::Info,
            FidlSeverity::Warn => Severity::Warn,
            FidlSeverity::Error => Severity::Error,
            FidlSeverity::Fatal => Severity::Fatal,
        }
    }
}

/// An instance of diagnostics data with typed metadata and an optional nested payload.
#[derive(Deserialize, Serialize, Debug, Clone, PartialEq)]
pub struct Data<D: DiagnosticsData> {
    /// The source of the data.
    #[serde(default)]
    // TODO(fxb/58033) remove this once the Metadata enum is gone everywhere
    pub data_source: DataSource,

    /// The metadata for the diagnostics payload.
    #[serde(bound(
        deserialize = "D::Metadata: DeserializeOwned",
        serialize = "D::Metadata: Serialize"
    ))]
    pub metadata: D::Metadata,

    /// Moniker of the component that generated the payload.
    pub moniker: String,

    /// Payload containing diagnostics data, if the payload exists, else None.
    pub payload: Option<NodeHierarchy<D::Key>>,

    /// Schema version.
    #[serde(default)]
    pub version: u64,
}

pub type InspectData = Data<Inspect>;
pub type LifecycleData = Data<Lifecycle>;
pub type LogsData = Data<Logs>;

pub type LogsHierarchy = NodeHierarchy<LogsField>;
pub type LogsProperty = Property<LogsField>;

impl Data<Lifecycle> {
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

impl Data<Inspect> {
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

impl Data<Logs> {
    /// Creates a new data instance for logs.
    pub fn for_logs(
        moniker: impl Into<String>,
        payload: Option<LogsHierarchy>,
        timestamp_nanos: impl Into<Timestamp>,
        component_url: impl Into<String>,
        severity: impl Into<Severity>,
        size_bytes: usize,
        errors: Vec<LogError>,
    ) -> Self {
        let errors = if errors.is_empty() { None } else { Some(errors) };

        Data {
            moniker: moniker.into(),
            version: SCHEMA_VERSION,
            data_source: DataSource::Logs,
            payload,
            metadata: LogsMetadata {
                timestamp: timestamp_nanos.into(),
                component_url: component_url.into(),
                severity: severity.into(),
                size_bytes,
                errors,
            },
        }
    }
}

/// An enum containing well known argument names passed through logs, as well
/// as an `Other` variant for any other argument names.
///
/// This contains the fields of logs sent as a [`LogMessage`].
///
/// [`LogMessage`]: https://fuchsia.dev/reference/fidl/fuchsia.logger#LogMessage
#[derive(Clone, Debug, Deserialize, Eq, Hash, PartialEq, PartialOrd, Ord, Serialize)]
pub enum LogsField {
    ProcessId,
    ThreadId,
    Dropped,
    Tag,
    Verbosity,
    Msg,
    Other(String),
}

// TODO(50519) - ensure that strings reported here align with naming
// decisions made for the structured log format sent by other components.
pub const PID_LABEL: &str = "pid";
pub const TID_LABEL: &str = "tid";
pub const DROPPED_LABEL: &str = "num_dropped";
pub const TAG_LABEL: &str = "tag";
pub const MESSAGE_LABEL: &str = "message";
pub const VERBOSITY_LABEL: &str = "verbosity";

impl LogsField {
    pub fn is_legacy(&self) -> bool {
        matches!(
            self,
            LogsField::ProcessId
                | LogsField::ThreadId
                | LogsField::Dropped
                | LogsField::Tag
                | LogsField::Msg
                | LogsField::Verbosity
        )
    }
}

impl AsRef<str> for LogsField {
    fn as_ref(&self) -> &str {
        match self {
            Self::ProcessId => PID_LABEL,
            Self::ThreadId => TID_LABEL,
            Self::Dropped => DROPPED_LABEL,
            Self::Tag => TAG_LABEL,
            Self::Msg => MESSAGE_LABEL,
            Self::Verbosity => VERBOSITY_LABEL,
            Self::Other(str) => str.as_str(),
        }
    }
}

impl<T> From<T> for LogsField
where
    // Deref instead of AsRef b/c LogsField: AsRef<str> so this conflicts with concrete From<Self>
    T: Deref<Target = str>,
{
    fn from(s: T) -> Self {
        match s.as_ref() {
            PID_LABEL => Self::ProcessId,
            TID_LABEL => Self::ThreadId,
            DROPPED_LABEL => Self::Dropped,
            VERBOSITY_LABEL => Self::Verbosity,
            TAG_LABEL => Self::Tag,
            MESSAGE_LABEL => Self::Msg,
            _ => Self::Other(s.to_string()),
        }
    }
}

impl FromStr for LogsField {
    type Err = ();
    fn from_str(s: &str) -> Result<Self, Self::Err> {
        Ok(Self::from(s))
    }
}

#[derive(Clone, Deserialize, Debug, Eq, PartialEq, Serialize)]
pub enum LogError {
    DroppedLogs { count: u64 },
    Other(Error),
}

#[derive(Debug, PartialEq, Clone, Eq)]
pub struct Error {
    pub message: String,
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

// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// NOTE: this file is a clone of src/lib/diagnostics/data/rust/src/lib.rs
// at cb380a85c8c6d09f09b5e72f9fd465dae05513ea, the parent of the most recent
// breaking change to the format (fxrev.dev/522140).
// The format specified by structures in this file is frozen and
// should *not* be changed.

use fidl_fuchsia_diagnostics::DataType;
use serde::{
    self,
    de::{DeserializeOwned, Deserializer},
    Deserialize, Serialize, Serializer,
};
use std::{borrow::Borrow, fmt, hash::Hash, ops::Deref, str::FromStr, time::Duration};

pub use diagnostics_hierarchy::{
    assert_data_tree, hierarchy, tree_assertion, DiagnosticsHierarchy, Property,
};

use diagnostics_data::{Severity, Timestamp};

const SCHEMA_VERSION: u64 = 1;
const MICROS_IN_SEC: u128 = 1000000;

/// The source of diagnostics data
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

/// Metadata contained in a `DiagnosticsData` object.
#[derive(Deserialize, Serialize, Clone, Debug, PartialEq)]
#[serde(untagged)]
pub enum Metadata {
    Empty,
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
    /// Transforms a Metdata string into a errorful metadata, overriding any other
    /// errors.
    fn override_error(metadata: Self::Metadata, error: String) -> Self::Metadata;
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

    fn override_error(metadata: Self::Metadata, error: String) -> Self::Metadata {
        LogsMetadata {
            severity: metadata.severity,
            size_bytes: metadata.size_bytes,
            component_url: metadata.component_url,
            timestamp: metadata.timestamp,
            errors: Some(vec![LogError::Other(Error { message: error })]),
        }
    }
}

/// The metadata contained in a `DiagnosticsData` object where the data source is
/// `DataSource::Logs`.
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

/// An instance of diagnostics data with typed metadata and an optional nested payload.
#[derive(Deserialize, Serialize, Debug, Clone, PartialEq)]
pub struct Data<D: DiagnosticsData> {
    /// The source of the data.
    #[serde(default)]
    // TODO(fxbug.dev/58033) remove this once the Metadata enum is gone everywhere
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
    pub payload: Option<DiagnosticsHierarchy<D::Key>>,

    /// Schema version.
    #[serde(default)]
    pub version: u64,
}

impl<D> Data<D>
where
    D: DiagnosticsData,
{
    pub fn dropped_payload_schema(self, error_string: String) -> Data<D>
    where
        D: DiagnosticsData,
    {
        Data {
            metadata: D::override_error(self.metadata, error_string),
            moniker: self.moniker,
            data_source: self.data_source,
            version: self.version,
            payload: None,
        }
    }
}

/// A diagnostics data object containing logs data.
pub type LogsData = Data<Logs>;

/// A diagnostics data payload containing logs data.
pub type LogsHierarchy = DiagnosticsHierarchy<LogsField>;

/// A diagnostics hierarchy property keyed by `LogsField`.
pub type LogsProperty = Property<LogsField>;

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

    /// Returns the string log associated with the message, if one exists.
    pub fn msg(&self) -> Option<&str> {
        self.payload
            .as_ref()
            .map(|p| {
                p.properties
                    .iter()
                    .filter_map(|property| match property {
                        LogsProperty::String(LogsField::Msg, msg) => Some(msg.as_str()),
                        _ => None,
                    })
                    .next()
            })
            .flatten()
    }

    /// Returns the file path associated with the message, if one exists.
    pub fn file_path(&self) -> Option<&str> {
        self.payload
            .as_ref()
            .map(|p| {
                p.properties
                    .iter()
                    .filter_map(|property| match property {
                        LogsProperty::String(LogsField::FilePath, msg) => Some(msg.as_str()),
                        _ => None,
                    })
                    .next()
            })
            .flatten()
    }

    /// Returns the line number associated with the message, if one exists.
    pub fn line_number(&self) -> Option<&u64> {
        self.payload
            .as_ref()
            .map(|p| {
                p.properties
                    .iter()
                    .filter_map(|property| match property {
                        LogsProperty::Uint(LogsField::LineNumber, msg) => Some(msg),
                        _ => None,
                    })
                    .next()
            })
            .flatten()
    }

    /// Returns the pid associated with the message, if one exists.
    pub fn pid(&self) -> Option<u64> {
        self.payload.as_ref().and_then(|p| {
            p.properties
                .iter()
                .filter_map(|property| match property {
                    LogsProperty::Uint(LogsField::ProcessId, pid) => Some(*pid),
                    _ => None,
                })
                .next()
        })
    }

    /// Returns the tid associated with the message, if one exists.
    pub fn tid(&self) -> Option<u64> {
        self.payload.as_ref().and_then(|p| {
            p.properties
                .iter()
                .filter_map(|property| match property {
                    LogsProperty::Uint(LogsField::ThreadId, tid) => Some(*tid),
                    _ => None,
                })
                .next()
        })
    }
}

impl fmt::Display for Data<Logs> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        // Multiple tags are supported for the `LogMessage` format and are represented
        // as multiple instances of LogsField::Tag arguments.
        let tags = self
            .payload
            .as_ref()
            .map(|p| {
                p.properties
                    .iter()
                    .filter_map(|property| match property {
                        LogsProperty::String(LogsField::Tag, tag) => Some(tag.as_str()),
                        _ => None,
                    })
                    .collect::<Vec<_>>()
            })
            .unwrap_or(vec![]);
        let kvps = self
            .payload
            .as_ref()
            .map(|p| {
                p.properties
                    .iter()
                    .filter_map(|property| match property {
                        LogsProperty::String(LogsField::Tag, _tag) => None,
                        LogsProperty::String(LogsField::ProcessId, _tag) => None,
                        LogsProperty::String(LogsField::ThreadId, _tag) => None,
                        LogsProperty::String(LogsField::Verbosity, _tag) => None,
                        LogsProperty::String(LogsField::Dropped, _tag) => None,
                        LogsProperty::String(LogsField::Msg, _tag) => None,
                        LogsProperty::String(LogsField::FilePath, _tag) => None,
                        LogsProperty::String(LogsField::LineNumber, _tag) => None,
                        LogsProperty::String(LogsField::Other(key), value) => {
                            Some(format!("{}={}", key.to_string(), value))
                        }
                        LogsProperty::Bytes(LogsField::Other(key), _) => {
                            Some(format!("{} = <bytes>", key.to_string()))
                        }
                        LogsProperty::Int(LogsField::Other(key), value) => {
                            Some(format!("{}={}", key.to_string(), value))
                        }
                        LogsProperty::Uint(LogsField::Other(key), value) => {
                            Some(format!("{}={}", key.to_string(), value))
                        }
                        LogsProperty::Double(LogsField::Other(key), value) => {
                            Some(format!("{}={}", key.to_string(), value))
                        }
                        LogsProperty::Bool(LogsField::Other(key), value) => {
                            Some(format!("{}={}", key.to_string(), value))
                        }
                        LogsProperty::DoubleArray(LogsField::Other(key), value) => {
                            Some(format!("{}={:?}", key.to_string(), value))
                        }
                        LogsProperty::IntArray(LogsField::Other(key), value) => {
                            Some(format!("{}={:?}", key.to_string(), value))
                        }
                        LogsProperty::UintArray(LogsField::Other(key), value) => {
                            Some(format!("{}={:?}", key.to_string(), value))
                        }
                        LogsProperty::StringList(LogsField::Other(key), value) => {
                            Some(format!("{}={:?}", key.to_string(), value))
                        }
                        _ => None,
                    })
                    .collect::<Vec<_>>()
            })
            .unwrap_or(vec![]);
        let mut file = None;
        let mut line = None;

        for p in self.payload.as_ref() {
            for property in &p.properties {
                match property {
                    LogsProperty::String(LogsField::FilePath, tag) => {
                        file = Some(tag.to_string());
                        ()
                    }
                    LogsProperty::Uint(LogsField::LineNumber, tag) => {
                        line = Some(tag);
                        ()
                    }
                    _ => (),
                }
            }
        }
        let time: Duration = self.metadata.timestamp.into();
        write!(
            f,
            "[{:05}.{:06}][{}][{}][{}]",
            time.as_secs(),
            time.as_micros() % MICROS_IN_SEC,
            self.pid().map(|s| s.to_string()).unwrap_or("".to_string()),
            self.tid().map(|s| s.to_string()).unwrap_or("".to_string()),
            self.moniker,
        )?;
        let mut file_line_str = "".to_string();
        if file.is_some() && line.is_some() {
            file_line_str = format!(" [{}({})]", file.unwrap(), line.unwrap());
        }
        if !tags.is_empty() {
            write!(f, "[{}]", tags.join(","))?;
        }

        write!(f, " {}:{} {}", self.metadata.severity, file_line_str, self.msg().unwrap_or(""))?;
        if !kvps.is_empty() {
            for kvp in kvps {
                write!(f, " {}", kvp)?;
            }
        }
        Ok(())
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
    FilePath,
    LineNumber,
    Other(String),
}

// TODO(fxbug.dev/50519) - ensure that strings reported here align with naming
// decisions made for the structured log format sent by other components.
pub const PID_LABEL: &str = "pid";
pub const TID_LABEL: &str = "tid";
pub const DROPPED_LABEL: &str = "num_dropped";
pub const TAG_LABEL: &str = "tag";
pub const MESSAGE_LABEL: &str = "message";
pub const VERBOSITY_LABEL: &str = "verbosity";
pub const FILE_PATH_LABEL: &str = "file";
pub const LINE_NUMBER_LABEL: &str = "line";

impl LogsField {
    /// Whether the logs field is legacy or not.
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
            Self::FilePath => FILE_PATH_LABEL,
            Self::LineNumber => LINE_NUMBER_LABEL,
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
            FILE_PATH_LABEL => Self::FilePath,
            LINE_NUMBER_LABEL => Self::LineNumber,
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

/// Possible errors that can come in a `DiagnosticsData` object where the data source is
/// `DataSource::Logs`.
#[derive(Clone, Deserialize, Debug, Eq, PartialEq, Serialize)]
pub enum LogError {
    #[serde(rename = "dropped_logs")]
    DroppedLogs { count: u64 },
    #[serde(rename = "other")]
    Other(Error),
}

/// Possible error that can come in a `DiagnosticsData` object where the data source is
/// `DataSource::LifecycleEvent` or `DataSource::Inspect`.
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

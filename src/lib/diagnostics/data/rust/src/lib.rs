// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! # Diagnostics data
//!
//! This library contains the Diagnostics data schema used for inspect and logs . This is
//! the data that the Archive returns on `fuchsia.diagnostics.ArchiveAccessor` reads.

use anyhow::format_err;
use fidl_fuchsia_diagnostics::{DataType, Severity as FidlSeverity};
use serde::{
    self,
    de::{DeserializeOwned, Deserializer},
    Deserialize, Serialize, Serializer,
};
use std::{
    borrow::Borrow,
    cmp::Ordering,
    convert::TryFrom,
    fmt,
    hash::Hash,
    ops::{Deref, DerefMut},
    str::FromStr,
    time::Duration,
};

pub use diagnostics_hierarchy::{
    assert_data_tree, hierarchy, tree_assertion, DiagnosticsHierarchy, Property,
};

#[cfg(target_os = "fuchsia")]
mod logs_legacy;

#[cfg(target_os = "fuchsia")]
pub use crate::logs_legacy::*;

const SCHEMA_VERSION: u64 = 1;
const MICROS_IN_SEC: u128 = 1000000;

/// The source of diagnostics data
#[derive(Deserialize, Serialize, Clone, Debug, PartialEq, Eq)]
pub enum DataSource {
    Unknown,
    Inspect,
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
    Inspect(InspectMetadata),
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
    fn component_url(metadata: &Self::Metadata) -> Option<&str>;

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

/// Inspect carries snapshots of data trees hosted by components.
#[derive(Deserialize, Serialize, Debug, Clone, PartialEq)]
pub struct Inspect;

impl DiagnosticsData for Inspect {
    type Metadata = InspectMetadata;
    type Key = String;
    type Error = InspectError;
    const DATA_TYPE: DataType = DataType::Inspect;

    fn component_url(metadata: &Self::Metadata) -> Option<&str> {
        metadata.component_url.as_ref().map(|s| s.as_str())
    }

    fn timestamp(metadata: &Self::Metadata) -> Timestamp {
        Timestamp(metadata.timestamp)
    }

    fn errors(metadata: &Self::Metadata) -> &Option<Vec<Self::Error>> {
        &metadata.errors
    }

    fn override_error(metadata: Self::Metadata, error: String) -> Self::Metadata {
        InspectMetadata {
            filename: metadata.filename,
            component_url: metadata.component_url,
            timestamp: metadata.timestamp,
            errors: Some(vec![InspectError { message: error.into() }]),
        }
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

    fn component_url(metadata: &Self::Metadata) -> Option<&str> {
        metadata.component_url.as_ref().map(|s| s.as_str())
    }

    fn timestamp(metadata: &Self::Metadata) -> Timestamp {
        Timestamp(metadata.timestamp)
    }

    fn errors(metadata: &Self::Metadata) -> &Option<Vec<Self::Error>> {
        &metadata.errors
    }

    fn override_error(metadata: Self::Metadata, error: String) -> Self::Metadata {
        LogsMetadata {
            severity: metadata.severity,
            component_url: metadata.component_url,
            timestamp: metadata.timestamp,
            errors: Some(vec![LogError::Other { message: error }]),
            file: metadata.file,
            line: metadata.line,
            pid: metadata.pid,
            tags: metadata.tags,
            tid: metadata.tid,
            dropped: None,
            size_bytes: None,
        }
    }
}

/// Wraps a time for serialization and deserialization purposes.
#[derive(Clone, Copy, Debug, Deserialize, Eq, Ord, PartialEq, PartialOrd, Serialize)]
pub struct Timestamp(i64);

impl fmt::Display for Timestamp {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", self.0)
    }
}

// i32 here because it's the default for a bare integer literal w/o a type suffix
impl From<i32> for Timestamp {
    fn from(nanos: i32) -> Timestamp {
        Timestamp(nanos as i64)
    }
}

impl From<i64> for Timestamp {
    fn from(nanos: i64) -> Timestamp {
        Timestamp(nanos)
    }
}

impl Into<i64> for Timestamp {
    fn into(self) -> i64 {
        self.0
    }
}

impl Into<Duration> for Timestamp {
    fn into(self) -> Duration {
        Duration::from_nanos(self.0 as u64)
    }
}

#[cfg(target_os = "fuchsia")]
mod zircon {
    use super::*;
    use fuchsia_zircon as zx;

    impl From<zx::Time> for Timestamp {
        fn from(t: zx::Time) -> Timestamp {
            Timestamp(t.into_nanos())
        }
    }

    impl Into<zx::Time> for Timestamp {
        fn into(self) -> zx::Time {
            zx::Time::from_nanos(self.0)
        }
    }
}

impl Deref for Timestamp {
    type Target = i64;

    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

impl DerefMut for Timestamp {
    fn deref_mut(&mut self) -> &mut Self::Target {
        &mut self.0
    }
}

/// The metadata contained in a `DiagnosticsData` object where the data source is
/// `DataSource::Inspect`.
#[derive(Deserialize, Serialize, Clone, Debug, PartialEq)]
pub struct InspectMetadata {
    /// Optional vector of errors encountered by platform.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub errors: Option<Vec<InspectError>>,
    /// Name of diagnostics file producing data.
    pub filename: String,
    /// The url with which the component was launched.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub component_url: Option<String>,
    /// Monotonic time in nanos.
    pub timestamp: i64,
}

/// The metadata contained in a `DiagnosticsData` object where the data source is
/// `DataSource::Logs`.
#[derive(Deserialize, Serialize, Clone, Debug, PartialEq)]
pub struct LogsMetadata {
    // TODO(fxbug.dev/58369) figure out exact spelling of pid/tid context and severity
    /// Optional vector of errors encountered by platform.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub errors: Option<Vec<LogError>>,

    /// The url with which the component was launched.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub component_url: Option<String>,

    /// Monotonic time in nanos.
    pub timestamp: i64,

    /// Severity of the message.
    pub severity: Severity,

    /// Tags to add at the beginning of the message
    #[serde(skip_serializing_if = "Option::is_none")]
    pub tags: Option<Vec<String>>,

    /// The process ID
    #[serde(skip_serializing_if = "Option::is_none")]
    pub pid: Option<u64>,

    /// The thread ID
    #[serde(skip_serializing_if = "Option::is_none")]
    pub tid: Option<u64>,

    /// The file name
    #[serde(skip_serializing_if = "Option::is_none")]
    pub file: Option<String>,

    /// The line number
    #[serde(skip_serializing_if = "Option::is_none")]
    pub line: Option<u64>,

    /// Number of dropped messages
    /// DEPRECATED: do not set. Left for backwards compatibility with older serialized metadatas
    /// that contain this field.
    #[serde(skip)]
    dropped: Option<u64>,

    /// Size of the original message on the wire, in bytes.
    /// DEPRECATED: do not set. Left for backwards compatibility with older serialized metadatas
    /// that contain this field.
    #[serde(skip)]
    size_bytes: Option<usize>,
}

/// Severities a log message can have, often called the log's "level".
// NOTE: this is only duplicated because we can't get Serialize/Deserialize on the FIDL type
#[derive(Clone, Copy, Debug, Deserialize, Eq, Ord, PartialEq, PartialOrd, Serialize)]
pub enum Severity {
    /// Trace records include detailed information about program execution.
    #[serde(rename = "TRACE", alias = "Trace")]
    Trace,

    /// Debug records include development-facing information about program execution.
    #[serde(rename = "DEBUG", alias = "Debug")]
    Debug,

    /// Info records include general information about program execution. (default)
    #[serde(rename = "INFO", alias = "Info")]
    Info,

    /// Warning records include information about potentially problematic operations.
    #[serde(rename = "WARN", alias = "Warn")]
    Warn,

    /// Error records include information about failed operations.
    #[serde(rename = "ERROR", alias = "Error")]
    Error,

    /// Fatal records convey information about operations which cause a program's termination.
    #[serde(rename = "FATAL", alias = "Fatal")]
    Fatal,
}

impl TryFrom<i32> for Severity {
    type Error = anyhow::Error;

    fn try_from(value: i32) -> Result<Self, anyhow::Error> {
        u8::try_from(value)
            .ok()
            .and_then(|num| FidlSeverity::from_primitive(num))
            .map(|s| Severity::from(s))
            .ok_or(format_err!("invalid severity number: {}", value))
    }
}

impl fmt::Display for Severity {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let repr = match self {
            Severity::Trace => "TRACE",
            Severity::Debug => "DEBUG",
            Severity::Info => "INFO",
            Severity::Warn => "WARN",
            Severity::Error => "ERROR",
            Severity::Fatal => "FATAL",
        };
        write!(f, "{}", repr)
    }
}

impl FromStr for Severity {
    type Err = anyhow::Error;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        let s = s.to_lowercase();
        match s.as_str() {
            "trace" => Ok(Severity::Trace),
            "debug" => Ok(Severity::Debug),
            "info" => Ok(Severity::Info),
            "warn" | "warning" => Ok(Severity::Warn),
            "error" => Ok(Severity::Error),
            "fatal" => Ok(Severity::Fatal),
            other => Err(format_err!("invalid severity: {}", other)),
        }
    }
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

/// A diagnostics data object containing inspect data.
pub type InspectData = Data<Inspect>;

/// A diagnostics data object containing logs data.
pub type LogsData = Data<Logs>;

/// A diagnostics data payload containing logs data.
pub type LogsHierarchy = DiagnosticsHierarchy<LogsField>;

/// A diagnostics hierarchy property keyed by `LogsField`.
pub type LogsProperty = Property<LogsField>;

impl Data<Inspect> {
    /// Creates a new data instance for inspect.
    pub fn for_inspect(
        moniker: impl Into<String>,
        inspect_hierarchy: Option<DiagnosticsHierarchy>,
        timestamp: impl Into<Timestamp>,
        component_url: impl Into<String>,
        filename: impl Into<String>,
        errors: Vec<InspectError>,
    ) -> InspectData {
        let errors_opt = if errors.is_empty() { None } else { Some(errors) };

        Data {
            moniker: moniker.into(),
            version: SCHEMA_VERSION,
            data_source: DataSource::Inspect,
            payload: inspect_hierarchy,
            metadata: InspectMetadata {
                timestamp: *(timestamp.into()),
                component_url: Some(component_url.into()),
                filename: filename.into(),
                errors: errors_opt,
            },
        }
    }
}

/// Internal state of the LogsDataBuilder impl
/// External customers should not directly access these fields.
pub struct LogsDataBuilder {
    /// List of errors
    errors: Vec<LogError>,
    /// Message in log
    msg: Option<String>,
    /// List of tags
    tags: Vec<String>,
    /// Process ID
    pid: Option<u64>,
    /// Thread ID
    tid: Option<u64>,
    /// File name
    file: Option<String>,
    /// Line number
    line: Option<u64>,
    /// BuilderArgs that was passed in at construction time
    args: BuilderArgs,
    /// List of KVPs from the user
    keys: Vec<Property<LogsField>>,
    /// Printf format string
    format: Option<String>,
    /// Arguments for printf string
    printf_args: Vec<String>,
}

pub struct BuilderArgs {
    /// The moniker for the component
    pub moniker: String,
    /// The timestamp of the message in nanoseconds
    pub timestamp_nanos: Timestamp,
    /// The component URL
    pub component_url: Option<String>,
    /// The message severity
    pub severity: Severity,
}

impl LogsDataBuilder {
    /// Constructs a new LogsDataBuilder
    pub fn new(args: BuilderArgs) -> Self {
        LogsDataBuilder {
            args,
            errors: vec![],
            msg: None,
            file: None,
            line: None,
            pid: None,
            tags: vec![],
            tid: None,
            keys: vec![],
            format: None,
            printf_args: vec![],
        }
    }

    /// Sets the number of dropped messages.
    /// If value is greater than zero, a DroppedLogs error
    /// will also be added to the list of errors or updated if
    /// already present.

    #[must_use = "You must call build on your builder to consume its result"]
    pub fn set_dropped(mut self, value: u64) -> Self {
        if value == 0 {
            return self;
        }
        let val = self.errors.iter_mut().find_map(|error| {
            if let LogError::DroppedLogs { count } = error {
                Some(count)
            } else {
                None
            }
        });
        if let Some(v) = val {
            *v = value;
        } else {
            self.errors.push(LogError::DroppedLogs { count: value });
        }
        self
    }

    pub fn set_severity(mut self, severity: Severity) -> Self {
        self.args.severity = severity;
        self
    }

    /// Sets the process ID that logged the message
    #[must_use = "You must call build on your builder to consume its result"]
    pub fn set_pid(mut self, value: u64) -> Self {
        self.pid = Some(value);
        self
    }

    /// Sets the thread ID that logged the message
    #[must_use = "You must call build on your builder to consume its result"]
    pub fn set_tid(mut self, value: u64) -> Self {
        self.tid = Some(value);
        self
    }

    /// Constructs a LogsData from this builder
    pub fn build(self) -> LogsData {
        let mut args = vec![];
        if let Some(msg) = self.msg {
            args.push(LogsProperty::String(LogsField::MsgStructured, msg));
        }
        let mut payload_fields = vec![DiagnosticsHierarchy::new("message", args, vec![])];
        if !self.keys.is_empty() {
            let val = DiagnosticsHierarchy::new("keys", self.keys, vec![]);
            payload_fields.push(val);
        }
        if let Some(format) = self.format {
            let val = DiagnosticsHierarchy::new(
                "printf".to_string(),
                vec![
                    LogsProperty::String(LogsField::Other("format".to_string()), format),
                    LogsProperty::StringList(
                        LogsField::Other("args".to_string()),
                        self.printf_args,
                    ),
                ],
                vec![],
            );
            payload_fields.push(val);
        }
        let mut payload = LogsHierarchy::new("root", vec![], payload_fields);
        payload.sort();
        let mut ret = LogsData::for_logs(
            self.args.moniker,
            Some(payload),
            self.args.timestamp_nanos,
            self.args.component_url,
            self.args.severity,
            self.errors,
        );
        ret.metadata.file = self.file;
        ret.metadata.line = self.line;
        ret.metadata.pid = self.pid;
        ret.metadata.tid = self.tid;
        ret.metadata.tags = Some(self.tags);
        return ret;
    }

    /// Adds an error
    #[must_use = "You must call build on your builder to consume its result"]
    pub fn add_error(mut self, error: LogError) -> Self {
        self.errors.push(error);
        self
    }

    /// Sets the message to be printed in the log message
    #[must_use = "You must call build on your builder to consume its result"]
    pub fn set_message(mut self, msg: impl Into<String>) -> Self {
        self.msg = Some(msg.into());
        self
    }

    /// Sets the printf format and arguments.
    #[must_use = "You must call build on your builder to consume its result"]
    pub fn set_format_printf(mut self, format: impl Into<String>, args: Vec<String>) -> Self {
        self.format = Some(format.into());
        self.printf_args = args;
        self
    }

    /// Sets the file name that printed this message.
    #[must_use = "You must call build on your builder to consume its result"]
    pub fn set_file(mut self, file: impl Into<String>) -> Self {
        self.file = Some(file.into());
        self
    }

    /// Sets the line number that printed this message.
    #[must_use = "You must call build on your builder to consume its result"]
    pub fn set_line(mut self, line: u64) -> Self {
        self.line = Some(line);
        self
    }

    /// Adds a property to the list of key value pairs that are a part of this log message.
    #[must_use = "You must call build on your builder to consume its result"]
    pub fn add_key(mut self, kvp: Property<LogsField>) -> Self {
        self.keys.push(kvp);
        self
    }

    /// Adds a tag to the list of tags that precede this log message.
    #[must_use = "You must call build on your builder to consume its result"]
    pub fn add_tag(mut self, tag: impl Into<String>) -> Self {
        self.tags.push(tag.into());
        self
    }
}

impl Data<Logs> {
    /// Creates a new data instance for logs.
    pub fn for_logs(
        moniker: impl Into<String>,
        payload: Option<LogsHierarchy>,
        timestamp: impl Into<Timestamp>,
        component_url: Option<String>,
        severity: impl Into<Severity>,
        errors: Vec<LogError>,
    ) -> Self {
        let errors = if errors.is_empty() { None } else { Some(errors) };

        Data {
            moniker: moniker.into(),
            version: SCHEMA_VERSION,
            data_source: DataSource::Logs,
            payload,
            metadata: LogsMetadata {
                timestamp: *(timestamp.into()),
                component_url: component_url,
                severity: severity.into(),
                errors,
                file: None,
                line: None,
                pid: None,
                tags: None,
                tid: None,
                dropped: None,
                size_bytes: None,
            },
        }
    }

    /// Returns the string log associated with the message, if one exists.
    pub fn msg(&self) -> Option<&str> {
        self.payload_message().as_ref().and_then(|p| {
            p.properties.iter().find_map(|property| match property {
                LogsProperty::String(LogsField::MsgStructured, msg) => Some(msg.as_str()),
                _ => None,
            })
        })
    }

    pub fn msg_mut(&mut self) -> Option<&mut String> {
        self.payload_message_mut().and_then(|p| {
            p.properties.iter_mut().find_map(|property| match property {
                LogsProperty::String(LogsField::MsgStructured, msg) => Some(msg),
                _ => None,
            })
        })
    }

    pub fn payload_printf_format(&mut self) -> Option<&str> {
        self.payload_printf().as_ref().and_then(|p| {
            p.properties.iter().find_map(|property| match property {
                LogsProperty::String(LogsField::Format, format) => Some(format.as_str()),
                _ => None,
            })
        })
    }

    pub fn payload_printf_args(&mut self) -> Option<&Vec<String>> {
        self.payload_printf().as_ref().and_then(|p| {
            p.properties.iter().find_map(|property| match property {
                LogsProperty::StringList(LogsField::Args, format) => Some(format),
                _ => None,
            })
        })
    }

    pub fn payload_printf(&self) -> Option<&DiagnosticsHierarchy<LogsField>> {
        self.payload
            .as_ref()
            .and_then(|p| p.children.iter().find(|property| property.name.as_str() == "printf"))
    }

    pub fn payload_message(&self) -> Option<&DiagnosticsHierarchy<LogsField>> {
        self.payload
            .as_ref()
            .and_then(|p| p.children.iter().find(|property| property.name.as_str() == "message"))
    }

    pub fn payload_keys(&self) -> Option<&DiagnosticsHierarchy<LogsField>> {
        self.payload
            .as_ref()
            .and_then(|p| p.children.iter().find(|property| property.name.as_str() == "keys"))
    }

    /// Returns an iterator over the payload keys as strings with the format "key=value".
    pub fn payload_keys_strings(&self) -> Box<dyn Iterator<Item = String> + '_> {
        let maybe_iter = self.payload_keys().map(|p| {
            Box::new(p.properties.iter().filter_map(|property| match property {
                LogsProperty::String(LogsField::Tag, _tag) => None,
                LogsProperty::String(LogsField::ProcessId, _tag) => None,
                LogsProperty::String(LogsField::ThreadId, _tag) => None,
                LogsProperty::String(LogsField::Verbosity, _tag) => None,
                LogsProperty::String(LogsField::Dropped, _tag) => None,
                LogsProperty::String(LogsField::Msg, _tag) => None,
                LogsProperty::String(LogsField::FilePath, _tag) => None,
                LogsProperty::String(LogsField::LineNumber, _tag) => None,
                LogsProperty::String(
                    key @ (LogsField::Other(_) | LogsField::MsgStructured),
                    value,
                ) => Some(format!("{}={}", key, value)),
                LogsProperty::Bytes(key @ (LogsField::Other(_) | LogsField::MsgStructured), _) => {
                    Some(format!("{} = <bytes>", key))
                }
                LogsProperty::Int(
                    key @ (LogsField::Other(_) | LogsField::MsgStructured),
                    value,
                ) => Some(format!("{}={}", key, value)),
                LogsProperty::Uint(
                    key @ (LogsField::Other(_) | LogsField::MsgStructured),
                    value,
                ) => Some(format!("{}={}", key, value)),
                LogsProperty::Double(
                    key @ (LogsField::Other(_) | LogsField::MsgStructured),
                    value,
                ) => Some(format!("{}={}", key, value)),
                LogsProperty::Bool(
                    key @ (LogsField::Other(_) | LogsField::MsgStructured),
                    value,
                ) => Some(format!("{}={}", key, value)),
                LogsProperty::DoubleArray(
                    key @ (LogsField::Other(_) | LogsField::MsgStructured),
                    value,
                ) => Some(format!("{}={:?}", key, value)),
                LogsProperty::IntArray(
                    key @ (LogsField::Other(_) | LogsField::MsgStructured),
                    value,
                ) => Some(format!("{}={:?}", key, value)),
                LogsProperty::UintArray(
                    key @ (LogsField::Other(_) | LogsField::MsgStructured),
                    value,
                ) => Some(format!("{}={:?}", key, value)),
                LogsProperty::StringList(
                    key @ (LogsField::Other(_) | LogsField::MsgStructured),
                    value,
                ) => Some(format!("{}={:?}", key, value)),
                _ => None,
            }))
        });
        match maybe_iter {
            Some(i) => Box::new(i),
            None => Box::new(std::iter::empty()),
        }
    }

    pub fn payload_message_mut(&mut self) -> Option<&mut DiagnosticsHierarchy<LogsField>> {
        self.payload.as_mut().and_then(|p| {
            p.children.iter_mut().find(|property| property.name.as_str() == "message")
        })
    }

    /// Returns the file path associated with the message, if one exists.
    pub fn file_path(&self) -> Option<&str> {
        self.metadata.file.as_ref().map(|file| file.as_str())
    }

    /// Returns the line number associated with the message, if one exists.
    pub fn line_number(&self) -> Option<&u64> {
        self.metadata.line.as_ref()
    }

    /// Returns the pid associated with the message, if one exists.
    pub fn pid(&self) -> Option<u64> {
        self.metadata.pid
    }

    /// Returns the tid associated with the message, if one exists.
    pub fn tid(&self) -> Option<u64> {
        self.metadata.tid
    }

    /// Returns the tags associated with the message, if any exist.
    pub fn tags(&self) -> Option<&Vec<String>> {
        self.metadata.tags.as_ref()
    }

    /// The message's severity.
    #[cfg(target_os = "fuchsia")]
    pub fn legacy_severity(&self) -> LegacySeverity {
        if let Some(verbosity) = self.verbosity() {
            LegacySeverity::Verbose(verbosity)
        } else {
            match self.metadata.severity {
                Severity::Trace => LegacySeverity::Trace,
                Severity::Debug => LegacySeverity::Debug,
                Severity::Info => LegacySeverity::Info,
                Severity::Warn => LegacySeverity::Warn,
                Severity::Error => LegacySeverity::Error,
                Severity::Fatal => LegacySeverity::Fatal,
            }
        }
    }

    /// Returns number of dropped logs if reported in the message.
    pub fn dropped_logs(&self) -> Option<u64> {
        self.metadata
            .errors
            .as_ref()
            .map(|errors| {
                errors.iter().find_map(|e| match e {
                    LogError::DroppedLogs { count } => Some(*count),
                    _ => None,
                })
            })
            .flatten()
    }

    /// Returns number of rolled out logs if reported in the message.
    pub fn rolled_out_logs(&self) -> Option<u64> {
        self.metadata
            .errors
            .as_ref()
            .map(|errors| {
                errors.iter().find_map(|e| match e {
                    LogError::RolledOutLogs { count } => Some(*count),
                    _ => None,
                })
            })
            .flatten()
    }

    pub fn verbosity(&self) -> Option<i8> {
        self.payload_message().and_then(|payload| {
            payload
                .properties
                .iter()
                .filter_map(|property| match property {
                    LogsProperty::Int(LogsField::Verbosity, verbosity) => Some(*verbosity as i8),
                    _ => None,
                })
                .next()
        })
    }

    pub fn set_legacy_verbosity(&mut self, legacy: i8) {
        if let Some(payload_message) = self.payload_message_mut() {
            payload_message.properties.push(LogsProperty::Int(LogsField::Verbosity, legacy.into()));
        }
    }

    #[cfg(target_os = "fuchsia")]
    pub(crate) fn non_legacy_contents(&self) -> Box<dyn Iterator<Item = &LogsProperty> + '_> {
        match self.payload_keys() {
            None => Box::new(std::iter::empty()),
            Some(payload) => Box::new(payload.properties.iter()),
        }
    }

    /// Returns the component nam. This only makes sense for v1 components.
    pub fn component_name(&self) -> &str {
        self.moniker.rsplit("/").next().unwrap_or("UNKNOWN")
    }
}

/// Display options for unstructured logs.
pub struct LogTextDisplayOptions {
    /// Whether or not to display the full moniker.
    pub show_full_moniker: bool,
}

/// Used to control stringification options of Data<Logs>
pub struct LogTextPresenter<'a> {
    /// The log to parameterize
    log: &'a Data<Logs>,

    /// Options for stringifying the log
    options: LogTextDisplayOptions,
}

impl<'a> LogTextPresenter<'a> {
    /// Creates a new LogTextPresenter with the specified options and
    /// log message. This presenter is bound to the lifetime of the
    /// underlying log message.
    pub fn new(log: &'a Data<Logs>, options: LogTextDisplayOptions) -> Self {
        Self { log, options }
    }
}

impl fmt::Display for Data<Logs> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        LogTextPresenter::new(self, LogTextDisplayOptions { show_full_moniker: true }).fmt(f)
    }
}

impl Deref for LogTextPresenter<'_> {
    type Target = Data<Logs>;
    fn deref(&self) -> &Self::Target {
        self.log
    }
}

fn strip_moniker(moniker: &str) -> &str {
    if let Some(last_slash) = moniker.rfind('/') {
        &moniker[last_slash + 1..]
    } else {
        moniker
    }
}

impl fmt::Display for LogTextPresenter<'_> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        // Multiple tags are supported for the `LogMessage` format and are represented
        // as multiple instances of LogsField::Tag arguments.
        let kvps = self.payload_keys_strings();
        let time: Duration = Duration::from_nanos(self.metadata.timestamp as u64);
        let moniker = if self.options.show_full_moniker {
            self.moniker.as_str()
        } else {
            strip_moniker(self.moniker.as_str())
        };
        if self.options.show_full_moniker {
            write!(
                f,
                "[{:05}.{:06}][{}][{}][{}]",
                time.as_secs(),
                time.as_micros() % MICROS_IN_SEC,
                self.pid().map(|s| s.to_string()).unwrap_or("".to_string()),
                self.tid().map(|s| s.to_string()).unwrap_or("".to_string()),
                moniker,
            )?;
        } else {
            write!(
                f,
                "[{:05}.{:06}][{}]",
                time.as_secs(),
                time.as_micros() % MICROS_IN_SEC,
                moniker,
            )?;
        }
        match &self.metadata.tags {
            Some(tags) if !tags.is_empty() => {
                write!(f, "[{}]", tags.join(","))?;
            }
            _ => {}
        }
        write!(f, " {}:", self.metadata.severity)?;
        if let (Some(file), Some(line)) = (&self.metadata.file, &self.metadata.line) {
            write!(f, " [{}({})]", file, line)?;
        }

        write!(f, " {}", self.msg().unwrap_or(""))?;
        for kvp in kvps {
            write!(f, " {}", kvp)?;
        }
        Ok(())
    }
}

impl Eq for Data<Logs> {}

impl PartialOrd for Data<Logs> {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}

impl Ord for Data<Logs> {
    fn cmp(&self, other: &Self) -> Ordering {
        self.metadata.timestamp.cmp(&other.metadata.timestamp)
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
    MsgStructured,
    FilePath,
    LineNumber,
    Args,
    Format,
    Other(String),
}

impl fmt::Display for LogsField {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            LogsField::ProcessId => write!(f, "pid"),
            LogsField::ThreadId => write!(f, "tid"),
            LogsField::Dropped => write!(f, "num_dropped"),
            LogsField::Tag => write!(f, "tag"),
            LogsField::Verbosity => write!(f, "verbosity"),
            LogsField::Msg => write!(f, "message"),
            LogsField::MsgStructured => write!(f, "value"),
            LogsField::FilePath => write!(f, "file_path"),
            LogsField::LineNumber => write!(f, "line_number"),
            LogsField::Args => write!(f, "args"),
            LogsField::Format => write!(f, "format"),
            LogsField::Other(name) => write!(f, "{}", name),
        }
    }
}

// TODO(fxbug.dev/50519) - ensure that strings reported here align with naming
// decisions made for the structured log format sent by other components.
pub const PID_LABEL: &str = "pid";
pub const TID_LABEL: &str = "tid";
pub const DROPPED_LABEL: &str = "num_dropped";
pub const TAG_LABEL: &str = "tag";
pub const MESSAGE_LABEL_STRUCTURED: &str = "value";
pub const MESSAGE_LABEL: &str = "message";
pub const FORMAT_LABEL: &str = "format";
pub const ARGS_LABEL: &str = "args";
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
            Self::MsgStructured => MESSAGE_LABEL_STRUCTURED,
            Self::Args => ARGS_LABEL,
            Self::Format => FORMAT_LABEL,
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
            MESSAGE_LABEL_STRUCTURED => Self::MsgStructured,
            FORMAT_LABEL => Self::Format,
            ARGS_LABEL => Self::Args,
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
    /// Represents the number of logs that were dropped by the component writing the logs due to an
    /// error writing to the socket before succeeding to write a log.
    #[serde(rename = "dropped_logs")]
    DroppedLogs { count: u64 },
    /// Represents the number of logs that were dropped for a component by the archivist due to the
    /// log buffer execeeding its maximum capacity before the current message.
    #[serde(rename = "rolled_out_logs")]
    RolledOutLogs { count: u64 },
    #[serde(rename = "parse_record")]
    FailedToParseRecord(String),
    #[serde(rename = "other")]
    Other { message: String },
}

/// Possible error that can come in a `DiagnosticsData` object where the data source is
/// `DataSource::Inspect`..
#[derive(Debug, PartialEq, Clone, Eq)]
pub struct InspectError {
    pub message: String,
}

impl fmt::Display for InspectError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", self.message)
    }
}

impl Borrow<str> for InspectError {
    fn borrow(&self) -> &str {
        &self.message
    }
}

impl Serialize for InspectError {
    fn serialize<S: Serializer>(&self, ser: S) -> Result<S::Ok, S::Error> {
        self.message.serialize(ser)
    }
}

impl<'de> Deserialize<'de> for InspectError {
    fn deserialize<D>(de: D) -> Result<Self, D::Error>
    where
        D: Deserializer<'de>,
    {
        let message = String::deserialize(de)?;
        Ok(Self { message })
    }
}

impl Metadata {
    /// Returns the inspect metadata or None if the metadata contained is not for inspect.
    pub fn inspect(&self) -> Option<&InspectMetadata> {
        match self {
            Metadata::Inspect(m) => Some(m),
            _ => None,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use diagnostics_hierarchy::hierarchy;
    use serde_json::json;

    const TEST_URL: &'static str = "fuchsia-pkg://test";

    #[fuchsia::test]
    fn test_canonical_json_inspect_formatting() {
        let mut hierarchy = hierarchy! {
            root: {
                x: "foo",
            }
        };

        hierarchy.sort();
        let json_schema = Data::for_inspect(
            "a/b/c/d",
            Some(hierarchy),
            123456i64,
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
            "filename": "test_file_plz_ignore.inspect",
            "timestamp": 123456,
          }
        });

        pretty_assertions::assert_eq!(result_json, expected_json, "golden diff failed.");
    }

    #[fuchsia::test]
    fn test_errorful_json_inspect_formatting() {
        let json_schema = Data::for_inspect(
            "a/b/c/d",
            None,
            123456i64,
            TEST_URL,
            "test_file_plz_ignore.inspect",
            vec![InspectError { message: "too much fun being had.".to_string() }],
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

    #[fuchsia::test]
    fn default_builder_test() {
        let builder = LogsDataBuilder::new(BuilderArgs {
            component_url: Some("url".to_string()),
            moniker: String::from("moniker"),
            severity: Severity::Info,
            timestamp_nanos: 0.into(),
        });
        //let tree = builder.build();
        let expected_json = json!({
          "moniker": "moniker",
          "version": 1,
          "data_source": "Logs",
          "payload": {
              "root":
              {
                  "message":{}
              }
          },
          "metadata": {
            "component_url": "url",
              "severity": "INFO",
              "tags": [],

            "timestamp": 0,
          }
        });
        let result_json =
            serde_json::to_value(&builder.build()).expect("serialization should succeed.");
        pretty_assertions::assert_eq!(result_json, expected_json, "golden diff failed.");
    }

    #[fuchsia::test]
    fn regular_message_test() {
        let builder = LogsDataBuilder::new(BuilderArgs {
            component_url: Some("url".to_string()),
            moniker: String::from("moniker"),
            severity: Severity::Info,
            timestamp_nanos: 0.into(),
        })
        .set_message("app")
        .set_file("test file.cc")
        .set_line(420)
        .set_pid(1001)
        .set_tid(200)
        .set_dropped(2)
        .add_tag("You're")
        .add_tag("IT!")
        .add_key(LogsProperty::String(LogsField::Other("key".to_string()), "value".to_string()));
        // TODO (http://fxbug.dev/77054): Convert to our custom DSL when possible.
        let expected_json = json!({
          "moniker": "moniker",
          "version": 1,
          "data_source": "Logs",
          "payload": {
              "root":
              {
                  "keys":{
                      "key":"value"
                  },
                  "message":{
                      "value":"app"
                  }
              }
          },
          "metadata": {
            "errors": [],
            "component_url": "url",
              "errors": [{"dropped_logs":{"count":2}}],
              "file": "test file.cc",
              "line": 420,
              "pid": 1001,
              "severity": "INFO",
              "tags": ["You're", "IT!"],
              "tid": 200,

            "timestamp": 0,
          }
        });
        let result_json =
            serde_json::to_value(&builder.build()).expect("serialization should succeed.");
        pretty_assertions::assert_eq!(result_json, expected_json, "golden diff failed.");
    }

    #[fuchsia::test]
    fn printf_test() {
        let builder = LogsDataBuilder::new(BuilderArgs {
            component_url: Some("url".to_string()),
            moniker: String::from("moniker"),
            severity: Severity::Info,
            timestamp_nanos: 0.into(),
        })
        .set_format_printf("app", vec!["some".to_string(), "arg".to_string()])
        .set_file("test file.cc")
        .set_line(420)
        .set_pid(1001)
        .set_tid(200)
        .set_dropped(2)
        .add_tag("You're")
        .add_tag("IT!")
        .add_key(LogsProperty::String(LogsField::Other("key".to_string()), "value".to_string()));
        let expected_json = json!({
          "moniker": "moniker",
          "version": 1,
          "data_source": "Logs",
          "payload": {
              "root":
              {
                  "keys":{
                      "key":"value"
                  },
                  "printf":{
                    "args":["some", "arg"],
                    "format":"app"
                  },
                  "message":{
                  }
              }
          },
          "metadata": {
            "errors": [],
            "component_url": "url",
              "errors": [{"dropped_logs":{"count":2}}],
              "file": "test file.cc",
              "line": 420,
              "pid": 1001,
              "severity": "INFO",
              "tags": ["You're", "IT!"],
              "tid": 200,

            "timestamp": 0,
          }
        });
        let result_json =
            serde_json::to_value(&builder.build()).expect("serialization should succeed.");
        pretty_assertions::assert_eq!(result_json, expected_json, "golden diff failed.");
    }

    #[fuchsia::test]
    fn display_for_logs() {
        let data = LogsDataBuilder::new(BuilderArgs {
            timestamp_nanos: Timestamp::from(12345678000i64).into(),
            component_url: Some(String::from("fake-url")),
            moniker: String::from("moniker"),
            severity: Severity::Info,
        })
        .set_pid(123)
        .set_tid(456)
        .set_message("some message".to_string())
        .set_file("some_file.cc".to_string())
        .set_line(420)
        .add_tag("foo")
        .add_tag("bar")
        .add_key(LogsProperty::String(LogsField::Other("test".to_string()), "property".to_string()))
        .add_key(LogsProperty::String(LogsField::MsgStructured, "test".to_string()))
        .build();

        assert_eq!(
            "[00012.345678][123][456][moniker][foo,bar] INFO: [some_file.cc(420)] some message test=property value=test",
            format!("{}", data)
        )
    }

    #[fuchsia::test]
    fn display_for_logs_partial_moniker() {
        let data = LogsDataBuilder::new(BuilderArgs {
            timestamp_nanos: Timestamp::from(12345678000i64).into(),
            component_url: Some(String::from("fake-url")),
            moniker: String::from("test/moniker"),
            severity: Severity::Info,
        })
        .set_pid(123)
        .set_tid(456)
        .set_message("some message".to_string())
        .set_file("some_file.cc".to_string())
        .set_line(420)
        .add_tag("foo")
        .add_tag("bar")
        .add_key(LogsProperty::String(LogsField::Other("test".to_string()), "property".to_string()))
        .add_key(LogsProperty::String(LogsField::MsgStructured, "test".to_string()))
        .build();

        assert_eq!(
            "[00012.345678][moniker][foo,bar] INFO: [some_file.cc(420)] some message test=property value=test",
            format!("{}", LogTextPresenter::new(&data, LogTextDisplayOptions{show_full_moniker:false}))
        )
    }

    #[fuchsia::test]
    fn display_for_logs_no_tags() {
        let data = LogsDataBuilder::new(BuilderArgs {
            timestamp_nanos: Timestamp::from(12345678000i64).into(),
            component_url: Some(String::from("fake-url")),
            moniker: String::from("moniker"),
            severity: Severity::Info,
        })
        .set_pid(123)
        .set_tid(456)
        .set_message("some message".to_string())
        .build();

        assert_eq!("[00012.345678][123][456][moniker] INFO: some message", format!("{}", data))
    }

    #[fuchsia::test]
    fn display_for_logs_with_args_no_printf() {
        let data = LogsDataBuilder::new(BuilderArgs {
            timestamp_nanos: Timestamp::from(12345678000i64).into(),
            component_url: Some(String::from("fake-url")),
            moniker: String::from("moniker"),
            severity: Severity::Info,
        })
        .set_pid(123)
        .set_tid(456)
        .set_message("some message".to_string())
        .add_key(LogsProperty::String(LogsField::Other("args".to_string()), "value".to_string()))
        .build();

        assert_eq!(
            "[00012.345678][123][456][moniker] INFO: some message args=value",
            format!("{}", data)
        )
    }

    #[fuchsia::test]
    fn size_bytes_deserialize_backwards_compatibility() {
        let original_json = json!({
          "moniker": "a/b",
          "version": 1,
          "data_source": "Logs",
          "payload": {
            "root": {
              "message":{}
            }
          },
          "metadata": {
            "component_url": "url",
              "severity": "INFO",
              "tags": [],

            "timestamp": 123,
          }
        });
        let expected_data = LogsDataBuilder::new(BuilderArgs {
            component_url: Some("url".to_string()),
            moniker: String::from("a/b"),
            severity: Severity::Info,
            timestamp_nanos: 123.into(),
        })
        .build();
        let original_data: LogsData = serde_json::from_value(original_json).unwrap();
        assert_eq!(original_data, expected_data);
        // We skip deserializing the size_bytes
        assert_eq!(original_data.metadata.size_bytes, None);
    }

    #[fuchsia::test]
    fn dropped_deserialize_backwards_compatibility() {
        let original_json = json!({
          "moniker": "a/b",
          "version": 1,
          "data_source": "Logs",
          "payload": {
            "root": {
              "message":{}
            }
          },
          "metadata": {
            "dropped": 0,
            "component_url": "url",
              "severity": "INFO",
              "tags": [],

            "timestamp": 123,
          }
        });
        let expected_data = LogsDataBuilder::new(BuilderArgs {
            component_url: Some("url".to_string()),
            moniker: String::from("a/b"),
            severity: Severity::Info,
            timestamp_nanos: 123.into(),
        })
        .build();
        let original_data: LogsData = serde_json::from_value(original_json).unwrap();
        assert_eq!(original_data, expected_data);
        // We skip deserializing dropped
        assert_eq!(original_data.metadata.dropped, None);
    }

    #[fuchsia::test]
    fn severity_aliases() {
        assert_eq!(Severity::from_str("warn").unwrap(), Severity::Warn);
        assert_eq!(Severity::from_str("warning").unwrap(), Severity::Warn);
    }
}

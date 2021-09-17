// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! # Diagnostics data
//!
//! This library contians the Diagnostics data schema used for inspect, logs and lifecycle. This is
//! the data that the Archive returns on `fuchsia.diagnostics.ArchiveAccessor` reads.

use fidl_fuchsia_diagnostics::{DataType, Severity as FidlSeverity};
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
    time::Duration,
};

pub use diagnostics_hierarchy::{
    assert_data_tree, hierarchy, tree_assertion, DiagnosticsHierarchy, Property,
};

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

/// The type of a lifecycle event exposed by the `fuchsia.diagnostics.ArchiveAccessor`
#[derive(Deserialize, Serialize, Clone, Debug, PartialEq, Eq)]
pub enum LifecycleType {
    Started,
    Stopped,
    Running,
    DiagnosticsReady,
}

/// Metadata contained in a `DiagnosticsData` object.
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

/// Lifecycle events track the start, stop, and diagnostics directory readiness of components.
#[derive(Deserialize, Serialize, Debug, Clone, PartialEq)]
pub struct Lifecycle;

impl DiagnosticsData for Lifecycle {
    type Metadata = LifecycleEventMetadata;
    type Key = String;
    type Error = Error;
    const DATA_TYPE: DataType = DataType::Lifecycle;

    fn component_url(metadata: &Self::Metadata) -> Option<&str> {
        metadata.component_url.as_ref().map(|s| s.as_str())
    }

    fn timestamp(metadata: &Self::Metadata) -> Timestamp {
        metadata.timestamp
    }

    fn errors(metadata: &Self::Metadata) -> &Option<Vec<Self::Error>> {
        &metadata.errors
    }

    fn override_error(metadata: Self::Metadata, error: String) -> Self::Metadata {
        LifecycleEventMetadata {
            lifecycle_event_type: metadata.lifecycle_event_type,
            component_url: metadata.component_url,
            timestamp: metadata.timestamp,
            errors: Some(vec![Error { message: error.into() }]),
        }
    }
}

/// Inspect carries snapshots of data trees hosted by components.
#[derive(Deserialize, Serialize, Debug, Clone, PartialEq)]
pub struct Inspect;

impl DiagnosticsData for Inspect {
    type Metadata = InspectMetadata;
    type Key = String;
    type Error = Error;
    const DATA_TYPE: DataType = DataType::Inspect;

    fn component_url(metadata: &Self::Metadata) -> Option<&str> {
        metadata.component_url.as_ref().map(|s| s.as_str())
    }

    fn timestamp(metadata: &Self::Metadata) -> Timestamp {
        metadata.timestamp
    }

    fn errors(metadata: &Self::Metadata) -> &Option<Vec<Self::Error>> {
        &metadata.errors
    }

    fn override_error(metadata: Self::Metadata, error: String) -> Self::Metadata {
        InspectMetadata {
            filename: metadata.filename,
            component_url: metadata.component_url,
            timestamp: metadata.timestamp,
            errors: Some(vec![Error { message: error.into() }]),
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
            dropped: metadata.dropped,
            file: metadata.file,
            line: metadata.line,
            pid: metadata.pid,
            tags: metadata.tags,
            tid: metadata.tid,
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
/// `DataSource::LifecycleEvent`.
#[derive(Deserialize, Serialize, Clone, Debug, PartialEq)]
pub struct LifecycleEventMetadata {
    /// Optional vector of errors encountered by platform.
    pub errors: Option<Vec<Error>>,

    /// Type of lifecycle event being encoded in the payload.
    pub lifecycle_event_type: LifecycleType,

    /// The url with which the component was launched.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub component_url: Option<String>,

    /// Monotonic time in nanos.
    pub timestamp: Timestamp,
}

/// The metadata contained in a `DiagnosticsData` object where the data source is
/// `DataSource::Inspect`.
#[derive(Deserialize, Serialize, Clone, Debug, PartialEq)]
pub struct InspectMetadata {
    /// Optional vector of errors encountered by platform.
    pub errors: Option<Vec<Error>>,
    /// Name of diagnostics file producing data.
    pub filename: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    /// The url with which the component was launched.
    pub component_url: Option<String>,
    /// Monotonic time in nanos.
    pub timestamp: Timestamp,
}

/// The metadata contained in a `DiagnosticsData` object where the data source is
/// `DataSource::Logs`.
#[derive(Deserialize, Serialize, Clone, Debug, PartialEq)]
pub struct LogsMetadata {
    // TODO(fxbug.dev/58369) figure out exact spelling of pid/tid context and severity
    /// Optional vector of errors encountered by platform.
    pub errors: Option<Vec<LogError>>,

    /// The url with which the component was launched.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub component_url: Option<String>,

    /// Monotonic time in nanos.
    pub timestamp: Timestamp,

    /// Severity of the message.
    pub severity: Severity,

    /// Size of the original message on the wire, in bytes.
    pub size_bytes: usize,

    /// Tags to add at the beginning of the message
    pub tags: Option<Vec<String>>,

    /// The process ID
    pub pid: Option<u64>,

    /// The thread ID
    pub tid: Option<u64>,

    /// The file name
    pub file: Option<String>,

    /// The line number
    pub line: Option<u64>,

    /// Number of dropped messages
    pub dropped: Option<u64>,
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
    type Err = Error;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        let s = s.to_lowercase();
        match s.as_str() {
            "trace" => Ok(Severity::Trace),
            "debug" => Ok(Severity::Debug),
            "info" => Ok(Severity::Info),
            "warn" => Ok(Severity::Warn),
            "error" => Ok(Severity::Error),
            "fatal" => Ok(Severity::Fatal),
            other => Err(Error { message: format!("invalid severity: {}", other) }),
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

/// A diagnostics data object containing lifecycle event data.
pub type LifecycleData = Data<Lifecycle>;

/// A diagnostics data object containing logs data.
pub type LogsData = Data<Logs>;

/// A diagnostics data payload containing logs data.
pub type LogsHierarchy = DiagnosticsHierarchy<LogsField>;

/// A diagnostics hierarchy property keyed by `LogsField`.
pub type LogsProperty = Property<LogsField>;

impl Data<Lifecycle> {
    /// Creates a new data instance for a lifecycle event.
    pub fn for_lifecycle_event(
        moniker: impl Into<String>,
        lifecycle_event_type: LifecycleType,
        payload: Option<DiagnosticsHierarchy>,
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
                component_url: Some(component_url.into()),
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
        inspect_hierarchy: Option<DiagnosticsHierarchy>,
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
    /// Number of dropped messages
    dropped: Option<u64>,
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
    /// Size of the message encoded in wire format in bytes.
    /// This value does not actually correspond to memory usage
    /// in anything other than wire format and should not be used
    /// for memory accounting purposes.
    pub size_bytes: usize,
}

impl LogsDataBuilder {
    /// Constructs a new LogsDataBuilder
    pub fn new(args: BuilderArgs) -> Self {
        LogsDataBuilder {
            args,
            errors: vec![],
            msg: None,
            dropped: Some(0),
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
        self.dropped = Some(value);
        if value <= 0 {
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
            self.args.size_bytes,
            self.errors,
        );
        ret.metadata.dropped = self.dropped;
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
        timestamp_nanos: impl Into<Timestamp>,
        component_url: Option<String>,
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
                component_url: component_url,
                severity: severity.into(),
                size_bytes,
                errors,
                dropped: None,
                file: None,
                line: None,
                pid: None,
                tags: None,
                tid: None,
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

    pub fn payload_args(&self) -> Option<&DiagnosticsHierarchy<LogsField>> {
        self.payload
            .as_ref()
            .and_then(|p| p.children.iter().find(|property| property.name.as_str() == "keys"))
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
}

impl fmt::Display for Data<Logs> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        // Multiple tags are supported for the `LogMessage` format and are represented
        // as multiple instances of LogsField::Tag arguments.
        let kvps = self
            .payload_args()
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
        println!("{:?}", self.msg());
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
    MsgStructured,
    FilePath,
    LineNumber,
    Args,
    Format,
    Other(String),
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
            Self::Args => MESSAGE_LABEL,
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

impl Metadata {
    /// Returns the inspect metadata or None if the metadata contained is not for inspect.
    pub fn inspect(&self) -> Option<&InspectMetadata> {
        match self {
            Metadata::Inspect(m) => Some(m),
            _ => None,
        }
    }

    /// Returns the lifecycle event metadata or None if the metadata contained is not for a
    /// lifecycle event.
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
    use diagnostics_hierarchy::hierarchy;
    use pretty_assertions;
    use serde_json::json;

    const TEST_URL: &'static str = "fuchsia-pkg://test";

    #[test]
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
            123456i64,
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
    fn default_builder_test() {
        let builder = LogsDataBuilder::new(BuilderArgs {
            component_url: Some("url".to_string()),
            moniker: String::from("moniker"),
            severity: Severity::Info,
            size_bytes: 0,
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
            "errors": [],
            "component_url": "url",
              "dropped": 0,
              "errors": null,
              "file": null,
              "line": null,
              "pid": null,
              "severity": "INFO",
              "size_bytes": 0,
              "tags": [],
              "tid": null,

            "timestamp": 0,
          }
        });
        let result_json =
            serde_json::to_value(&builder.build()).expect("serialization should succeed.");
        pretty_assertions::assert_eq!(result_json, expected_json, "golden diff failed.");
    }

    #[test]
    fn regular_message_test() {
        let builder = LogsDataBuilder::new(BuilderArgs {
            component_url: Some("url".to_string()),
            moniker: String::from("moniker"),
            severity: Severity::Info,
            size_bytes: 0,
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
              "dropped": 2,
              "errors": [{"dropped_logs":{"count":2}}],
              "file": "test file.cc",
              "line": 420,
              "pid": 1001,
              "severity": "INFO",
              "size_bytes": 0,
              "tags": ["You're", "IT!"],
              "tid": 200,

            "timestamp": 0,
          }
        });
        let result_json =
            serde_json::to_value(&builder.build()).expect("serialization should succeed.");
        pretty_assertions::assert_eq!(result_json, expected_json, "golden diff failed.");
    }

    #[test]
    fn printf_test() {
        let builder = LogsDataBuilder::new(BuilderArgs {
            component_url: Some("url".to_string()),
            moniker: String::from("moniker"),
            severity: Severity::Info,
            size_bytes: 0,
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
              "dropped": 2,
              "errors": [{"dropped_logs":{"count":2}}],
              "file": "test file.cc",
              "line": 420,
              "pid": 1001,
              "severity": "INFO",
              "size_bytes": 0,
              "tags": ["You're", "IT!"],
              "tid": 200,

            "timestamp": 0,
          }
        });
        let result_json =
            serde_json::to_value(&builder.build()).expect("serialization should succeed.");
        pretty_assertions::assert_eq!(result_json, expected_json, "golden diff failed.");
    }

    #[test]
    fn test_canonical_json_lifecycle_event_formatting() {
        let json_schema = Data::for_lifecycle_event(
            "a/b/c/d",
            LifecycleType::DiagnosticsReady,
            None,
            TEST_URL,
            123456i64,
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
            123456i64,
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

    #[test]
    fn display_for_logs() {
        let data = LogsDataBuilder::new(BuilderArgs {
            timestamp_nanos: Timestamp::from(12345678000i64).into(),
            component_url: Some(String::from("fake-url")),
            moniker: String::from("moniker"),
            severity: Severity::Info,
            size_bytes: 1,
        })
        .set_pid(123)
        .set_tid(456)
        .set_message("some message".to_string())
        .set_file("some_file.cc".to_string())
        .set_line(420)
        .add_tag("foo")
        .add_tag("bar")
        .add_key(LogsProperty::String(LogsField::Other("test".to_string()), "property".to_string()))
        .build();

        assert_eq!(
            "[00012.345678][123][456][moniker][foo,bar] INFO: [some_file.cc(420)] some message test=property",
            format!("{}", data)
        )
    }

    #[test]
    fn display_for_logs_no_tags() {
        let data = LogsDataBuilder::new(BuilderArgs {
            timestamp_nanos: Timestamp::from(12345678000i64).into(),
            component_url: Some(String::from("fake-url")),
            moniker: String::from("moniker"),
            severity: Severity::Info,
            size_bytes: 1,
        })
        .set_pid(123)
        .set_tid(456)
        .set_message("some message".to_string())
        .build();

        assert_eq!("[00012.345678][123][456][moniker] INFO: some message", format!("{}", data))
    }
}

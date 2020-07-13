// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

use super::buffer::Accounted;
use super::error::StreamError;
use byteorder::{ByteOrder, LittleEndian};
use diagnostic_streams::{Severity as StreamSeverity, Value};
use fidl_fuchsia_logger::{LogLevelFilter, LogMessage};
use fuchsia_inspect_node_hierarchy::{NodeHierarchy, Property};
use fuchsia_zircon as zx;
use libc::{c_char, c_int};
use std::{convert::TryFrom, fmt::Write, iter::FromIterator, mem, str};

pub const METADATA_SIZE: usize = mem::size_of::<fx_log_metadata_t>();
pub const MIN_PACKET_SIZE: usize = METADATA_SIZE + 1;

pub const MAX_DATAGRAM_LEN: usize = 2032;
pub const MAX_TAGS: usize = 5;
pub const MAX_TAG_LEN: usize = 64;

const PID_LABEL: &str = "__pid";
const TID_LABEL: &str = "__tid";
const DROPPED_LABEL: &str = "__dropped";
const TAG_LABEL: &str = "__tag";
const MESSAGE_LABEL: &str = "__msg";

/// An enum containing well known argument names passed through logs, as well
/// as an `Other` variant for any other argument names.
///
/// This contains the fields of logs sent as a [`LogMessage`].
///
/// [`LogMessage`]: https://fuchsia.dev/reference/fidl/fuchsia.logger#LogMessage
// TODO(53222) - rename enum to avoid clashes with MESSAGE_LABEL above.
#[derive(Debug, PartialEq, Clone)]
pub enum Field {
    ProcessId,
    ThreadId,
    Dropped,
    Tag,
    Msg,
    Other(String),
}

impl Field {
    fn is_legacy(&self) -> bool {
        matches!(
            self,
            Field::ProcessId | Field::ThreadId | Field::Dropped | Field::Tag | Field::Msg
        )
    }
}

impl AsRef<str> for Field {
    fn as_ref(&self) -> &str {
        match self {
            // TODO(50519) - ensure that strings reported here align with naming
            // decisions made for the structured log format sent by other components.
            Self::ProcessId => PID_LABEL,
            Self::ThreadId => TID_LABEL,
            Self::Dropped => DROPPED_LABEL,
            Self::Tag => TAG_LABEL,
            Self::Msg => MESSAGE_LABEL,
            Self::Other(str) => str.as_str(),
        }
    }
}

impl From<String> for Field {
    fn from(s: String) -> Self {
        match s.as_str() {
            PID_LABEL => Self::ProcessId,
            TID_LABEL => Self::ThreadId,
            DROPPED_LABEL => Self::Dropped,
            TAG_LABEL => Self::Tag,
            MESSAGE_LABEL => Self::Msg,
            _ => Self::Other(s),
        }
    }
}

/// A representation of a structured log's payload.
pub type LogHierarchy = NodeHierarchy<Field>;
/// A representation of a property in a structured log.
pub type LogProperty = Property<Field>;

/// An internal representation of a structured log.
#[derive(Clone, Debug, PartialEq)]
pub struct Message {
    /// Size this message took up on the wire.
    pub size: usize,

    /// Message severity reported by the writer.
    pub severity: Severity,

    /// Timestamp reported by the writer.
    pub time: zx::Time,

    /// Payload of the structured log. Until the structured log encoding supports
    /// nested structures, the contents consist of a single LogHierarchy with
    /// properties and no children.
    pub contents: LogHierarchy,
}

impl Accounted for Message {
    fn bytes_used(&self) -> usize {
        self.size
    }
}

impl Message {
    /// Parse the provided buffer as if it implements the [logger/syslog wire format].
    ///
    /// Note that this is distinct from the parsing we perform for the debuglog log, which also
    /// takes a `&[u8]` and is why we don't implement this as `TryFrom`.
    ///
    /// [logger/syslog wire format]: https://fuchsia.googlesource.com/fuchsia/+/master/zircon/system/ulib/syslog/include/lib/syslog/wire_format.h
    pub(super) fn from_logger(bytes: &[u8]) -> Result<Self, StreamError> {
        if bytes.len() < MIN_PACKET_SIZE {
            return Err(StreamError::ShortRead { len: bytes.len() });
        }

        let terminator = bytes[bytes.len() - 1];
        if terminator != 0 {
            return Err(StreamError::NotNullTerminated { terminator });
        }

        let pid = LittleEndian::read_u64(&bytes[..8]);
        let tid = LittleEndian::read_u64(&bytes[8..16]);
        let time = zx::Time::from_nanos(LittleEndian::read_i64(&bytes[16..24]));

        let raw_severity = LittleEndian::read_i32(&bytes[24..28]);
        let severity = Severity::try_from(raw_severity)?;

        let dropped_logs = LittleEndian::read_u32(&bytes[28..METADATA_SIZE]) as usize;

        // start reading tags after the header
        let mut cursor = METADATA_SIZE;
        let mut tag_len = bytes[cursor] as usize;
        let mut tags = Vec::new();
        while tag_len != 0 {
            if tags.len() == MAX_TAGS {
                return Err(StreamError::TooManyTags);
            }

            if tag_len > MAX_TAG_LEN - 1 {
                return Err(StreamError::TagTooLong { index: tags.len(), len: tag_len });
            }

            if (cursor + tag_len + 1) > bytes.len() {
                return Err(StreamError::OutOfBounds);
            }

            let tag_start = cursor + 1;
            let tag_end = tag_start + tag_len;
            let tag = str::from_utf8(&bytes[tag_start..tag_end])?;
            tags.push(tag.to_owned());

            cursor = tag_end;
            tag_len = bytes[cursor] as usize;
        }

        let msg_start = cursor + 1;
        let mut msg_end = cursor + 1;
        while msg_end < bytes.len() {
            if bytes[msg_end] == 0 {
                let message = str::from_utf8(&bytes[msg_start..msg_end])?.to_owned();
                let message_len = message.len();
                let mut contents = LogHierarchy::new(
                    "root",
                    vec![
                        LogProperty::Uint(Field::ProcessId, pid),
                        LogProperty::Uint(Field::ThreadId, tid),
                        LogProperty::Uint(Field::Dropped, dropped_logs as u64),
                        LogProperty::String(Field::Msg, message),
                    ],
                    vec![],
                );
                for tag in tags {
                    contents.add_property(vec!["root"], LogProperty::String(Field::Tag, tag));
                }

                return Ok(Self { size: cursor + message_len + 1, time, severity, contents });
            }
            msg_end += 1;
        }

        Err(StreamError::OutOfBounds)
    }

    /// Constructs a `Message` from the provided bytes, assuming the bytes
    /// are in the format specified as in the [log encoding].
    ///
    /// [log encoding] https://fuchsia.dev/fuchsia-src/development/logs/encodings
    pub fn from_structured(bytes: &[u8]) -> Result<Self, StreamError> {
        let (record, _) = diagnostic_streams::parse::parse_record(bytes)?;
        let properties = Result::from_iter(record.arguments.into_iter().map(|a| {
            let label = Field::from(a.name);

            match a.value {
                Value::SignedInt(v) => Ok(LogProperty::Int(label, v)),
                Value::UnsignedInt(v) => Ok(LogProperty::Uint(label, v)),
                Value::Floating(v) => Ok(LogProperty::Double(label, v)),
                Value::Text(v) => Ok(LogProperty::String(label, v)),
                Value::__UnknownVariant { .. } => Err(StreamError::UnrecognizedValue),
            }
        }))?;
        Ok(Self {
            size: bytes.len(),
            time: zx::Time::from_nanos(record.timestamp),
            severity: record.severity.into(),
            contents: LogHierarchy::new("root", properties, vec![]),
        })
    }

    /// Returns the pid associated with the message, if one exists.
    pub fn pid(&self) -> Option<u64> {
        self.contents.properties.iter().find_map(|property| match property {
            LogProperty::Uint(Field::ProcessId, pid) => Some(*pid),
            _ => None,
        })
    }

    /// Returns the tid associated with the message, if one exists.
    pub fn tid(&self) -> Option<u64> {
        self.contents.properties.iter().find_map(|property| match property {
            LogProperty::Uint(Field::ThreadId, tid) => Some(*tid),
            _ => None,
        })
    }

    /// Returns any tags associated with the message.
    pub fn tags(&self) -> impl Iterator<Item = &str> {
        // Multiple tags are supported for the `LogMessage` format and are represented
        // as multiple instances of Field::Tag arguments.
        self.contents.properties.iter().filter_map(|property| match property {
            LogProperty::String(Field::Tag, tag) => Some(tag.as_str()),
            _ => None,
        })
    }

    /// Returns the string log associated with the message, if one exists.
    pub fn msg(&self) -> Option<&str> {
        self.contents.properties.iter().find_map(|property| match property {
            LogProperty::String(Field::Msg, msg) => Some(msg.as_str()),
            _ => None,
        })
    }

    /// Returns number of dropped logs if reported in the message.
    pub fn dropped_logs(&self) -> Option<u64> {
        self.contents.properties.iter().find_map(|property| match property {
            LogProperty::Uint(Field::Dropped, dropped) => Some(*dropped),
            _ => None,
        })
    }

    fn non_legacy_contents(&self) -> impl Iterator<Item = &LogProperty> {
        self.contents.properties.iter().filter(|prop| !prop.key().is_legacy())
    }

    /// Convert this `Message` to a FIDL representation suitable for sending to `LogListenerSafe`.
    pub fn for_listener(&self) -> LogMessage {
        let mut msg = self.msg().unwrap_or("").to_string();

        for property in self.non_legacy_contents() {
            write!(&mut msg, " {}", property).expect("allocations have to fail for this to fail");
        }

        LogMessage {
            pid: self.pid().unwrap_or(zx::sys::ZX_KOID_INVALID),
            tid: self.tid().unwrap_or(zx::sys::ZX_KOID_INVALID),
            time: self.time.into_nanos(),
            severity: self.severity.for_listener(),
            dropped_logs: self.dropped_logs().unwrap_or(0) as _,
            tags: self.tags().map(String::from).collect(),
            msg,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, Ord, PartialEq, PartialOrd)]
#[repr(i8)]
pub enum Severity {
    Trace,
    Debug,
    Verbose(i8),
    Info,
    Warn,
    Error,
    Fatal,
}

impl Severity {
    pub fn for_listener(self) -> fx_log_severity_t {
        match self {
            Severity::Trace => LogLevelFilter::Trace as _,
            Severity::Debug => LogLevelFilter::Debug as _,
            Severity::Info => LogLevelFilter::Info as _,
            Severity::Warn => LogLevelFilter::Warn as _,
            Severity::Error => LogLevelFilter::Error as _,
            Severity::Fatal => LogLevelFilter::Fatal as _,
            Severity::Verbose(v) => (LogLevelFilter::Info as i8 - v) as _,
        }
    }
}

impl From<StreamSeverity> for Severity {
    fn from(fidl_severity: StreamSeverity) -> Self {
        match fidl_severity {
            StreamSeverity::Trace => Self::Trace,
            StreamSeverity::Debug => Self::Debug,
            StreamSeverity::Info => Self::Info,
            StreamSeverity::Warn => Self::Warn,
            StreamSeverity::Error => Self::Error,
            StreamSeverity::Fatal => Self::Fatal,
        }
    }
}

impl TryFrom<fx_log_severity_t> for Severity {
    type Error = StreamError;

    fn try_from(raw: fx_log_severity_t) -> Result<Self, StreamError> {
        // Handle legacy/deprecated level filter values.
        if -10 <= raw && raw <= -3 {
            Ok(Severity::Verbose(-raw as i8))
        } else if raw == -2 {
            // legacy values from trace verbosity
            Ok(Severity::Trace)
        } else if raw == -1 {
            // legacy value from debug verbosity
            Ok(Severity::Debug)
        } else if raw == 0 {
            // legacy value for INFO
            Ok(Severity::Info)
        } else if raw == 1 {
            // legacy value for WARNING
            Ok(Severity::Warn)
        } else if raw == 2 {
            // legacy value for ERROR
            Ok(Severity::Error)
        } else if raw < LogLevelFilter::Info as i32 && raw > LogLevelFilter::Debug as i32 {
            // Verbosity scale exists as incremental steps between INFO & DEBUG
            Ok(Severity::Verbose(LogLevelFilter::Info as i8 - raw as i8))
        } else if let Some(level) = LogLevelFilter::from_primitive(raw as i8) {
            // Handle current level filter values.
            match level {
                // Match defined severities at their given filter level.
                LogLevelFilter::Trace => Ok(Severity::Trace),
                LogLevelFilter::Debug => Ok(Severity::Debug),
                LogLevelFilter::Info => Ok(Severity::Info),
                LogLevelFilter::Warn => Ok(Severity::Warn),
                LogLevelFilter::Error => Ok(Severity::Error),
                LogLevelFilter::Fatal => Ok(Severity::Fatal),
                _ => Err(StreamError::InvalidSeverity { provided: raw }),
            }
        } else {
            Err(StreamError::InvalidSeverity { provided: raw })
        }
    }
}

#[allow(non_camel_case_types)]
pub(super) type fx_log_severity_t = c_int;

#[repr(C)]
#[derive(Debug, Copy, Clone, Default, Eq, PartialEq)]
pub struct fx_log_metadata_t {
    pub pid: zx::sys::zx_koid_t,
    pub tid: zx::sys::zx_koid_t,
    pub time: zx::sys::zx_time_t,
    pub severity: fx_log_severity_t,
    pub dropped_logs: u32,
}

#[repr(C)]
#[derive(Clone)]
pub struct fx_log_packet_t {
    pub metadata: fx_log_metadata_t,
    // Contains concatenated tags and message and a null terminating character at
    // the end.
    // char(tag_len) + "tag1" + char(tag_len) + "tag2\0msg\0"
    pub data: [c_char; MAX_DATAGRAM_LEN - METADATA_SIZE],
}

impl Default for fx_log_packet_t {
    fn default() -> fx_log_packet_t {
        fx_log_packet_t {
            data: [0; MAX_DATAGRAM_LEN - METADATA_SIZE],
            metadata: Default::default(),
        }
    }
}

impl fx_log_packet_t {
    /// This struct has no padding bytes, but we can't use zerocopy because it needs const
    /// generics to support arrays this large.
    pub fn as_bytes(&self) -> &[u8] {
        unsafe {
            std::slice::from_raw_parts(
                (self as *const Self) as *const u8,
                mem::size_of::<fx_log_packet_t>(),
            )
        }
    }

    /// Fills data with a single value for defined region.
    pub fn fill_data(&mut self, region: std::ops::Range<usize>, with: c_char) {
        self.data[region].iter_mut().for_each(|c| *c = with);
    }

    /// Copies bytes to data at specifies offset.
    pub fn add_data<T: std::convert::TryInto<c_char> + Copy>(&mut self, offset: usize, bytes: &[T])
    where
        <T as std::convert::TryInto<c_char>>::Error: std::fmt::Debug,
    {
        self.data[offset..(offset + bytes.len())]
            .iter_mut()
            .enumerate()
            .for_each(|(i, x)| *x = bytes[i].try_into().unwrap());
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use diagnostic_streams::{encode::Encoder, Argument, Record};
    use std::io::Cursor;

    #[repr(C, packed)]
    pub struct fx_log_metadata_t_packed {
        pub pid: zx::sys::zx_koid_t,
        pub tid: zx::sys::zx_koid_t,
        pub time: zx::sys::zx_time_t,
        pub severity: fx_log_severity_t,
        pub dropped_logs: u32,
    }

    #[repr(C, packed)]
    pub struct fx_log_packet_t_packed {
        pub metadata: fx_log_metadata_t_packed,
        /// Contains concatenated tags and message and a null terminating character at the end.
        /// `char(tag_len) + "tag1" + char(tag_len) + "tag2\0msg\0"`
        pub data: [c_char; MAX_DATAGRAM_LEN - METADATA_SIZE],
    }

    #[test]
    fn abi_test() {
        assert_eq!(METADATA_SIZE, 32);
        assert_eq!(MAX_TAGS, 5);
        assert_eq!(MAX_TAG_LEN, 64);
        assert_eq!(mem::size_of::<fx_log_packet_t>(), MAX_DATAGRAM_LEN);

        // Test that there is no padding
        assert_eq!(mem::size_of::<fx_log_packet_t>(), mem::size_of::<fx_log_packet_t_packed>());

        assert_eq!(mem::size_of::<fx_log_metadata_t>(), mem::size_of::<fx_log_metadata_t_packed>());
    }
    fn test_packet() -> fx_log_packet_t {
        let mut packet: fx_log_packet_t = Default::default();
        packet.metadata.pid = 1;
        packet.metadata.tid = 2;
        packet.metadata.time = 3;
        packet.metadata.severity = LogLevelFilter::Debug as i32;
        packet.metadata.dropped_logs = 10;
        packet
    }

    #[test]
    fn short_reads() {
        let packet = test_packet();
        let one_short = &packet.as_bytes()[..METADATA_SIZE];
        let two_short = &packet.as_bytes()[..METADATA_SIZE - 1];

        assert_eq!(Message::from_logger(one_short), Err(StreamError::ShortRead { len: 32 }));

        assert_eq!(Message::from_logger(two_short), Err(StreamError::ShortRead { len: 31 }));
    }

    #[test]
    fn unterminated() {
        let mut packet = test_packet();
        let end = 9;
        packet.data[end] = 1;

        let buffer = &packet.as_bytes()[..MIN_PACKET_SIZE + end];
        let parsed = Message::from_logger(buffer);

        assert_eq!(parsed, Err(StreamError::NotNullTerminated { terminator: 1 }));
    }

    #[test]
    fn tags_no_message() {
        let mut packet = test_packet();
        let end = 12;
        packet.data[0] = end as c_char - 1;
        packet.fill_data(1..end, 'A' as _);
        packet.data[end] = 0;

        let buffer = &packet.as_bytes()[..MIN_PACKET_SIZE + end]; // omit null-terminated
        let parsed = Message::from_logger(buffer);

        assert_eq!(parsed, Err(StreamError::OutOfBounds));
    }

    #[test]
    fn tags_with_message() {
        let mut packet = test_packet();
        let a_start = 1;
        let a_count = 11;
        let a_end = a_start + a_count;

        packet.data[0] = a_count as c_char;
        packet.fill_data(a_start..a_end, 'A' as _);
        packet.data[a_end] = 0; // terminate tags

        let b_start = a_start + a_count + 1;
        let b_count = 5;
        let b_end = b_start + b_count;
        packet.fill_data(b_start..b_end, 'B' as _);

        let data_size = b_start + b_count;

        let buffer = &packet.as_bytes()[..METADATA_SIZE + data_size + 1]; // null-terminate message
        let mut parsed = Message::from_logger(buffer).unwrap();
        let mut expected = Message {
            size: METADATA_SIZE + data_size,
            time: zx::Time::from_nanos(packet.metadata.time),
            severity: Severity::Debug,
            contents: LogHierarchy::new(
                "root",
                vec![
                    LogProperty::Uint(Field::ProcessId, packet.metadata.pid),
                    LogProperty::Uint(Field::ThreadId, packet.metadata.tid),
                    LogProperty::Uint(Field::Dropped, packet.metadata.dropped_logs as _),
                    LogProperty::String(Field::Tag, "AAAAAAAAAAA".into()),
                    LogProperty::String(Field::Msg, "BBBBB".into()),
                ],
                vec![],
            ),
        };

        parsed.contents.sort();
        expected.contents.sort();

        assert_eq!(parsed, expected);
    }

    #[test]
    fn two_tags_no_message() {
        let mut packet = test_packet();
        let a_start = 1;
        let a_count = 11;
        let a_end = a_start + a_count;

        packet.data[0] = a_count as c_char;
        packet.fill_data(a_start..a_end, 'A' as _);

        let b_start = a_end + 1;
        let b_count = 5;
        let b_end = b_start + b_count;

        packet.data[a_end] = b_count as c_char;
        packet.fill_data(b_start..b_end, 'B' as _);

        let buffer = &packet.as_bytes()[..MIN_PACKET_SIZE + b_end];
        let parsed = Message::from_logger(buffer);

        assert_eq!(parsed, Err(StreamError::OutOfBounds));
    }

    #[test]
    fn two_tags_with_message() {
        let mut packet = test_packet();
        let a_start = 1;
        let a_count = 11;
        let a_end = a_start + a_count;

        packet.data[0] = a_count as c_char;
        packet.fill_data(a_start..a_end, 'A' as _);

        let b_start = a_end + 1;
        let b_count = 5;
        let b_end = b_start + b_count;

        packet.data[a_end] = b_count as c_char;
        packet.fill_data(b_start..b_end, 'B' as _);

        let c_start = b_end + 1;
        let c_count = 5;
        let c_end = c_start + c_count;
        packet.fill_data(c_start..c_end, 'C' as _);

        let data_size = c_start + c_count;

        let buffer = &packet.as_bytes()[..METADATA_SIZE + data_size + 1]; // null-terminated
        let mut parsed = Message::from_logger(buffer).unwrap();
        let mut expected = Message {
            size: METADATA_SIZE + data_size,
            time: zx::Time::from_nanos(packet.metadata.time),
            severity: Severity::Debug,
            contents: LogHierarchy::new(
                "root",
                vec![
                    LogProperty::Uint(Field::ProcessId, packet.metadata.pid),
                    LogProperty::Uint(Field::ThreadId, packet.metadata.tid),
                    LogProperty::Uint(Field::Dropped, packet.metadata.dropped_logs as _),
                    LogProperty::String(Field::Tag, "AAAAAAAAAAA".to_string()),
                    LogProperty::String(Field::Tag, "BBBBB".to_string()),
                    LogProperty::String(Field::Msg, "CCCCC".to_string()),
                ],
                vec![],
            ),
        };
        parsed.contents.sort();
        expected.contents.sort();

        assert_eq!(parsed, expected);
    }

    #[test]
    fn max_tags_with_message() {
        let mut packet = test_packet();

        let tags_start = 1;
        let tag_len = 2;
        let tag_size = tag_len + 1; // the length-prefix byte
        for tag_num in 0..MAX_TAGS {
            let start = tags_start + (tag_size * tag_num);
            let end = start + tag_len;

            packet.data[start - 1] = tag_len as c_char;
            let ascii = 'A' as c_char + tag_num as c_char;
            packet.fill_data(start..end, ascii);
        }

        let msg_start = tags_start + (tag_size * MAX_TAGS);
        let msg_len = 5;
        let msg_end = msg_start + msg_len;
        let msg_ascii = 'A' as c_char + MAX_TAGS as c_char;
        packet.fill_data(msg_start..msg_end, msg_ascii);

        let min_buffer = &packet.as_bytes()[..METADATA_SIZE + msg_end + 1]; // null-terminated
        let full_buffer = &packet.as_bytes()[..];

        let mut min_parsed = Message::from_logger(min_buffer).unwrap();
        let mut full_parsed = Message::from_logger(full_buffer).unwrap();

        let mut expected_properties = vec![
            LogProperty::Uint(Field::ProcessId, packet.metadata.pid),
            LogProperty::Uint(Field::ThreadId, packet.metadata.tid),
            LogProperty::Uint(Field::Dropped, packet.metadata.dropped_logs as _),
            LogProperty::String(
                Field::Msg,
                String::from_utf8(vec![msg_ascii as u8; msg_len]).unwrap(),
            ),
        ];
        let mut tag_properties = (0..MAX_TAGS as _)
            .map(|tag_num| {
                LogProperty::String(
                    Field::Tag,
                    String::from_utf8(vec![('A' as c_char + tag_num) as u8; tag_len]).unwrap(),
                )
            })
            .collect::<Vec<LogProperty>>();
        expected_properties.append(&mut tag_properties);
        let mut expected_contents = LogHierarchy::new("root", expected_properties, vec![]);

        // sort hierarchies so we can do direct comparison
        expected_contents.sort();
        min_parsed.contents.sort();
        full_parsed.contents.sort();

        let expected_message = Message {
            size: METADATA_SIZE + msg_end,
            time: zx::Time::from_nanos(packet.metadata.time),
            severity: Severity::Debug,
            contents: expected_contents,
        };

        assert_eq!(min_parsed, expected_message);
        assert_eq!(full_parsed, expected_message);
    }

    #[test]
    fn max_tags() {
        let mut packet = test_packet();
        let tags_start = 1;
        let tag_len = 2;
        let tag_size = tag_len + 1; // the length-prefix byte
        for tag_num in 0..MAX_TAGS {
            let start = tags_start + (tag_size * tag_num);
            let end = start + tag_len;

            packet.data[start - 1] = tag_len as c_char;
            let ascii = 'A' as c_char + tag_num as c_char;
            packet.fill_data(start..end, ascii);
        }

        let msg_start = tags_start + (tag_size * MAX_TAGS);

        let buffer_missing_terminator = &packet.as_bytes()[..METADATA_SIZE + msg_start];
        assert_eq!(
            Message::from_logger(buffer_missing_terminator),
            Err(StreamError::OutOfBounds),
            "can't parse an empty message without a nul terminator"
        );

        let buffer = &packet.as_bytes()[..METADATA_SIZE + msg_start + 1]; // null-terminated
        let mut parsed = Message::from_logger(buffer).unwrap();
        parsed.contents.sort();

        let mut expected_contents = LogHierarchy::new(
            "root",
            vec![
                LogProperty::Uint(Field::ProcessId, packet.metadata.pid),
                LogProperty::Uint(Field::ThreadId, packet.metadata.tid),
                LogProperty::Uint(Field::Dropped, packet.metadata.dropped_logs as _),
                LogProperty::String(Field::Msg, "".to_string()),
            ],
            vec![],
        );
        for tag_num in 0..MAX_TAGS as _ {
            expected_contents.add_property(
                vec!["root"],
                LogProperty::String(
                    Field::Tag,
                    String::from_utf8(vec![('A' as c_char + tag_num) as u8; 2]).unwrap(),
                ),
            );
        }
        expected_contents.sort();

        assert_eq!(
            parsed,
            Message {
                size: METADATA_SIZE + msg_start,
                time: zx::Time::from_nanos(packet.metadata.time),
                severity: Severity::Debug,
                contents: expected_contents
            }
        );
    }

    #[test]
    fn no_tags_with_message() {
        let mut packet = test_packet();
        packet.data[0] = 0;
        packet.data[1] = 'A' as _;
        packet.data[2] = 'A' as _; // measured size ends here
        packet.data[3] = 0;

        let buffer = &packet.as_bytes()[..METADATA_SIZE + 4]; // 0 tag size + 2 byte message + null
        let parsed = Message::from_logger(buffer).unwrap();

        assert_eq!(
            parsed,
            Message {
                size: METADATA_SIZE + 3,
                time: zx::Time::from_nanos(packet.metadata.time),
                severity: Severity::Debug,
                contents: LogHierarchy::new(
                    "root",
                    vec![
                        LogProperty::Uint(Field::ProcessId, packet.metadata.pid),
                        LogProperty::Uint(Field::ThreadId, packet.metadata.tid),
                        LogProperty::Uint(Field::Dropped, packet.metadata.dropped_logs as _),
                        LogProperty::String(Field::Msg, "AA".to_string())
                    ],
                    vec![],
                ),
            }
        );
    }

    #[test]
    fn message_severity() {
        let mut packet = test_packet();
        packet.metadata.severity = LogLevelFilter::Info as i32;
        packet.data[0] = 0; // tag size
        packet.data[1] = 0; // null terminated

        let mut buffer = &packet.as_bytes()[..METADATA_SIZE + 2]; // tag size + null
        let mut parsed = Message::from_logger(buffer).unwrap();
        let mut expected_message = Message {
            size: METADATA_SIZE + 1,
            time: zx::Time::from_nanos(packet.metadata.time),
            severity: Severity::Info,
            contents: LogHierarchy::new(
                "root",
                vec![
                    LogProperty::Uint(Field::ProcessId, packet.metadata.pid),
                    LogProperty::Uint(Field::ThreadId, packet.metadata.tid),
                    LogProperty::Uint(Field::Dropped, packet.metadata.dropped_logs as _),
                    LogProperty::String(Field::Msg, "".to_string()),
                ],
                vec![],
            ),
        };

        assert_eq!(parsed, expected_message);

        packet.metadata.severity = LogLevelFilter::Trace as i32;
        buffer = &packet.as_bytes()[..METADATA_SIZE + 2];
        parsed = Message::from_logger(buffer).unwrap();
        expected_message.severity = Severity::Trace;

        assert_eq!(parsed, expected_message);

        packet.metadata.severity = LogLevelFilter::Debug as i32;
        buffer = &packet.as_bytes()[..METADATA_SIZE + 2];
        parsed = Message::from_logger(buffer).unwrap();
        expected_message.severity = Severity::Debug;

        assert_eq!(parsed, expected_message);

        packet.metadata.severity = LogLevelFilter::Warn as i32;
        buffer = &packet.as_bytes()[..METADATA_SIZE + 2];
        parsed = Message::from_logger(buffer).unwrap();
        expected_message.severity = Severity::Warn;

        assert_eq!(parsed, expected_message);

        packet.metadata.severity = LogLevelFilter::Error as i32;
        buffer = &packet.as_bytes()[..METADATA_SIZE + 2];
        parsed = Message::from_logger(buffer).unwrap();
        expected_message.severity = Severity::Error;

        assert_eq!(parsed, expected_message);
    }

    #[test]
    fn legacy_message_severity() {
        let mut packet = test_packet();
        // legacy verbosity where v=10
        packet.metadata.severity = LogLevelFilter::Info as i32 - 10;
        packet.data[0] = 0; // tag size
        packet.data[1] = 0; // null terminated

        let mut buffer = &packet.as_bytes()[..METADATA_SIZE + 2]; // tag size + null
        let mut parsed = Message::from_logger(buffer).unwrap();
        let mut expected_message = Message {
            size: METADATA_SIZE + 1,
            time: zx::Time::from_nanos(packet.metadata.time),
            severity: Severity::Verbose(10),
            contents: LogHierarchy::new(
                "root",
                vec![
                    LogProperty::Uint(Field::ProcessId, packet.metadata.pid),
                    LogProperty::Uint(Field::ThreadId, packet.metadata.tid),
                    LogProperty::Uint(Field::Dropped, packet.metadata.dropped_logs as _),
                    LogProperty::String(Field::Msg, "".to_string()),
                ],
                vec![],
            ),
        };

        assert_eq!(parsed, expected_message);

        // legacy verbosity where v=2
        packet.metadata.severity = LogLevelFilter::Info as i32 - 2;
        buffer = &packet.as_bytes()[..METADATA_SIZE + 2];
        parsed = Message::from_logger(buffer).unwrap();
        expected_message.severity = Severity::Verbose(2);

        assert_eq!(parsed, expected_message);

        // legacy verbosity where v=1
        packet.metadata.severity = LogLevelFilter::Info as i32 - 1;
        buffer = &packet.as_bytes()[..METADATA_SIZE + 2];
        parsed = Message::from_logger(buffer).unwrap();
        expected_message.severity = Severity::Verbose(1);

        assert_eq!(parsed, expected_message);

        packet.metadata.severity = 0; // legacy severity
        buffer = &packet.as_bytes()[..METADATA_SIZE + 2];
        parsed = Message::from_logger(buffer).unwrap();
        expected_message.severity = Severity::Info;

        assert_eq!(parsed, expected_message);

        packet.metadata.severity = 1; // legacy severity
        buffer = &packet.as_bytes()[..METADATA_SIZE + 2];
        parsed = Message::from_logger(buffer).unwrap();
        expected_message.severity = Severity::Warn;

        assert_eq!(parsed, expected_message);
    }

    #[test]
    fn test_from_structured() {
        let record = Record {
            timestamp: 72,
            severity: StreamSeverity::Error,
            arguments: vec![
                Argument { name: "arg1".to_string(), value: Value::SignedInt(-23) },
                Argument { name: PID_LABEL.to_string(), value: Value::UnsignedInt(43) },
                Argument { name: TID_LABEL.to_string(), value: Value::UnsignedInt(912) },
                Argument { name: DROPPED_LABEL.to_string(), value: Value::UnsignedInt(2) },
                Argument { name: TAG_LABEL.to_string(), value: Value::Text("tag".to_string()) },
                Argument { name: MESSAGE_LABEL.to_string(), value: Value::Text("msg".to_string()) },
            ],
        };

        let mut buffer = Cursor::new(vec![0u8; MAX_DATAGRAM_LEN]);
        let mut encoder = Encoder::new(&mut buffer);
        encoder.write_record(&record).unwrap();
        let encoded = &buffer.get_ref().as_slice()[..buffer.position() as usize];
        let parsed = Message::from_structured(encoded).unwrap();
        assert_eq!(
            parsed,
            Message {
                size: encoded.len(),
                time: zx::Time::from_nanos(72),
                severity: Severity::Error,
                contents: LogHierarchy::new(
                    "root",
                    vec![
                        LogProperty::Int(Field::Other("arg1".to_string()), -23),
                        LogProperty::Uint(Field::ProcessId, 43),
                        LogProperty::Uint(Field::ThreadId, 912),
                        LogProperty::Uint(Field::Dropped, 2),
                        LogProperty::String(Field::Tag, "tag".to_string()),
                        LogProperty::String(Field::Msg, "msg".to_string()),
                    ],
                    vec![],
                )
            }
        );
        assert_eq!(
            parsed.for_listener(),
            LogMessage {
                severity: Severity::Error.for_listener(),
                time: 72,
                dropped_logs: 2,
                pid: 43,
                tid: 912,
                msg: "msg arg1=-23".into(),
                tags: vec!["tag".into()]
            }
        );

        // multiple tags
        let record = Record {
            timestamp: 72,
            severity: StreamSeverity::Error,
            arguments: vec![
                Argument { name: TAG_LABEL.to_string(), value: Value::Text("tag1".to_string()) },
                Argument { name: TAG_LABEL.to_string(), value: Value::Text("tag2".to_string()) },
                Argument { name: TAG_LABEL.to_string(), value: Value::Text("tag3".to_string()) },
            ],
        };
        let mut buffer = Cursor::new(vec![0u8; MAX_DATAGRAM_LEN]);
        let mut encoder = Encoder::new(&mut buffer);
        encoder.write_record(&record).unwrap();
        let encoded = &buffer.get_ref().as_slice()[..buffer.position() as usize];
        let parsed = Message::from_structured(encoded).unwrap();
        assert_eq!(
            parsed,
            Message {
                size: encoded.len(),
                time: zx::Time::from_nanos(72),
                severity: Severity::Error,
                contents: LogHierarchy::new(
                    "root",
                    vec![
                        LogProperty::String(Field::Tag, "tag1".to_string()),
                        LogProperty::String(Field::Tag, "tag2".to_string()),
                        LogProperty::String(Field::Tag, "tag3".to_string()),
                    ],
                    vec![],
                )
            }
        );

        // empty record
        let record = Record { timestamp: 72, severity: StreamSeverity::Error, arguments: vec![] };
        let mut buffer = Cursor::new(vec![0u8; MAX_DATAGRAM_LEN]);
        let mut encoder = Encoder::new(&mut buffer);
        encoder.write_record(&record).unwrap();
        let encoded = &buffer.get_ref().as_slice()[..buffer.position() as usize];
        let parsed = Message::from_structured(encoded).unwrap();
        assert_eq!(
            parsed,
            Message {
                size: encoded.len(),
                time: zx::Time::from_nanos(72),
                severity: Severity::Error,
                contents: LogHierarchy::new("root", vec![], vec![],)
            }
        );

        // parse error
        assert!(matches!(
            Message::from_structured(&vec![]).unwrap_err(),
            StreamError::ParseError { .. }
        ));
    }
}

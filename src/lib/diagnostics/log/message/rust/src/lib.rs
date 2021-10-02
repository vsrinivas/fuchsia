// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::error::MessageError;
use byteorder::{ByteOrder, LittleEndian};
use diagnostics_data::{
    BuilderArgs, LegacySeverity, LogError, LogsData, LogsDataBuilder, LogsField, LogsProperty,
    Severity,
};
use diagnostics_log_encoding::{Value, ValueUnknown};
use fuchsia_syslog::COMPONENT_NAME_PLACEHOLDER_TAG;
use fuchsia_zircon as zx;
use libc::{c_char, c_int};
use serde::Serialize;
use std::{convert::TryFrom, mem, str};

mod constants;
pub mod error;

pub use constants::*;

#[cfg(test)]
mod test;

// State machine used for parsing a record.
enum StateMachine {
    // Initial state (Printf, ModifiedNormal)
    Init,
    // Regular parsing case (no special mode such as printf)
    RegularArgs,
    // Modified parsing case (may switch to printf args)
    NestedRegularArgs,
    // Inside printf args (may switch to RegularArgs)
    PrintfArgs,
}

#[derive(Clone, Serialize)]
pub struct MonikerWithUrl {
    pub moniker: String,
    pub url: String,
}

/// Transforms the given legacy log message (already parsed) into a `LogsData` containing the
/// given identity information.
pub fn from_logger(source: MonikerWithUrl, msg: LoggerMessage) -> LogsData {
    let mut builder = LogsDataBuilder::new(BuilderArgs {
        timestamp_nanos: msg.timestamp.into(),
        component_url: Some(source.url),
        moniker: source.moniker.clone(),
        severity: msg.severity,
        size_bytes: msg.size_bytes,
    })
    .set_pid(msg.pid)
    .set_tid(msg.tid)
    .set_dropped(msg.dropped_logs)
    .set_message(msg.message);
    if msg.include_moniker_tag {
        builder = builder.add_tag(source.moniker);
    }
    for tag in &msg.tags {
        builder = builder.add_tag(tag.as_ref());
    }
    let mut result = builder.build();
    if let Some(verbosity) = msg.verbosity {
        result.set_legacy_verbosity(verbosity);
    }
    result
}

/// Returns a new `LogsData` which encodes a count of dropped messages in its metadata.
pub fn for_dropped(count: u64, source: MonikerWithUrl, timestamp: i64) -> LogsData {
    let message = format!("Rolled {} logs out of buffer", count);
    LogsDataBuilder::new(BuilderArgs {
        timestamp_nanos: timestamp.into(),
        component_url: Some(source.url.clone()),
        moniker: source.moniker,
        severity: Severity::Warn,
        size_bytes: 0,
    })
    .add_error(LogError::DroppedLogs { count })
    .set_message(message)
    .build()
}

/// Constructs a `LogsData` from the provided bytes, assuming the bytes
/// are in the format specified as in the [log encoding].
///
/// [log encoding] https://fuchsia.dev/fuchsia-src/development/logs/encodings
pub fn from_structured(source: MonikerWithUrl, bytes: &[u8]) -> Result<LogsData, MessageError> {
    let (record, _) = diagnostics_log_encoding::parse::parse_record(bytes)?;

    let mut builder = LogsDataBuilder::new(BuilderArgs {
        timestamp_nanos: record.timestamp.into(),
        component_url: Some(source.url.clone()),
        moniker: source.moniker,
        // NOTE: this severity is not final. Severity will be set after parsing the
        // record.arguments
        severity: Severity::Info,
        size_bytes: bytes.len(),
    });

    // Raw value from the client that we don't trust (not yet sanitized)
    let mut severity_untrusted = None;
    let mut state = StateMachine::Init;
    let mut printf_arguments = vec![];
    let mut msg = None;
    for a in record.arguments {
        let name = a.name;
        macro_rules! insert_normal {
            () => {
                let label = LogsField::from(name);
                match (a.value, label) {
                    (Value::SignedInt(v), LogsField::Dropped) => {
                        builder = builder.set_dropped(v as u64);
                    }
                    (Value::UnsignedInt(v), LogsField::Dropped) => {
                        builder = builder.set_dropped(v);
                    }
                    (Value::Floating(f), LogsField::Dropped) => {
                        return Err(MessageError::ExpectedInteger {
                            value: format!("{:?}", f),
                            found: "float",
                        })
                    }
                    (Value::Text(t), LogsField::Dropped) => {
                        return Err(MessageError::ExpectedInteger { value: t, found: "text" });
                    }
                    (Value::SignedInt(v), LogsField::Verbosity) => {
                        severity_untrusted = Some(v);
                    }
                    (_, LogsField::Verbosity) => {
                        return Err(MessageError::ExpectedInteger {
                            value: "".into(),
                            found: "other",
                        });
                    }
                    (Value::Text(text), LogsField::Tag) => {
                        builder = builder.add_tag(text);
                    }
                    (_, LogsField::Tag) => {
                        return Err(MessageError::UnrecognizedValue);
                    }
                    (Value::UnsignedInt(v), LogsField::ProcessId) => {
                        builder = builder.set_pid(v);
                    }
                    (Value::UnsignedInt(v), LogsField::ThreadId) => {
                        builder = builder.set_tid(v);
                    }
                    (Value::Text(v), LogsField::Msg) => {
                        msg = Some(v);
                    }
                    (Value::Text(v), LogsField::FilePath) => {
                        builder = builder.set_file(v);
                    }
                    (Value::UnsignedInt(v), LogsField::LineNumber) => {
                        builder = builder.set_line(v);
                    }
                    (value, label) => {
                        builder = builder.add_key(match value {
                            Value::SignedInt(v) => LogsProperty::Int(label, v),
                            Value::UnsignedInt(v) => LogsProperty::Uint(label, v),
                            Value::Floating(v) => LogsProperty::Double(label, v),
                            Value::Text(v) => LogsProperty::String(label, v),
                            ValueUnknown!() => return Err(MessageError::UnrecognizedValue),
                        })
                    }
                }
            };
        }
        match state {
            StateMachine::Init if name == "printf" => {
                state = StateMachine::NestedRegularArgs;
            }
            StateMachine::Init => {
                insert_normal!();
                state = StateMachine::RegularArgs;
            }
            StateMachine::RegularArgs => {
                insert_normal!();
            }
            StateMachine::NestedRegularArgs if name == "" => {
                state = StateMachine::PrintfArgs;
                printf_arguments.push(match a.value {
                    Value::SignedInt(v) => v.to_string(),
                    Value::UnsignedInt(v) => v.to_string(),
                    Value::Floating(v) => v.to_string(),
                    Value::Text(v) => v,
                    ValueUnknown!() => return Err(MessageError::UnrecognizedValue),
                });
            }
            StateMachine::NestedRegularArgs => {
                insert_normal!();
            }
            StateMachine::PrintfArgs if name == "" => {
                printf_arguments.push(match a.value {
                    Value::SignedInt(v) => v.to_string(),
                    Value::UnsignedInt(v) => v.to_string(),
                    Value::Floating(v) => v.to_string(),
                    Value::Text(v) => v,
                    ValueUnknown!() => return Err(MessageError::UnrecognizedValue),
                });
            }
            StateMachine::PrintfArgs => {
                insert_normal!();
                state = StateMachine::RegularArgs;
            }
        }
    }

    let raw_severity = if severity_untrusted.is_some() {
        let transcoded_i32: i32 = severity_untrusted.unwrap().to_string().parse().unwrap();
        LegacySeverity::try_from(transcoded_i32)?
    } else {
        LegacySeverity::try_from(record.severity).unwrap()
    };
    let (severity, verbosity) = raw_severity.for_structured();
    if let Some(string) = msg {
        if !printf_arguments.is_empty() {
            builder = builder.set_format_printf(string, printf_arguments);
        } else {
            builder = builder.set_message(string)
        }
    }
    builder = builder.set_severity(severity);

    let mut result = builder.build();

    if severity_untrusted.is_some() && verbosity.is_some() {
        result.set_legacy_verbosity(verbosity.unwrap())
    }
    Ok(result)
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct LoggerMessage {
    pub timestamp: i64,
    pub severity: Severity,
    pub verbosity: Option<i8>,
    pub pid: u64,
    pub tid: u64,
    pub size_bytes: usize,
    pub dropped_logs: u64,
    pub message: Box<str>,
    pub tags: Vec<Box<str>>,
    include_moniker_tag: bool,
}

/// Parse the provided buffer as if it implements the [logger/syslog wire format].
///
/// Note that this is distinct from the parsing we perform for the debuglog log, which also
/// takes a `&[u8]` and is why we don't implement this as `TryFrom`.
///
/// [logger/syslog wire format]: https://fuchsia.googlesource.com/fuchsia/+/HEAD/zircon/system/ulib/syslog/include/lib/syslog/wire_format.h
impl TryFrom<&[u8]> for LoggerMessage {
    type Error = MessageError;

    fn try_from(bytes: &[u8]) -> Result<Self, Self::Error> {
        if bytes.len() < MIN_PACKET_SIZE {
            return Err(MessageError::ShortRead { len: bytes.len() });
        }

        let terminator = bytes[bytes.len() - 1];
        if terminator != 0 {
            return Err(MessageError::NotNullTerminated { terminator });
        }

        let pid = LittleEndian::read_u64(&bytes[..8]);
        let tid = LittleEndian::read_u64(&bytes[8..16]);
        let timestamp = LittleEndian::read_i64(&bytes[16..24]);

        let raw_severity = LittleEndian::read_i32(&bytes[24..28]);
        let severity = LegacySeverity::try_from(raw_severity)?;

        let dropped_logs = LittleEndian::read_u32(&bytes[28..METADATA_SIZE]) as u64;

        // start reading tags after the header
        let mut cursor = METADATA_SIZE;
        let mut tag_len = bytes[cursor] as usize;
        let mut tags = Vec::new();
        let mut include_moniker_tag = false;
        while tag_len != 0 {
            if tags.len() == MAX_TAGS {
                return Err(MessageError::TooManyTags);
            }

            if tag_len > MAX_TAG_LEN - 1 {
                return Err(MessageError::TagTooLong { index: tags.len(), len: tag_len });
            }

            if (cursor + tag_len + 1) > bytes.len() {
                return Err(MessageError::OutOfBounds);
            }

            let tag_start = cursor + 1;
            let tag_end = tag_start + tag_len;
            let tag = str::from_utf8(&bytes[tag_start..tag_end])?;

            if tag == COMPONENT_NAME_PLACEHOLDER_TAG {
                include_moniker_tag = true;
            } else {
                tags.push(tag.into());
            }

            cursor = tag_end;
            tag_len = bytes[cursor] as usize;
        }

        let msg_start = cursor + 1;
        let mut msg_end = cursor + 1;
        while msg_end < bytes.len() {
            if bytes[msg_end] > 0 {
                msg_end += 1;
                continue;
            }
            let message = str::from_utf8(&bytes[msg_start..msg_end])?.to_owned();
            let message_len = message.len();
            let (severity, verbosity) = severity.for_structured();
            let result = LoggerMessage {
                timestamp,
                severity,
                verbosity,
                message: message.into_boxed_str(),
                pid,
                tid,
                dropped_logs,
                tags,
                include_moniker_tag,
                size_bytes: cursor + message_len + 1,
            };
            return Ok(result);
        }

        Err(MessageError::OutOfBounds)
    }
}

#[allow(non_camel_case_types)]
pub type fx_log_severity_t = c_int;

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

pub fn parse_basic_structured_info(bytes: &[u8]) -> Result<(i64, Severity), MessageError> {
    let (record, _) = diagnostics_log_encoding::parse::parse_record(bytes)?;

    let mut severity_untrusted = None;
    for a in record.arguments {
        let label = LogsField::from(a.name);
        match (a.value, label) {
            (Value::SignedInt(v), LogsField::Verbosity) => {
                severity_untrusted = Some(v);
                break;
            }
            _ => {}
        }
    }
    let raw_severity = if severity_untrusted.is_some() {
        let transcoded_i32: i32 = severity_untrusted.unwrap().to_string().parse().unwrap();
        LegacySeverity::try_from(transcoded_i32)?
    } else {
        LegacySeverity::try_from(record.severity).unwrap()
    };
    let (severity, _) = raw_severity.for_structured();
    Ok((record.timestamp, severity))
}

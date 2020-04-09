// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

use super::buffer::Accounted;
use super::error::StreamError;
use byteorder::{ByteOrder, LittleEndian};
use fidl_fuchsia_logger::LogMessage;
use fuchsia_zircon as zx;
use libc::{c_char, c_int};
use std::{mem, str};

pub const METADATA_SIZE: usize = mem::size_of::<fx_log_metadata_t>();
pub const MIN_PACKET_SIZE: usize = METADATA_SIZE + 1;

pub const MAX_DATAGRAM_LEN: usize = 2032;
pub const MAX_TAGS: usize = 5;
pub const MAX_TAG_LEN: usize = 64;

/// A type-safe(r) [`LogMessage`].
///
/// [`LogMessage`]: https://fuchsia.dev/reference/fidl/fuchsia.logger#LogMessage
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Message {
    /// Size this message took up on the wire.
    pub size: usize,

    /// Message severity reported by the writer.
    pub severity: fx_log_severity_t,

    /// Timestamp reported by the writer.
    pub time: zx::Time,

    /// Process koid as reported by the writer.
    pub pid: zx::sys::zx_koid_t,

    /// Thread koid as reported by the writer.
    pub tid: zx::sys::zx_koid_t,

    /// Number of logs the writer had to drop before writing this message.
    pub dropped_logs: usize,

    /// Tags annotating the context or semantics of this message.
    pub tags: Vec<String>,

    /// The message's string contents.
    pub contents: String,
}

impl Accounted for Message {
    fn bytes_used(&self) -> usize {
        self.size
    }
}

impl Message {
    /// Parse the provided buffer as if it implements the [logger/syslog wire format].
    ///
    /// Note that this is distinct from the parsing we perform for the debuglog log, which aslo
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
        let severity = LittleEndian::read_i32(&bytes[24..28]);
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
                let contents = str::from_utf8(&bytes[msg_start..msg_end])?.to_owned();

                return Ok(Self {
                    size: cursor + contents.len() + 1,
                    tags,
                    contents,
                    pid,
                    tid,
                    time,
                    severity,
                    dropped_logs,
                });
            }
            msg_end += 1;
        }

        Err(StreamError::OutOfBounds)
    }

    /// Convert this `Message` to a FIDL representation suitable for sending to `LogListenerSafe`.
    pub fn for_listener(&self) -> LogMessage {
        LogMessage {
            pid: self.pid,
            tid: self.tid,
            time: self.time.into_nanos(),
            severity: self.severity,
            dropped_logs: self.dropped_logs as _,
            tags: self.tags.clone(),
            msg: self.contents.clone(),
        }
    }
}

#[allow(non_camel_case_types)]
type fx_log_severity_t = c_int;

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

#[cfg(test)]
impl fx_log_packet_t {
    /// This struct has no padding bytes, but we can't use zerocopy because it needs const
    /// generics to support arrays this large.
    pub(super) fn as_bytes(&self) -> &[u8] {
        unsafe {
            std::slice::from_raw_parts(
                (self as *const Self) as *const u8,
                mem::size_of::<fx_log_packet_t>(),
            )
        }
    }

    pub(super) fn fill_data(&mut self, region: std::ops::Range<usize>, with: c_char) {
        self.data[region].iter_mut().for_each(|c| *c = with);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

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
        packet.metadata.severity = -1;
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
        let parsed = Message::from_logger(buffer).unwrap();

        assert_eq!(
            parsed,
            Message {
                size: METADATA_SIZE + data_size,
                pid: packet.metadata.pid,
                tid: packet.metadata.tid,
                time: zx::Time::from_nanos(packet.metadata.time),
                severity: packet.metadata.severity,
                dropped_logs: packet.metadata.dropped_logs as usize,
                tags: vec![String::from("AAAAAAAAAAA")],
                contents: String::from("BBBBB"),
            }
        );
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
        let parsed = Message::from_logger(buffer).unwrap();

        assert_eq!(
            parsed,
            Message {
                size: METADATA_SIZE + data_size,
                pid: packet.metadata.pid,
                tid: packet.metadata.tid,
                time: zx::Time::from_nanos(packet.metadata.time),
                severity: packet.metadata.severity,
                dropped_logs: packet.metadata.dropped_logs as usize,
                tags: vec![String::from("AAAAAAAAAAA"), String::from("BBBBB")],
                contents: String::from("CCCCC"),
            }
        );
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

        let min_parsed = Message::from_logger(min_buffer).unwrap();
        let full_parsed = Message::from_logger(full_buffer).unwrap();

        let expected_message = Message {
            size: METADATA_SIZE + msg_end,
            pid: packet.metadata.pid,
            tid: packet.metadata.tid,
            time: zx::Time::from_nanos(packet.metadata.time),
            severity: packet.metadata.severity,
            dropped_logs: packet.metadata.dropped_logs as usize,
            contents: String::from_utf8(vec![msg_ascii as u8; msg_len]).unwrap(),
            tags: (0..MAX_TAGS as _)
                .map(|tag_num| {
                    String::from_utf8(vec![('A' as c_char + tag_num) as u8; tag_len]).unwrap()
                })
                .collect(),
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
        let parsed = Message::from_logger(buffer).unwrap();

        assert_eq!(
            parsed,
            Message {
                size: METADATA_SIZE + msg_start,
                pid: packet.metadata.pid,
                tid: packet.metadata.tid,
                time: zx::Time::from_nanos(packet.metadata.time),
                severity: packet.metadata.severity,
                dropped_logs: packet.metadata.dropped_logs as usize,
                tags: (0..MAX_TAGS as _)
                    .map(|tag_num| String::from_utf8(vec![('A' as c_char + tag_num) as u8; 2])
                        .unwrap())
                    .collect(),
                contents: String::new(),
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
                pid: packet.metadata.pid,
                tid: packet.metadata.tid,
                time: zx::Time::from_nanos(packet.metadata.time),
                severity: packet.metadata.severity,
                dropped_logs: packet.metadata.dropped_logs as usize,
                tags: vec![],
                contents: String::from("AA"),
            }
        );
    }
}

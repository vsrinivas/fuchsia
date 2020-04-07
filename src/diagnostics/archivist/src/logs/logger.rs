// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::error::StreamError,
    byteorder::{ByteOrder, LittleEndian},
    fidl_fuchsia_logger::LogMessage,
    fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::{
        io::{self, AsyncRead},
        ready,
        task::{Context, Poll},
        Stream,
    },
    libc::{c_char, c_int},
    std::{mem, pin::Pin, str},
};

type FxLogSeverityT = c_int;
type ZxKoid = u64;

pub const FX_LOG_MAX_DATAGRAM_LEN: usize = 2032;
pub const FX_LOG_MAX_TAGS: usize = 5;
pub const FX_LOG_MAX_TAG_LEN: usize = 64;

#[repr(C)]
#[derive(Debug, Copy, Clone, Default, Eq, PartialEq)]
pub struct fx_log_metadata_t {
    pub pid: ZxKoid,
    pub tid: ZxKoid,
    pub time: zx::sys::zx_time_t,
    pub severity: FxLogSeverityT,
    pub dropped_logs: u32,
}

pub const METADATA_SIZE: usize = mem::size_of::<fx_log_metadata_t>();
pub const MIN_PACKET_SIZE: usize = METADATA_SIZE + 1;

#[repr(C)]
#[derive(Clone)]
pub struct fx_log_packet_t {
    pub metadata: fx_log_metadata_t,
    // Contains concatenated tags and message and a null terminating character at
    // the end.
    // char(tag_len) + "tag1" + char(tag_len) + "tag2\0msg\0"
    pub data: [c_char; FX_LOG_MAX_DATAGRAM_LEN - METADATA_SIZE],
}

impl Default for fx_log_packet_t {
    fn default() -> fx_log_packet_t {
        fx_log_packet_t {
            data: [0; FX_LOG_MAX_DATAGRAM_LEN - METADATA_SIZE],
            metadata: Default::default(),
        }
    }
}

#[must_use = "futures/streams"]
pub struct LogMessageSocket {
    socket: fasync::Socket,
    buffer: [u8; FX_LOG_MAX_DATAGRAM_LEN],
}

impl LogMessageSocket {
    /// Creates a new `LoggerStream` for given `socket`.
    pub fn new(socket: zx::Socket) -> Result<Self, io::Error> {
        Ok(Self {
            socket: fasync::Socket::from_socket(socket)?,
            buffer: [0; FX_LOG_MAX_DATAGRAM_LEN],
        })
    }
}

impl Stream for LogMessageSocket {
    /// It returns log message and the size of the packet received.
    /// The size does not include the metadata size taken by
    /// LogMessage data structure.
    type Item = Result<(LogMessage, usize), StreamError>;

    fn poll_next(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        let &mut Self { ref mut socket, ref mut buffer } = &mut *self;
        let len = ready!(Pin::new(socket).poll_read(cx, buffer)?);

        let parsed = if len > 0 { Some(parse_log_message(&buffer[..len])) } else { None };
        Poll::Ready(parsed)
    }
}

fn parse_log_message(bytes: &[u8]) -> Result<(LogMessage, usize), StreamError> {
    if bytes.len() < MIN_PACKET_SIZE {
        return Err(StreamError::ShortRead { len: bytes.len() });
    }

    let terminator = bytes[bytes.len() - 1];
    if terminator != 0 {
        return Err(StreamError::NotNullTerminated { terminator });
    }

    let pid = LittleEndian::read_u64(&bytes[..8]);
    let tid = LittleEndian::read_u64(&bytes[8..16]);
    let time = LittleEndian::read_i64(&bytes[16..24]);
    let severity = LittleEndian::read_i32(&bytes[24..28]);
    let dropped_logs = LittleEndian::read_u32(&bytes[28..METADATA_SIZE]);

    // start reading tags after the header
    let mut cursor = METADATA_SIZE;
    let mut tag_len = bytes[cursor] as usize;
    let mut tags = Vec::new();
    while tag_len != 0 {
        if tags.len() == FX_LOG_MAX_TAGS {
            return Err(StreamError::TooManyTags);
        }

        if tag_len > FX_LOG_MAX_TAG_LEN - 1 {
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
            let msg = str::from_utf8(&bytes[msg_start..msg_end])?.to_owned();
            cursor += msg.len() + 1;

            return Ok((LogMessage { pid, tid, time, severity, dropped_logs, tags, msg }, cursor));
        }
        msg_end += 1;
    }

    Err(StreamError::OutOfBounds)
}

#[cfg(test)]
mod tests {
    use super::*;

    use fuchsia_async::DurationExt;
    use fuchsia_zircon::prelude::*;
    use futures::future::TryFutureExt;
    use futures::stream::TryStreamExt;
    use std::ops::Range;
    use std::slice;
    use std::sync::atomic::{AtomicUsize, Ordering};
    use std::sync::Arc;

    #[repr(C, packed)]
    pub struct fx_log_metadata_t_packed {
        pub pid: ZxKoid,
        pub tid: ZxKoid,
        pub time: zx::sys::zx_time_t,
        pub severity: FxLogSeverityT,
        pub dropped_logs: u32,
    }

    #[repr(C, packed)]
    pub struct fx_log_packet_t_packed {
        pub metadata: fx_log_metadata_t_packed,
        /// Contains concatenated tags and message and a null terminating character at the end.
        /// `char(tag_len) + "tag1" + char(tag_len) + "tag2\0msg\0"`
        pub data: [c_char; FX_LOG_MAX_DATAGRAM_LEN - METADATA_SIZE],
    }

    impl fx_log_packet_t {
        /// This struct has no padding bytes, but we can't use zerocopy because it needs const
        /// generics to support arrays this large.
        fn as_bytes(&self) -> &[u8] {
            unsafe {
                slice::from_raw_parts(
                    (self as *const Self) as *const u8,
                    mem::size_of::<fx_log_packet_t>(),
                )
            }
        }

        fn fill_data(&mut self, region: Range<usize>, with: c_char) {
            self.data[region].iter_mut().for_each(|c| *c = with);
        }
    }

    #[test]
    fn abi_test() {
        assert_eq!(METADATA_SIZE, 32);
        assert_eq!(FX_LOG_MAX_TAGS, 5);
        assert_eq!(FX_LOG_MAX_TAG_LEN, 64);
        assert_eq!(mem::size_of::<fx_log_packet_t>(), FX_LOG_MAX_DATAGRAM_LEN);

        // Test that there is no padding
        assert_eq!(mem::size_of::<fx_log_packet_t>(), mem::size_of::<fx_log_packet_t_packed>());

        assert_eq!(mem::size_of::<fx_log_metadata_t>(), mem::size_of::<fx_log_metadata_t_packed>());
    }

    #[test]
    fn logger_stream_test() {
        let mut executor = fasync::Executor::new().unwrap();
        let (sin, sout) = zx::Socket::create(zx::SocketOpts::DATAGRAM).unwrap();
        let mut packet: fx_log_packet_t = Default::default();
        packet.metadata.pid = 1;
        packet.data[0] = 5;
        packet.fill_data(1..6, 'A' as _);
        packet.fill_data(7..12, 'B' as _);

        let ls = LogMessageSocket::new(sout).unwrap();
        sin.write(packet.as_bytes()).unwrap();
        let mut expected_p = LogMessage {
            pid: packet.metadata.pid,
            tid: packet.metadata.tid,
            time: packet.metadata.time,
            severity: packet.metadata.severity,
            dropped_logs: packet.metadata.dropped_logs,
            tags: Vec::with_capacity(1),
            msg: String::from("BBBBB"),
        };
        expected_p.tags.push(String::from("AAAAA"));
        let calltimes = Arc::new(AtomicUsize::new(0));
        let c = calltimes.clone();
        let f = ls
            .map_ok(move |(msg, s)| {
                assert_eq!(msg, expected_p);
                assert_eq!(s, METADATA_SIZE + 6 /* tag */+ 6 /* msg */);
                c.fetch_add(1, Ordering::Relaxed);
            })
            .try_collect::<()>();

        fasync::spawn(f.unwrap_or_else(|e| {
            panic!("test fail {:?}", e);
        }));

        let tries = 10;
        for _ in 0..tries {
            if calltimes.load(Ordering::Relaxed) == 1 {
                break;
            }
            let timeout = fasync::Timer::new(100.millis().after_now());
            executor.run(timeout, 2);
        }
        assert_eq!(1, calltimes.load(Ordering::Relaxed));

        // write one more time
        sin.write(packet.as_bytes()).unwrap();

        for _ in 0..tries {
            if calltimes.load(Ordering::Relaxed) == 2 {
                break;
            }
            let timeout = fasync::Timer::new(100.millis().after_now());
            executor.run(timeout, 2);
        }
        assert_eq!(2, calltimes.load(Ordering::Relaxed));
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

        assert_eq!(parse_log_message(one_short), Err(StreamError::ShortRead { len: 32 }));

        assert_eq!(parse_log_message(two_short), Err(StreamError::ShortRead { len: 31 }));
    }

    #[test]
    fn unterminated() {
        let mut packet = test_packet();
        let end = 9;
        packet.data[end] = 1;

        let buffer = &packet.as_bytes()[..MIN_PACKET_SIZE + end];
        let parsed = parse_log_message(buffer);

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
        let parsed = parse_log_message(buffer);

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
        let (parsed, size) = parse_log_message(buffer).unwrap();

        assert_eq!(size, METADATA_SIZE + data_size);
        assert_eq!(
            parsed,
            LogMessage {
                pid: packet.metadata.pid,
                tid: packet.metadata.tid,
                time: packet.metadata.time,
                severity: packet.metadata.severity,
                dropped_logs: packet.metadata.dropped_logs,
                tags: vec![String::from("AAAAAAAAAAA")],
                msg: String::from("BBBBB"),
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
        let parsed = parse_log_message(buffer);

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
        let (parsed, size) = parse_log_message(buffer).unwrap();

        assert_eq!(size, METADATA_SIZE + data_size);
        assert_eq!(
            parsed,
            LogMessage {
                pid: packet.metadata.pid,
                tid: packet.metadata.tid,
                time: packet.metadata.time,
                severity: packet.metadata.severity,
                dropped_logs: packet.metadata.dropped_logs,
                tags: vec![String::from("AAAAAAAAAAA"), String::from("BBBBB")],
                msg: String::from("CCCCC"),
            }
        );
    }

    #[test]
    fn max_tags_with_message() {
        let mut packet = test_packet();

        let tags_start = 1;
        let tag_len = 2;
        let tag_size = tag_len + 1; // the length-prefix byte
        for tag_num in 0..FX_LOG_MAX_TAGS {
            let start = tags_start + (tag_size * tag_num);
            let end = start + tag_len;

            packet.data[start - 1] = tag_len as c_char;
            let ascii = 'A' as c_char + tag_num as c_char;
            packet.fill_data(start..end, ascii);
        }

        let msg_start = tags_start + (tag_size * FX_LOG_MAX_TAGS);
        let msg_len = 5;
        let msg_end = msg_start + msg_len;
        let msg_ascii = 'A' as c_char + FX_LOG_MAX_TAGS as c_char;
        packet.fill_data(msg_start..msg_end, msg_ascii);

        let min_buffer = &packet.as_bytes()[..METADATA_SIZE + msg_end + 1]; // null-terminated
        let full_buffer = &packet.as_bytes()[..];

        let (min_parsed, min_size) = parse_log_message(min_buffer).unwrap();
        let (full_parsed, full_size) = parse_log_message(full_buffer).unwrap();

        let expected_message = LogMessage {
            pid: packet.metadata.pid,
            tid: packet.metadata.tid,
            time: packet.metadata.time,
            severity: packet.metadata.severity,
            dropped_logs: packet.metadata.dropped_logs,
            msg: String::from_utf8(vec![msg_ascii as u8; msg_len]).unwrap(),
            tags: (0..FX_LOG_MAX_TAGS as _)
                .map(|tag_num| {
                    String::from_utf8(vec![('A' as c_char + tag_num) as u8; tag_len]).unwrap()
                })
                .collect(),
        };

        assert_eq!(min_size, METADATA_SIZE + msg_end);
        assert_eq!(full_size, min_size);

        assert_eq!(min_parsed, expected_message);
        assert_eq!(full_parsed, expected_message);
    }

    #[test]
    fn max_tags() {
        let mut packet = test_packet();
        let tags_start = 1;
        let tag_len = 2;
        let tag_size = tag_len + 1; // the length-prefix byte
        for tag_num in 0..FX_LOG_MAX_TAGS {
            let start = tags_start + (tag_size * tag_num);
            let end = start + tag_len;

            packet.data[start - 1] = tag_len as c_char;
            let ascii = 'A' as c_char + tag_num as c_char;
            packet.fill_data(start..end, ascii);
        }

        let msg_start = tags_start + (tag_size * FX_LOG_MAX_TAGS);

        let buffer_missing_terminator = &packet.as_bytes()[..METADATA_SIZE + msg_start];
        assert_eq!(
            parse_log_message(buffer_missing_terminator),
            Err(StreamError::OutOfBounds),
            "can't parse an empty message without a nul terminator"
        );

        let buffer = &packet.as_bytes()[..METADATA_SIZE + msg_start + 1]; // null-terminated
        let (parsed, size) = parse_log_message(buffer).unwrap();

        assert_eq!(size, METADATA_SIZE + msg_start);
        assert_eq!(
            parsed,
            LogMessage {
                pid: packet.metadata.pid,
                tid: packet.metadata.tid,
                time: packet.metadata.time,
                severity: packet.metadata.severity,
                dropped_logs: packet.metadata.dropped_logs,
                tags: (0..FX_LOG_MAX_TAGS as _)
                    .map(|tag_num| String::from_utf8(vec![('A' as c_char + tag_num) as u8; 2])
                        .unwrap())
                    .collect(),
                msg: String::new(),
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
        let (parsed, size) = parse_log_message(buffer).unwrap();

        assert_eq!(size, METADATA_SIZE + 3);
        assert_eq!(
            parsed,
            LogMessage {
                pid: packet.metadata.pid,
                tid: packet.metadata.tid,
                time: packet.metadata.time,
                severity: packet.metadata.severity,
                dropped_logs: packet.metadata.dropped_logs,
                tags: vec![],
                msg: String::from("AA"),
            }
        );
    }
}

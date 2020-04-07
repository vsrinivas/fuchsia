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

    let mut l = LogMessage {
        pid: LittleEndian::read_u64(&bytes[..8]),
        tid: LittleEndian::read_u64(&bytes[8..16]),
        time: LittleEndian::read_i64(&bytes[16..24]),
        severity: LittleEndian::read_i32(&bytes[24..28]),
        dropped_logs: LittleEndian::read_u32(&bytes[28..METADATA_SIZE]),
        tags: Vec::new(),
        msg: String::new(),
    };

    // start reading tags after the header
    let mut pos = METADATA_SIZE;
    let mut tag_len = bytes[pos] as usize;
    while tag_len != 0 {
        if l.tags.len() == FX_LOG_MAX_TAGS {
            return Err(StreamError::TooManyTags);
        }

        if tag_len > FX_LOG_MAX_TAG_LEN - 1 {
            return Err(StreamError::TagTooLong { index: l.tags.len(), len: tag_len });
        }

        if (pos + tag_len + 1) > bytes.len() {
            return Err(StreamError::OutOfBounds);
        }

        let str_slice = str::from_utf8(&bytes[(pos + 1)..(pos + tag_len + 1)])?;
        let str_buf: String = str_slice.to_owned();
        l.tags.push(str_buf);

        pos = pos + tag_len + 1;
        if pos >= bytes.len() {
            return Err(StreamError::OutOfBounds);
        }
        tag_len = bytes[pos] as usize;
    }
    let mut i = pos + 1;
    let mut found_msg = false;
    while i < bytes.len() {
        if bytes[i] == 0 {
            let str_slice = str::from_utf8(&bytes[pos + 1..i])?;
            let str_buf: String = str_slice.to_owned();
            found_msg = true;
            l.msg = str_buf;
            pos = pos + l.msg.len() + 1;
            break;
        }
        i = i + 1;
    }
    if !found_msg {
        return Err(StreamError::OutOfBounds);
    }
    Ok((l, pos))
}

#[cfg(test)]
mod tests {
    use super::*;

    use fuchsia_async::DurationExt;
    use fuchsia_zircon::prelude::*;
    use futures::future::TryFutureExt;
    use futures::stream::TryStreamExt;
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
    }

    const A: c_char = 'A' as _;
    const B: c_char = 'B' as _;
    const C: c_char = 'C' as _;

    fn memset<T: Copy>(x: &mut [T], offset: usize, value: T, size: usize) {
        x[offset..(offset + size)].iter_mut().for_each(|x| *x = value);
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
        let mut p: fx_log_packet_t = Default::default();
        p.metadata.pid = 1;
        p.data[0] = 5;
        memset(&mut p.data[..], 1, A, 5);
        memset(&mut p.data[..], 7, B, 5);

        let ls = LogMessageSocket::new(sout).unwrap();
        sin.write(p.as_bytes()).unwrap();
        let mut expected_p = LogMessage {
            pid: p.metadata.pid,
            tid: p.metadata.tid,
            time: p.metadata.time,
            severity: p.metadata.severity,
            dropped_logs: p.metadata.dropped_logs,
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
        sin.write(p.as_bytes()).unwrap();

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
        let mut p: fx_log_packet_t = Default::default();
        p.metadata.pid = 1;
        p.metadata.tid = 2;
        p.metadata.time = 3;
        p.metadata.severity = -1;
        p.metadata.dropped_logs = 10;
        p
    }

    #[test]
    fn short_reads() {
        let p = test_packet();
        let one_short = &p.as_bytes()[..METADATA_SIZE];
        let two_short = &p.as_bytes()[..METADATA_SIZE - 1];

        assert_eq!(parse_log_message(one_short), Err(StreamError::ShortRead { len: 32 }));

        assert_eq!(parse_log_message(two_short), Err(StreamError::ShortRead { len: 31 }));
    }

    #[test]
    fn unterminated() {
        let mut p = test_packet();
        let end = 9;
        p.data[end] = 1;

        let buffer = &p.as_bytes()[..MIN_PACKET_SIZE + end];
        let parsed = parse_log_message(buffer);

        assert_eq!(parsed, Err(StreamError::NotNullTerminated { terminator: 1 }));
    }

    #[test]
    fn tags_no_message() {
        let mut p = test_packet();
        let end = 12;
        p.data[0] = end as c_char - 1;
        memset(&mut p.data[..], 1, A, end - 1);
        p.data[end] = 0;

        let buffer = &p.as_bytes()[..MIN_PACKET_SIZE + end];
        let parsed = parse_log_message(buffer);

        assert_eq!(parsed, Err(StreamError::OutOfBounds));
    }

    #[test]
    fn tags_with_message() {
        let mut p = test_packet();
        let a_start = 1;
        let a_count = 11;

        p.data[0] = a_count as c_char;
        memset(&mut p.data[..], a_start, A, a_count);
        p.data[a_start + a_count] = 0; // terminate tags

        let b_start = a_start + a_count + 1;
        let b_count = 5;
        memset(&mut p.data[..], b_start, B, b_count);

        let data_size = b_start + b_count;

        let buffer = &p.as_bytes()[..METADATA_SIZE + data_size + 1]; // null-terminate message
        let (parsed, size) = parse_log_message(buffer).unwrap();

        assert_eq!(size, METADATA_SIZE + data_size);
        assert_eq!(
            parsed,
            LogMessage {
                pid: p.metadata.pid,
                tid: p.metadata.tid,
                time: p.metadata.time,
                severity: p.metadata.severity,
                dropped_logs: p.metadata.dropped_logs,
                tags: vec![String::from("AAAAAAAAAAA")],
                msg: String::from("BBBBB"),
            }
        );
    }

    #[test]
    fn two_tags_no_message() {
        let mut p = test_packet();
        let a_start = 1;
        let a_count = 11;

        p.data[0] = a_count as c_char;
        memset(&mut p.data[..], a_start, A, a_count);

        let b_start = a_start + a_count + 1;
        let b_count = 5;

        p.data[b_start - 1] = b_count as c_char;
        memset(&mut p.data[..], b_start, B, b_count);

        let buffer = &p.as_bytes()[..MIN_PACKET_SIZE + b_start + b_count];
        let parsed = parse_log_message(buffer);

        assert_eq!(parsed, Err(StreamError::OutOfBounds));
    }

    #[test]
    fn two_tags_with_message() {
        let mut p = test_packet();
        let a_start = 1;
        let a_count = 11;

        p.data[0] = a_count as c_char;
        memset(&mut p.data[..], a_start, A, a_count);

        let b_start = a_start + a_count + 1;
        let b_count = 5;

        p.data[b_start - 1] = b_count as c_char;
        memset(&mut p.data[..], b_start, B, b_count);

        let c_start = b_start + b_count + 1;
        let c_count = 5;
        memset(&mut p.data[..], c_start, C, c_count);

        let data_size = c_start + c_count;

        let buffer = &p.as_bytes()[..METADATA_SIZE + data_size + 1]; // null-terminated
        let (parsed, size) = parse_log_message(buffer).unwrap();

        assert_eq!(size, METADATA_SIZE + data_size);
        assert_eq!(
            parsed,
            LogMessage {
                pid: p.metadata.pid,
                tid: p.metadata.tid,
                time: p.metadata.time,
                severity: p.metadata.severity,
                dropped_logs: p.metadata.dropped_logs,
                tags: vec![String::from("AAAAAAAAAAA"), String::from("BBBBB")],
                msg: String::from("CCCCC"),
            }
        );
    }

    #[test]
    fn max_tags_with_message() {
        let mut p = test_packet();

        let tags_start = 1;
        let tag_len = 2;
        let tag_size = tag_len + 1; // the length-prefix byte
        for i in 0..FX_LOG_MAX_TAGS {
            let start = tags_start + (tag_size * i);
            let ascii = A + i as c_char;

            p.data[start - 1] = tag_len as c_char;
            memset(&mut p.data[..], start, ascii, tag_len);
        }

        let msg_start = tags_start + (tag_size * FX_LOG_MAX_TAGS);
        let msg_len = 5;
        let ascii = A + FX_LOG_MAX_TAGS as c_char;
        memset(&mut p.data[..], msg_start, ascii, msg_len);

        let data_size = msg_start + msg_len;

        let min_buffer = &p.as_bytes()[..METADATA_SIZE + data_size + 1]; // null-terminated
        let full_buffer = &p.as_bytes()[..];

        let (min_parsed, min_size) = parse_log_message(min_buffer).unwrap();
        let (full_parsed, full_size) = parse_log_message(full_buffer).unwrap();

        let expected_message = LogMessage {
            pid: p.metadata.pid,
            tid: p.metadata.tid,
            time: p.metadata.time,
            severity: p.metadata.severity,
            dropped_logs: p.metadata.dropped_logs,
            msg: String::from_utf8(vec![ascii as u8; msg_len]).unwrap(),
            tags: (0..FX_LOG_MAX_TAGS as _)
                .map(|i| String::from_utf8(vec![(A + i) as u8; tag_len]).unwrap())
                .collect(),
        };

        assert_eq!(min_size, METADATA_SIZE + data_size);
        assert_eq!(full_size, min_size);

        assert_eq!(min_parsed, expected_message);
        assert_eq!(full_parsed, expected_message);
    }

    #[test]
    fn max_tags() {
        let mut p = test_packet();
        let tags_start = 1;
        let tag_len = 2;
        let tag_size = tag_len + 1; // the length-prefix byte
        for i in 0..FX_LOG_MAX_TAGS {
            let start = tags_start + (tag_size * i);
            let ascii = A + i as c_char;

            p.data[start - 1] = tag_len as c_char;
            memset(&mut p.data[..], start, ascii, tag_len);
        }

        let msg_start = tags_start + (tag_size * FX_LOG_MAX_TAGS);

        let buffer_missing_terminator = &p.as_bytes()[..METADATA_SIZE + msg_start];
        assert_eq!(
            parse_log_message(buffer_missing_terminator),
            Err(StreamError::OutOfBounds),
            "can't parse an empty message without a nul terminator"
        );

        let buffer = &p.as_bytes()[..METADATA_SIZE + msg_start + 1]; // null-terminated
        let (parsed, size) = parse_log_message(buffer).unwrap();

        assert_eq!(size, METADATA_SIZE + msg_start);
        assert_eq!(
            parsed,
            LogMessage {
                pid: p.metadata.pid,
                tid: p.metadata.tid,
                time: p.metadata.time,
                severity: p.metadata.severity,
                dropped_logs: p.metadata.dropped_logs,
                tags: (0..FX_LOG_MAX_TAGS as _)
                    .map(|i| String::from_utf8(vec![(A + i) as u8; 2]).unwrap())
                    .collect(),
                msg: String::new(),
            }
        );
    }

    #[test]
    fn no_tags_with_message() {
        let mut p = test_packet();
        p.data[0] = 0;
        p.data[1] = A;
        p.data[2] = A; // measured size ends here
        p.data[3] = 0;

        let buffer = &p.as_bytes()[..METADATA_SIZE + 4]; // 0 tag size + 2 byte message + null
        let (parsed, size) = parse_log_message(buffer).unwrap();

        assert_eq!(size, METADATA_SIZE + 3);
        assert_eq!(
            parsed,
            LogMessage {
                pid: p.metadata.pid,
                tid: p.metadata.tid,
                time: p.metadata.time,
                severity: p.metadata.severity,
                dropped_logs: p.metadata.dropped_logs,
                tags: vec![],
                msg: String::from("AA"),
            }
        );
    }
}

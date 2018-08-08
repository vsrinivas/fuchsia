// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use async;
use byteorder::{ByteOrder, LittleEndian};
use fidl_fuchsia_logger::LogMessage;
use futures::io;
use futures::prelude::*;
use libc::{c_char, c_int, uint32_t, uint64_t, uint8_t};
use std::cell::RefCell;
use std::{mem, str};
use zx;

type FxLogSeverityT = c_int;
type ZxKoid = uint64_t;

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
    pub dropped_logs: uint32_t,
}

pub const METADATA_SIZE: usize = mem::size_of::<fx_log_metadata_t>();

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
pub struct LoggerStream {
    socket: async::Socket,
}

thread_local! {
    pub static BUFFER:
        RefCell<[u8; FX_LOG_MAX_DATAGRAM_LEN]> = RefCell::new([0; FX_LOG_MAX_DATAGRAM_LEN]);
}

impl LoggerStream {
    /// Creates a new `LoggerStream` for given `socket`.
    pub fn new(socket: zx::Socket) -> Result<LoggerStream, io::Error> {
        let l = LoggerStream {
            socket: async::Socket::from_socket(socket)?,
        };
        Ok(l)
    }
}

fn convert_to_log_message(bytes: &[u8]) -> Option<(LogMessage, usize)> {
    // Check that data has metadata and first 1 byte is integer and last byte is NULL.
    if bytes.len() < METADATA_SIZE + mem::size_of::<uint8_t>() || bytes[bytes.len() - 1] != 0 {
        return None;
    }

    let mut l = LogMessage {
        pid: LittleEndian::read_u64(&bytes[0..8]),
        tid: LittleEndian::read_u64(&bytes[8..16]),
        time: LittleEndian::read_u64(&bytes[16..24]),
        severity: LittleEndian::read_i32(&bytes[24..28]),
        dropped_logs: LittleEndian::read_u32(&bytes[28..METADATA_SIZE]),
        tags: Vec::new(),
        msg: String::new(),
    };

    let mut pos = METADATA_SIZE;
    let mut tag_len = bytes[pos] as usize;
    while tag_len != 0 {
        if l.tags.len() == FX_LOG_MAX_TAGS {
            return None;
        }
        if tag_len > FX_LOG_MAX_TAG_LEN - 1 {
            return None;
        }
        if (pos + tag_len + 1) > bytes.len() {
            return None;
        }
        let str_slice = match str::from_utf8(&bytes[(pos + 1)..(pos + tag_len + 1)]) {
            Err(_e) => return None,
            Ok(s) => s,
        };
        let str_buf: String = str_slice.to_owned();
        l.tags.push(str_buf);

        pos = pos + tag_len + 1;
        if pos >= bytes.len() {
            return None;
        }
        tag_len = bytes[pos] as usize;
    }
    let mut i = pos + 1;
    let mut found_msg = false;
    while i < bytes.len() {
        if bytes[i] == 0 {
            let str_slice = match str::from_utf8(&bytes[pos + 1..i]) {
                Err(_e) => return None,
                Ok(s) => s,
            };
            let str_buf: String = str_slice.to_owned();
            found_msg = true;
            l.msg = str_buf;
            pos = pos + l.msg.len() + 1;
            break;
        }
        i = i + 1;
    }
    if !found_msg {
        return None;
    }
    Some((l, pos))
}

impl Stream for LoggerStream {
    /// It returns log message and the size of the packet received.
    /// The size does not include the metadata size taken by
    /// LogMessage data structure.
    type Item = (LogMessage, usize);
    type Error = io::Error;

    fn poll_next(&mut self, cx: &mut task::Context) -> Poll<Option<Self::Item>, Self::Error> {
        BUFFER.with(|b| {
            let mut b = b.borrow_mut();
            let len = try_ready!(self.socket.poll_read(cx, &mut *b));
            if len == 0 {
                return Ok(Async::Ready(None));
            }
            Ok(Async::Ready(convert_to_log_message(&b[0..len])))
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use std::slice;
    use std::sync::atomic::{AtomicUsize, Ordering};
    use std::sync::Arc;
    use zx::prelude::*;

    #[repr(C, packed)]
    pub struct fx_log_metadata_t_packed {
        pub pid: ZxKoid,
        pub tid: ZxKoid,
        pub time: zx::sys::zx_time_t,
        pub severity: FxLogSeverityT,
        pub dropped_logs: uint32_t,
    }

    #[repr(C, packed)]
    pub struct fx_log_packet_t_packed {
        pub metadata: fx_log_metadata_t_packed,
        // Contains concatenated tags and message and a null terminating character at
        // the end.
        // char(tag_len) + "tag1" + char(tag_len) + "tag2\0msg\0"
        pub data: [c_char; FX_LOG_MAX_DATAGRAM_LEN - METADATA_SIZE],
    }

    /// Function to convert fx_log_packet_t to &[u8].
    /// This function is safe as it works on `fx_log_packet_t` which
    /// doesn't have any uninitialized padding bits.
    fn to_u8_slice(p: &fx_log_packet_t) -> &[u8] {
        // This code just converts to &[u8] so no need to explicity drop it as memory
        // location would be freed as soon as p is dropped.
        unsafe {
            slice::from_raw_parts(
                (p as *const fx_log_packet_t) as *const u8,
                mem::size_of::<fx_log_packet_t>(),
            )
        }
    }

    fn memset<T: Copy>(x: &mut [T], offset: usize, value: T, size: usize) {
        x[offset..(offset + size)]
            .iter_mut()
            .for_each(|x| *x = value);
    }

    #[test]
    fn abi_test() {
        assert_eq!(METADATA_SIZE, 32);
        assert_eq!(FX_LOG_MAX_TAGS, 5);
        assert_eq!(FX_LOG_MAX_TAG_LEN, 64);
        assert_eq!(mem::size_of::<fx_log_packet_t>(), FX_LOG_MAX_DATAGRAM_LEN);

        // Test that there is no padding
        assert_eq!(
            mem::size_of::<fx_log_packet_t>(),
            mem::size_of::<fx_log_packet_t_packed>(),
        );

        assert_eq!(
            mem::size_of::<fx_log_metadata_t>(),
            mem::size_of::<fx_log_metadata_t_packed>(),
        );
    }

    #[test]
    fn logger_stream_test() {
        let mut executor = async::Executor::new().unwrap();
        let (sin, sout) = zx::Socket::create(zx::SocketOpts::DATAGRAM).unwrap();
        let mut p: fx_log_packet_t = Default::default();
        p.metadata.pid = 1;
        p.data[0] = 5;
        memset(&mut p.data[..], 1, 65, 5);
        memset(&mut p.data[..], 7, 66, 5);

        let ls = LoggerStream::new(sout).unwrap();
        sin.write(to_u8_slice(&mut p)).unwrap();
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
        let f = ls.for_each(move |(msg, s)| {
            assert_eq!(msg, expected_p);
            assert_eq!(s, METADATA_SIZE + 6 /* tag */+ 6 /* msg */);
            c.fetch_add(1, Ordering::Relaxed);
            Ok(())
        }).map(|_s| ());

        async::spawn(f.recover(|e| {
            panic!("test fail {:?}", e);
        }));

        let tries = 10;
        for _ in 0..tries {
            if calltimes.load(Ordering::Relaxed) == 1 {
                break;
            }
            let timeout = async::Timer::<()>::new(100.millis().after_now());
            executor.run(timeout, 2).unwrap();
        }
        assert_eq!(1, calltimes.load(Ordering::Relaxed));

        // write one more time
        sin.write(to_u8_slice(&p)).unwrap();

        for _ in 0..tries {
            if calltimes.load(Ordering::Relaxed) == 2 {
                break;
            }
            let timeout = async::Timer::<()>::new(100.millis().after_now());
            executor.run(timeout, 2).unwrap();
        }
        assert_eq!(2, calltimes.load(Ordering::Relaxed));
    }

    #[test]
    fn convert_to_log_message_test() {
        // We use fx_log_packet_t and unsafe operations so that we can test that
        // convert_to_log_message correctly parses all its fields.
        let mut p: fx_log_packet_t = Default::default();
        p.metadata.pid = 1;
        p.metadata.tid = 2;
        p.metadata.time = 3;
        p.metadata.severity = -1;
        p.metadata.dropped_logs = 10;

        {
            let buffer = to_u8_slice(&p);
            assert_eq!(convert_to_log_message(&buffer[0..METADATA_SIZE]), None);
            assert_eq!(convert_to_log_message(&buffer[0..METADATA_SIZE - 1]), None);
        }

        // Test that there should be null byte at end
        {
            p.data[9] = 1;
            let buffer = to_u8_slice(&p);
            assert_eq!(convert_to_log_message(&buffer[0..METADATA_SIZE + 10]), None);
        }
        // test tags but no message
        {
            p.data[0] = 11;
            memset(&mut p.data[..], 1, 65, 11);
            p.data[12] = 0;
            let buffer = to_u8_slice(&p);
            assert_eq!(convert_to_log_message(&buffer[0..METADATA_SIZE + 13]), None);
        }

        // test tags with message

        let mut expected_p = LogMessage {
            pid: p.metadata.pid,
            tid: p.metadata.tid,
            time: p.metadata.time,
            severity: p.metadata.severity,
            dropped_logs: p.metadata.dropped_logs,
            tags: Vec::with_capacity(1),
            msg: String::from("BBBBB"),
        };
        expected_p.tags.push(String::from("AAAAAAAAAAA"));
        let mut s = Some((expected_p, METADATA_SIZE + 18));
        {
            memset(&mut p.data[..], 13, 66, 5);
            let buffer = to_u8_slice(&p);
            assert_eq!(convert_to_log_message(&buffer[0..METADATA_SIZE + 19]), s);
        }

        // test 2 tags with no message
        {
            p.data[0] = 11;
            p.data[12] = 5;
            let buffer = to_u8_slice(&p);
            assert_eq!(convert_to_log_message(&buffer[0..METADATA_SIZE + 19]), None);
        }

        // test 2 tags with message
        {
            memset(&mut p.data[..], 19, 67, 5);
            expected_p = s.unwrap().0;
            expected_p.tags.push(String::from("BBBBB"));
            expected_p.msg = String::from("CCCCC");
            s = Some((expected_p, METADATA_SIZE + 24));
            let buffer = to_u8_slice(&p);
            assert_eq!(convert_to_log_message(&buffer[0..METADATA_SIZE + 25]), s);
        }

        // test max tags with message
        {
            let data_len = p.data.len();
            memset(&mut p.data[..], 0, 0, data_len);
            let mut i: usize = 0;
            while i < FX_LOG_MAX_TAGS as usize {
                let ascii = (65 + i) as c_char;
                p.data[3 * i] = 2;
                memset(&mut p.data[..], 1 + 3 * i, ascii, 2);
                i = i + 1;
            }
            p.data[3 * i] = 0;
            let ascii = (65 + i) as c_char;
            memset(&mut p.data[..], 1 + 3 * i, ascii, 5);
            p.data[1 + 3 * i + 5] = 0;
            expected_p = s.unwrap().0;
            expected_p.tags.clear();
            {
                let mut i: u8 = 0;
                while i < FX_LOG_MAX_TAGS as u8 {
                    let tag = vec![65 + i, 65 + i];
                    expected_p.tags.push(String::from_utf8(tag).unwrap());
                    i = i + 1;
                }
                let msg = vec![65 + i, 65 + i, 65 + i, 65 + i, 65 + i];
                expected_p.msg = String::from_utf8(msg).unwrap();
            }
            s = Some((
                expected_p,
                METADATA_SIZE + 1 + 3 * (FX_LOG_MAX_TAGS as usize) + 5,
            ));
            let buffer = to_u8_slice(&p);
            assert_eq!(
                convert_to_log_message(
                    &buffer[0..(METADATA_SIZE + 1 + 3 * (FX_LOG_MAX_TAGS as usize) + 6)],
                ),
                s
            );

            // test max tags with message and writing full bytes
            assert_eq!(convert_to_log_message(&buffer[0..buffer.len()]), s);
        }

        // test max tags with no message
        {
            let buffer = to_u8_slice(&p);
            assert_eq!(
                convert_to_log_message(
                    &buffer[0..(METADATA_SIZE + 1 + 3 * (FX_LOG_MAX_TAGS as usize))],
                ),
                None
            );
        }

        // test max tags with empty message
        {
            p.data[1 + 3 * FX_LOG_MAX_TAGS as usize] = 0;
            expected_p = s.unwrap().0;
            expected_p.msg = String::from("");
            s = Some((
                expected_p,
                METADATA_SIZE + 1 + 3 * (FX_LOG_MAX_TAGS as usize),
            ));
            let buffer = to_u8_slice(&p);
            assert_eq!(
                convert_to_log_message(
                    &buffer[0..(METADATA_SIZE + 1 + 3 * (FX_LOG_MAX_TAGS as usize) + 1)],
                ),
                s
            );
        }

        // test zero tags with some message
        {
            p.data[0] = 0;
            p.data[3] = 0;
            expected_p = s.unwrap().0;
            expected_p.msg = String::from("AA");
            expected_p.tags.clear();
            s = Some((expected_p, METADATA_SIZE + 3));
            let buffer = to_u8_slice(&p);
            assert_eq!(convert_to_log_message(&buffer[0..(METADATA_SIZE + 4)]), s);
        }
    }
}

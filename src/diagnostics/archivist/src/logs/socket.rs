// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

use super::error::StreamError;
use super::message::{parse_log_message, MAX_DATAGRAM_LEN};
use fidl_fuchsia_logger::LogMessage;
use fidl_fuchsia_sys_internal::SourceIdentity;
use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use futures::{
    io::{self, AsyncRead},
    ready,
    task::{Context, Poll},
    Stream,
};
use std::pin::Pin;

#[must_use = "don't drop logs on the floor please!"]
pub struct LogMessageSocket {
    #[allow(unused)]
    source: SourceIdentity,
    socket: fasync::Socket,
    buffer: [u8; MAX_DATAGRAM_LEN],
}

impl LogMessageSocket {
    /// Creates a new `LoggerStream` for given `socket`.
    pub fn new(socket: zx::Socket, source: SourceIdentity) -> Result<Self, io::Error> {
        Ok(Self {
            socket: fasync::Socket::from_socket(socket)?,
            buffer: [0; MAX_DATAGRAM_LEN],
            source,
        })
    }
}

impl Stream for LogMessageSocket {
    /// It returns log message and the size of the packet received.
    /// The size does not include the metadata size taken by
    /// LogMessage data structure.
    type Item = Result<(LogMessage, usize), StreamError>;

    fn poll_next(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        let &mut Self { ref mut socket, ref mut buffer, .. } = &mut *self;
        let len = ready!(Pin::new(socket).poll_read(cx, buffer)?);

        let parsed = if len > 0 { Some(parse_log_message(&buffer[..len])) } else { None };
        Poll::Ready(parsed)
    }
}

#[cfg(test)]
mod tests {
    use super::super::message::{fx_log_packet_t, METADATA_SIZE};
    use super::*;

    use fuchsia_async::DurationExt;
    use fuchsia_zircon::prelude::*;
    use futures::future::TryFutureExt;
    use futures::stream::TryStreamExt;
    use std::sync::{
        atomic::{AtomicUsize, Ordering},
        Arc,
    };

    #[test]
    fn logger_stream_test() {
        let mut executor = fasync::Executor::new().unwrap();
        let (sin, sout) = zx::Socket::create(zx::SocketOpts::DATAGRAM).unwrap();
        let mut packet: fx_log_packet_t = Default::default();
        packet.metadata.pid = 1;
        packet.data[0] = 5;
        packet.fill_data(1..6, 'A' as _);
        packet.fill_data(7..12, 'B' as _);

        let ls = LogMessageSocket::new(sout, SourceIdentity::empty()).unwrap();
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
}

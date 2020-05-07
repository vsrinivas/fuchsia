// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

use super::error::StreamError;
use super::message::{Message, MAX_DATAGRAM_LEN};
use crate::logs::stats::ComponentLogStats;
use fidl_fuchsia_sys_internal::SourceIdentity;
use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use futures::{
    io::{self, AsyncRead},
    lock::Mutex,
    ready,
    task::{Context, Poll},
    Future, Stream,
};
use std::pin::Pin;
use std::sync::Arc;

#[must_use = "don't drop logs on the floor please!"]
pub struct LogMessageSocket {
    source: Arc<SourceIdentity>,
    /// stats represents the stats for a particular component URL. Once all sockets
    /// that hold these stats are dropped, so is the inspect node.
    stats: Arc<Mutex<ComponentLogStats>>,
    socket: fasync::Socket,
    buffer: [u8; MAX_DATAGRAM_LEN],
}

impl LogMessageSocket {
    /// Creates a new `LogMessageSocket` from the given `socket`.
    pub fn new(
        socket: zx::Socket,
        stats: Arc<Mutex<ComponentLogStats>>,
        source: Arc<SourceIdentity>,
    ) -> Result<Self, io::Error> {
        Ok(Self {
            socket: fasync::Socket::from_socket(socket)?,
            buffer: [0; MAX_DATAGRAM_LEN],
            stats,
            source,
        })
    }

    /// What we know of the identity of the writer of these logs.
    pub fn source(&self) -> &Arc<SourceIdentity> {
        &self.source
    }
}

impl Stream for LogMessageSocket {
    type Item = Result<Message, StreamError>;

    fn poll_next(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        let &mut Self { ref mut socket, ref mut buffer, .. } = &mut *self;
        let len = ready!(Pin::new(socket).poll_read(cx, buffer)?);

        let parsed = if len > 0 {
            let message = Message::from_logger(&buffer[..len]);
            if let Ok(message) = &message {
                let mut stats = self.stats.lock();
                let component_log_stats = ready!(Pin::new(&mut stats).poll(cx));
                component_log_stats.record_log(&message);
            }
            Some(message)
        } else {
            None
        };
        Poll::Ready(parsed)
    }
}

#[cfg(test)]
mod tests {
    use super::super::message::{fx_log_packet_t, Message, Severity, METADATA_SIZE};
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

        let ls = LogMessageSocket::new(
            sout,
            Arc::new(Mutex::new(ComponentLogStats::default())),
            Arc::new(SourceIdentity::empty()),
        )
        .unwrap();
        sin.write(packet.as_bytes()).unwrap();
        let mut expected_p = Message {
            size: METADATA_SIZE + 6 /* tag */+ 6, /* msg */
            pid: packet.metadata.pid as _,
            tid: packet.metadata.tid as _,
            time: zx::Time::from_nanos(packet.metadata.time),
            severity: Severity::Info,
            dropped_logs: packet.metadata.dropped_logs as usize,
            tags: Vec::with_capacity(1),
            contents: String::from("BBBBB"),
        };
        expected_p.tags.push(String::from("AAAAA"));
        let calltimes = Arc::new(AtomicUsize::new(0));
        let c = calltimes.clone();
        let f = ls
            .map_ok(move |msg| {
                assert_eq!(msg, expected_p);
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

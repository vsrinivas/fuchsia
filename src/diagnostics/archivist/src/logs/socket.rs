// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

use super::error::StreamError;
use super::message::Message;
use fidl_fuchsia_sys_internal::SourceIdentity;
use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use futures::{
    io, ready,
    task::{Context, Poll},
    Stream,
};
use std::pin::Pin;
use std::sync::Arc;

#[must_use = "don't drop logs on the floor please!"]
pub struct LogMessageSocket {
    source: Arc<SourceIdentity>,
    socket: fasync::Socket,
    buffer: Vec<u8>,
}

impl LogMessageSocket {
    /// Creates a new `LogMessageSocket` from the given `socket`.
    pub fn new(socket: zx::Socket, source: Arc<SourceIdentity>) -> Result<Self, io::Error> {
        Ok(Self { socket: fasync::Socket::from_socket(socket)?, buffer: Vec::new(), source })
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
        Poll::Ready(match ready!(socket.poll_datagram(cx, buffer)) {
            Ok(_) => {
                let res = Message::from_logger(buffer.as_slice());
                buffer.clear();
                Some(res)
            }
            Err(zx::Status::PEER_CLOSED) => None,
            Err(e) => Some(Err(StreamError::Io { source: e.into_io_error() })),
        })
    }
}

#[cfg(test)]
mod tests {
    use super::super::message::{
        fx_log_packet_t, LogHierarchy, LogProperty, Message, MessageLabel, Severity, METADATA_SIZE,
    };
    use super::*;
    use futures::stream::TryStreamExt;

    #[fasync::run_until_stalled(test)]
    async fn logger_stream_test() {
        let (sin, sout) = zx::Socket::create(zx::SocketOpts::DATAGRAM).unwrap();
        let mut packet: fx_log_packet_t = Default::default();
        packet.metadata.pid = 1;
        packet.metadata.severity = 0x30; // INFO
        packet.data[0] = 5;
        packet.fill_data(1..6, 'A' as _);
        packet.fill_data(7..12, 'B' as _);

        let mut ls = LogMessageSocket::new(sout, Arc::new(SourceIdentity::empty())).unwrap();
        sin.write(packet.as_bytes()).unwrap();
        let mut expected_p = Message {
            size: METADATA_SIZE + 6 /* tag */+ 6, /* msg */
            time: zx::Time::from_nanos(packet.metadata.time),
            severity: Severity::Info,
            contents: LogHierarchy::new(
                "root",
                vec![
                    LogProperty::Uint(MessageLabel::ProcessId, packet.metadata.pid),
                    LogProperty::Uint(MessageLabel::ThreadId, packet.metadata.tid),
                    LogProperty::Uint(MessageLabel::Dropped, packet.metadata.dropped_logs as _),
                    LogProperty::string(MessageLabel::Tag, "AAAAA"),
                    LogProperty::string(MessageLabel::Msg, "BBBBB"),
                ],
                vec![],
            ),
        };
        expected_p.contents.sort();

        let mut result_message = ls.try_next().await.unwrap().unwrap();
        result_message.contents.sort();
        assert_eq!(result_message, expected_p);

        // write one more time
        sin.write(packet.as_bytes()).unwrap();

        let mut result_message = ls.try_next().await.unwrap().unwrap();
        result_message.contents.sort();
        assert_eq!(result_message, expected_p);
    }
}

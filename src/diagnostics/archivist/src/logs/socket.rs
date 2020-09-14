// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

use super::error::StreamError;
use super::message::{Message, MAX_DATAGRAM_LEN};
use fidl_fuchsia_sys_internal::SourceIdentity;
use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use futures::io::{self, AsyncReadExt};
use std::{marker::PhantomData, sync::Arc};

/// An `Encoding` is able to parse a `Message` from raw bytes.
pub trait Encoding {
    /// Attempt to parse a message from the given buffer
    fn parse_message(buf: &[u8]) -> Result<Message, StreamError>;
}

/// An encoding that can parse the legacy [logger/syslog wire format]
///
/// [logger/syslog wire format]: https://fuchsia.googlesource.com/fuchsia/+/master/zircon/system/ulib/syslog/include/lib/syslog/wire_format.h
#[derive(Clone, Debug)]
pub struct LegacyEncoding;

/// An encoding that can parse the [structured log format]
///
/// [structured log format]: https://fuchsia.dev/fuchsia-src/development/logs/encodings
#[derive(Clone, Debug)]
pub struct StructuredEncoding;

impl Encoding for LegacyEncoding {
    fn parse_message(buf: &[u8]) -> Result<Message, StreamError> {
        Message::from_logger(buf)
    }
}

impl Encoding for StructuredEncoding {
    fn parse_message(buf: &[u8]) -> Result<Message, StreamError> {
        Message::from_structured(buf)
    }
}

#[must_use = "don't drop logs on the floor please!"]
pub struct LogMessageSocket<E> {
    source: Arc<SourceIdentity>,
    socket: fasync::Socket,
    buffer: [u8; MAX_DATAGRAM_LEN],
    forwarder: Forwarder<E>,
    _encoder: PhantomData<E>,
}

impl<E> LogMessageSocket<E> {
    /// Description of the source of the items.
    pub fn source(&self) -> &SourceIdentity {
        &self.source
    }
}

impl LogMessageSocket<LegacyEncoding> {
    /// Creates a new `LogMessageSocket` from the given `socket` that reads the legacy format.
    pub fn new(
        socket: zx::Socket,
        source: Arc<SourceIdentity>,
        forwarder: Forwarder<LegacyEncoding>,
    ) -> Result<Self, io::Error> {
        Ok(Self {
            socket: fasync::Socket::from_socket(socket)?,
            buffer: [0; MAX_DATAGRAM_LEN],
            source,
            forwarder,
            _encoder: PhantomData,
        })
    }
}

impl LogMessageSocket<StructuredEncoding> {
    /// Creates a new `LogMessageSocket` from the given `socket` that reads the structured log
    /// format.
    pub fn new_structured(
        socket: zx::Socket,
        source: Arc<SourceIdentity>,
        forwarder: Forwarder<StructuredEncoding>,
    ) -> Result<Self, io::Error> {
        Ok(Self {
            socket: fasync::Socket::from_socket(socket)?,
            buffer: [0; MAX_DATAGRAM_LEN],
            source,
            forwarder,
            _encoder: PhantomData,
        })
    }
}

impl<E> LogMessageSocket<E>
where
    E: Encoding + Unpin,
{
    pub async fn next(&mut self) -> Result<Message, StreamError> {
        let len = self.socket.read(&mut self.buffer).await?;

        if len == 0 {
            return Err(StreamError::Closed);
        }

        let msg_bytes = &self.buffer[..len];
        let message = E::parse_message(msg_bytes)?;
        self.forwarder.maybe_send(msg_bytes);
        Ok(message)
    }
}

/// Optionally forwards all log messages to another socket.
#[derive(Clone, Debug)]
pub struct Forwarder<E> {
    target: Option<Arc<zx::Socket>>,
    _encoding: PhantomData<E>,
}

impl<E> Forwarder<E> {
    /// Creates a nop forwarder.
    pub fn new() -> Self {
        Self { target: None, _encoding: PhantomData }
    }

    /// Start forwarding messages to the `target` socket.
    pub fn init(&mut self, target: zx::Socket) {
        self.target = Some(Arc::new(target));
    }

    /// Forward the message bytes if we've been initialized.
    pub fn maybe_send(&self, bytes: &[u8]) {
        if let Some(target) = self.target.as_ref() {
            target.write(bytes).ok();
        }
    }
}

#[cfg(test)]
mod tests {
    use super::super::message::{
        fx_log_packet_t, LogsField, LogsHierarchy, LogsProperty, Message, Severity, METADATA_SIZE,
        PLACEHOLDER_MONIKER, PLACEHOLDER_URL,
    };
    use super::*;
    use diagnostic_streams::{
        encode::Encoder, Argument, Record, Severity as StreamSeverity, Value,
    };
    use std::io::Cursor;

    #[fasync::run_until_stalled(test)]
    async fn logger_stream_test() {
        let (sin, sout) = zx::Socket::create(zx::SocketOpts::DATAGRAM).unwrap();
        let mut packet: fx_log_packet_t = Default::default();
        packet.metadata.pid = 1;
        packet.metadata.severity = 0x30; // INFO
        packet.data[0] = 5;
        packet.fill_data(1..6, 'A' as _);
        packet.fill_data(7..12, 'B' as _);

        let mut ls =
            LogMessageSocket::new(sout, Arc::new(SourceIdentity::empty()), Forwarder::new())
                .unwrap();
        sin.write(packet.as_bytes()).unwrap();
        let expected_p = Message::new(
            zx::Time::from_nanos(packet.metadata.time),
            Severity::Info,
            METADATA_SIZE + 6 /* tag */+ 6, /* msg */
            packet.metadata.dropped_logs as u64,
            PLACEHOLDER_MONIKER,
            PLACEHOLDER_URL,
            LogsHierarchy::new(
                "root",
                vec![
                    LogsProperty::Uint(LogsField::ProcessId, packet.metadata.pid),
                    LogsProperty::Uint(LogsField::ThreadId, packet.metadata.tid),
                    LogsProperty::String(LogsField::Tag, "AAAAA".to_string()),
                    LogsProperty::String(LogsField::Msg, "BBBBB".to_string()),
                ],
                vec![],
            ),
        );

        let result_message = ls.next().await.unwrap();
        assert_eq!(result_message, expected_p);

        // write one more time
        sin.write(packet.as_bytes()).unwrap();

        let result_message = ls.next().await.unwrap();
        assert_eq!(result_message, expected_p);
    }

    #[fasync::run_until_stalled(test)]
    async fn structured_logger_stream_test() {
        let (sin, sout) = zx::Socket::create(zx::SocketOpts::DATAGRAM).unwrap();
        let timestamp = 107;
        let record = Record {
            timestamp,
            severity: StreamSeverity::Fatal,
            arguments: vec![
                Argument { name: "key".to_string(), value: Value::Text("value".to_string()) },
                Argument { name: "tag".to_string(), value: Value::Text("tag-a".to_string()) },
            ],
        };
        let mut buffer = Cursor::new(vec![0u8; 1024]);
        let mut encoder = Encoder::new(&mut buffer);
        encoder.write_record(&record).unwrap();
        let encoded = &buffer.get_ref()[..buffer.position() as usize];

        let expected_p = Message::new(
            timestamp,
            Severity::Fatal,
            encoded.len(),
            0, // dropped logs
            PLACEHOLDER_MONIKER,
            PLACEHOLDER_URL,
            LogsHierarchy::new(
                "root",
                vec![
                    LogsProperty::String(LogsField::Other("key".to_string()), "value".to_string()),
                    LogsProperty::String(LogsField::Tag, "tag-a".to_string()),
                ],
                vec![],
            ),
        );

        let mut stream = LogMessageSocket::new_structured(
            sout,
            Arc::new(SourceIdentity::empty()),
            Forwarder::new(),
        )
        .unwrap();

        sin.write(encoded).unwrap();
        let result_message = stream.next().await.unwrap();
        assert_eq!(result_message, expected_p);

        // write again
        sin.write(encoded).unwrap();
        let result_message = stream.next().await.unwrap();
        assert_eq!(result_message, expected_p);
    }
}

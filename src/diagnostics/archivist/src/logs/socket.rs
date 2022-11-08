// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

use super::stats::LogStreamStats;
use crate::logs::error::StreamError;
use crate::logs::stored_message::StoredMessage;
use diagnostics_message::MAX_DATAGRAM_LEN;
use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use futures::io::{self, AsyncReadExt};
use std::{marker::PhantomData, sync::Arc};

/// An `Encoding` is able to parse a `Message` from raw bytes.
pub trait Encoding {
    /// Attempt to parse a message from the given buffer
    fn wrap_bytes(bytes: &[u8], stats: Arc<LogStreamStats>) -> Result<StoredMessage, StreamError>;
}

/// An encoding that can parse the legacy [logger/syslog wire format]
///
/// [logger/syslog wire format]: https://fuchsia.googlesource.com/fuchsia/+/HEAD/zircon/system/ulib/syslog/include/lib/syslog/wire_format.h
#[derive(Clone, Debug)]
pub struct LegacyEncoding;

/// An encoding that can parse the [structured log format]
///
/// [structured log format]: https://fuchsia.dev/fuchsia-src/development/logs/encodings
#[derive(Clone, Debug)]
pub struct StructuredEncoding;

impl Encoding for LegacyEncoding {
    fn wrap_bytes(buf: &[u8], stats: Arc<LogStreamStats>) -> Result<StoredMessage, StreamError> {
        StoredMessage::legacy(buf, stats)
    }
}

impl Encoding for StructuredEncoding {
    fn wrap_bytes(buf: &[u8], stats: Arc<LogStreamStats>) -> Result<StoredMessage, StreamError> {
        StoredMessage::structured(buf, stats)
    }
}

#[must_use = "don't drop logs on the floor please!"]
pub struct LogMessageSocket<E> {
    stats: Arc<LogStreamStats>,
    socket: fasync::Socket,
    buffer: [u8; MAX_DATAGRAM_LEN],
    _encoder: PhantomData<E>,
}

impl LogMessageSocket<LegacyEncoding> {
    /// Creates a new `LogMessageSocket` from the given `socket` that reads the legacy format.
    pub fn new(socket: zx::Socket, stats: Arc<LogStreamStats>) -> Result<Self, io::Error> {
        stats.open_socket();
        Ok(Self {
            socket: fasync::Socket::from_socket(socket)?,
            buffer: [0; MAX_DATAGRAM_LEN],
            stats,
            _encoder: PhantomData,
        })
    }
}

impl LogMessageSocket<StructuredEncoding> {
    /// Creates a new `LogMessageSocket` from the given `socket` that reads the structured log
    /// format.
    pub fn new_structured(
        socket: zx::Socket,
        stats: Arc<LogStreamStats>,
    ) -> Result<Self, io::Error> {
        stats.open_socket();
        Ok(Self {
            socket: fasync::Socket::from_socket(socket)?,
            buffer: [0; MAX_DATAGRAM_LEN],
            stats,
            _encoder: PhantomData,
        })
    }
}

impl<E> LogMessageSocket<E>
where
    E: Encoding + Unpin,
{
    pub async fn next(&mut self) -> Result<StoredMessage, StreamError> {
        let len = self.socket.read(&mut self.buffer).await?;

        if len == 0 {
            return Err(StreamError::Closed);
        }

        let msg_bytes = &self.buffer[..len];
        E::wrap_bytes(msg_bytes, self.stats.clone())
    }
}

impl<E> Drop for LogMessageSocket<E> {
    fn drop(&mut self) {
        self.stats.close_socket();
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::testing::TEST_IDENTITY;
    use diagnostics_data::{LogsField, Severity};
    use diagnostics_log_encoding::{
        encode::Encoder, Argument, Record, Severity as StreamSeverity, Value,
    };
    use diagnostics_message::{fx_log_packet_t, METADATA_SIZE};
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

        let mut ls = LogMessageSocket::new(sout, Default::default()).unwrap();
        sin.write(packet.as_bytes()).unwrap();
        let expected_p = diagnostics_data::LogsDataBuilder::new(diagnostics_data::BuilderArgs {
            timestamp_nanos: zx::Time::from_nanos(packet.metadata.time).into(),
            component_url: Some(TEST_IDENTITY.url.clone()),
            moniker: TEST_IDENTITY.to_string(),
            severity: Severity::Info,
        })
        .set_pid(packet.metadata.pid)
        .set_tid(packet.metadata.tid)
        .add_tag("AAAAA")
        .set_message("BBBBB".to_string())
        .build();

        let bytes = ls.next().await.unwrap();
        assert_eq!(bytes.size(), METADATA_SIZE + 6 /* tag */+ 6 /* msg */,);
        let result_message = bytes.parse(&TEST_IDENTITY).unwrap();
        assert_eq!(result_message, expected_p);

        // write one more time
        sin.write(packet.as_bytes()).unwrap();

        let result_message = ls.next().await.unwrap().parse(&TEST_IDENTITY).unwrap();
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

        let expected_p = diagnostics_data::LogsDataBuilder::new(diagnostics_data::BuilderArgs {
            timestamp_nanos: timestamp.into(),
            component_url: Some(TEST_IDENTITY.url.clone()),
            moniker: TEST_IDENTITY.to_string(),
            severity: Severity::Fatal,
        })
        .add_tag("tag-a")
        .add_key(diagnostics_data::LogsProperty::String(
            LogsField::Other("key".to_string()),
            "value".to_string(),
        ))
        .build();

        let mut stream = LogMessageSocket::new_structured(sout, Default::default()).unwrap();

        sin.write(encoded).unwrap();
        let bytes = stream.next().await.unwrap();
        let result_message = bytes.parse(&TEST_IDENTITY).unwrap();
        assert_eq!(bytes.size(), encoded.len());
        assert_eq!(result_message, expected_p);

        // write again
        sin.write(encoded).unwrap();
        let result_message = stream.next().await.unwrap().parse(&TEST_IDENTITY).unwrap();
        assert_eq!(result_message, expected_p);
    }
}

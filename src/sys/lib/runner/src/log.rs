// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    async_trait::async_trait,
    fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::{
        io::{self, AsyncRead},
        prelude::*,
        ready,
        task::{Context, Poll},
    },
    std::{boxed::Box, cell::RefCell, pin::Pin},
    thiserror::Error,
};

const NEWLINE: u8 = b'\n';

thread_local! {
    pub static BUFFER:
        RefCell<[u8; 4096]> = RefCell::new([0; 4096]);
}

/// Error returned from draining LoggerStream or writing to LogWriter.
#[derive(Debug, Error)]
pub enum LogError {
    /// Error encountered when draining LoggerStream.
    #[error("can't get logs: {:?}", _0)]
    Read(std::io::Error),

    /// Error encountered when writing to LogWriter.
    #[error("can't write logs: {:?}", _0)]
    Write(std::io::Error),
}

/// Object capable of writing a stream of bytes.
#[async_trait]
pub trait LogWriter: Send {
    /// Write bytes to stream.
    /// If operation is successful, the number of bytes written should be returned.
    /// Otherwise, implementations of this method should return LogError::Write.
    async fn write(&mut self, bytes: &[u8]) -> Result<usize, LogError>;
}

/// A stream bound to a socket where a source stream is captured.
/// For example, stdout and stderr streams can be redirected to the contained
/// socket and captured.
#[must_use = "futures/streams"]
pub struct LoggerStream {
    socket: fasync::Socket,
}

impl Unpin for LoggerStream {}

impl LoggerStream {
    /// Create a LoggerStream from the provided zx::Socket. The `socket` object
    /// should be bound to its intented source stream (e.g. "stdout").
    pub fn new(socket: zx::Socket) -> Result<LoggerStream, zx::Status> {
        let l = LoggerStream { socket: fasync::Socket::from_socket(socket)? };
        Ok(l)
    }
}

impl Stream for LoggerStream {
    type Item = io::Result<Vec<u8>>;
    fn poll_next(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        BUFFER.with(|b| {
            let mut b = b.borrow_mut();
            let len = ready!(Pin::new(&mut self.socket).poll_read(cx, &mut *b)?);
            if len == 0 {
                return Poll::Ready(None);
            }
            let bytes = &b[0..len];
            Poll::Ready(Some(bytes.to_vec()).map(Ok))
        })
    }
}

/// Drain the `stream` and write all of its contents to `writer`. Bytes are
/// delimited by newline and each line will be passed to `writer.write`.
pub async fn buffer_and_drain_logger(
    mut stream: LoggerStream,
    writer: Box<&mut dyn LogWriter>,
) -> Result<(), LogError> {
    let mut buf: Vec<u8> = vec![];
    while let Some(bytes) = stream.try_next().await.map_err(LogError::Read)? {
        if bytes.is_empty() {
            continue;
        }

        // buffer by newline, find last newline and send message till then,
        // store rest in buffer.
        buf.extend(bytes);
        if let Some(i) = buf.iter().rposition(|&x| x == NEWLINE) {
            writer.write(buf.drain(0..=i).as_slice()).await?;
        }
    }

    if buf.len() > 0 {
        //  Flush remainder of buffer in case the last message isn't terminated with a newline.
        writer.write(&buf).await?;
    }

    Ok(())
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        anyhow::{Context, Error},
        async_trait::async_trait,
        fuchsia_async as fasync, fuchsia_zircon as zx,
        rand::{distributions::Alphanumeric, thread_rng, Rng},
    };

    struct TestLogWriter {
        lines: Vec<Vec<u8>>,
    }

    impl TestLogWriter {
        fn new() -> TestLogWriter {
            Self { lines: Vec::new() }
        }

        fn get_lines_as_str(&self) -> Result<Vec<String>, std::str::Utf8Error> {
            let mut result: Vec<String> = Vec::new();
            for line in self.lines.iter() {
                result.push(std::str::from_utf8(line)?.to_owned());
            }
            Ok(result)
        }
    }

    #[async_trait]
    impl LogWriter for TestLogWriter {
        async fn write(&mut self, bytes: &[u8]) -> Result<usize, LogError> {
            self.lines.push(bytes.to_vec());
            Ok(bytes.len())
        }
    }

    macro_rules! write_to_socket {
        ($socket:expr, $msg:expr) => {{
            fasync::Task::spawn(async move {
                let _ = $socket.write($msg.as_bytes()).unwrap();
            })
            .detach();
        }};
    }

    macro_rules! join_lines {
        ( $( $line:literal ),* ) => {
            {
                let mut buf: Vec<&str> = Vec::new();
                $(
                    buf.push($line);
                )*
                buf.join("\n")
            }
        };
    }

    #[fasync::run_singlethreaded(test)]
    async fn logger_stream_captures_simple_msg() -> Result<(), Error> {
        let (tx, rx) =
            zx::Socket::create(zx::SocketOpts::STREAM).context("Failed to create socket")?;
        let stream = LoggerStream::new(rx).context("Failed to create LoggerStream")?;
        write_to_socket!(tx, join_lines!("Hello World!", "Hola Mundo!"));

        let actual = read_from_stream(stream).await?;
        let expected = String::from("Hello World!\nHola Mundo!");

        assert_eq!(actual, expected);
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn logger_stream_captures_utf8_msg() -> Result<(), Error> {
        let (tx, rx) =
            zx::Socket::create(zx::SocketOpts::STREAM).context("Failed to create socket")?;
        let stream = LoggerStream::new(rx).context("Failed to create LoggerStream")?;
        let msg = "Hello UTF8 World! 👱👱🏻👱🏼👱🏽👱🏾👱🏿";
        write_to_socket!(tx, String::from(msg));

        let actual = read_from_stream(stream).await?;
        let expected = String::from(msg);

        assert_eq!(actual, expected);
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn logger_stream_captures_large_msg() -> Result<(), Error> {
        let (tx, rx) =
            zx::Socket::create(zx::SocketOpts::STREAM).context("Failed to create socket")?;
        let stream = LoggerStream::new(rx).context("Failed to create LoggerStream")?;
        let msg =
            thread_rng().sample_iter(&Alphanumeric).take(10000).map(char::from).collect::<String>();
        // We have to clone here because the macro write_to_socket! borrows `msg`.
        let expected = msg.clone();
        write_to_socket!(tx, msg);

        let actual = read_from_stream(stream).await?;

        assert_eq!(actual, expected);
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn drain_logger_splits_content_by_newline() -> Result<(), Error> {
        let (tx, rx) =
            zx::Socket::create(zx::SocketOpts::STREAM).context("Failed to create socket")?;
        let stream = LoggerStream::new(rx).context("Failed to create LoggerStream")?;
        write_to_socket!(tx, join_lines!("Hello World!", "Hola Mundo!"));
        let mut writer = TestLogWriter::new();

        buffer_and_drain_logger(stream, Box::new(&mut writer)).await?;
        let actual = writer.get_lines_as_str().context("Failed to parse lines")?;

        assert_eq!(actual, vec!["Hello World!\n", "Hola Mundo!"]);
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn drain_logger_return_error_if_stream_polls_err() -> Result<(), Error> {
        let (tx, rx) =
            zx::Socket::create(zx::SocketOpts::STREAM).context("Failed to create socket")?;
        // A closed socket should yield an error when stream is polled.
        rx.half_close()?;
        tx.half_close()?;
        let stream = LoggerStream::new(rx).context("Failed to create LoggerStream")?;
        let mut writer = TestLogWriter::new();

        let result = buffer_and_drain_logger(stream, Box::new(&mut writer)).await;

        assert!(result.is_err());
        Ok(())
    }

    async fn read_from_stream(stream: LoggerStream) -> Result<String, Error> {
        let result = stream.try_concat().await.context("Failed to read stream")?;
        let msg = std::str::from_utf8(&result).context("Failed to parse bytes")?;
        Ok(msg.to_owned())
    }
}

// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Helpers for capturing logs from Fuchsia processes.

use {
    crate::errors::FdioError, fdio::fdio_sys, fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::prelude::*, std::num::NonZeroUsize, std::os::unix::prelude::AsRawFd, thiserror::Error,
};

/// Buffer size for socket read calls to `LoggerStream::buffer_and_drain`.
const SOCKET_BUFFER_SIZE: usize = 2048;
const NEWLINE: u8 = b'\n';

/// Error returned by this library.
#[derive(Debug, PartialEq, Eq, Error, Clone)]
pub enum LoggerError {
    #[error("fdio error: {:?}", _0)]
    Fdio(#[from] FdioError),

    #[error("cannot create socket: {:?}", _0)]
    CreateSocket(zx::Status),

    #[error("invalid socket: {:?}", _0)]
    InvalidSocket(zx::Status),
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

/// Creates a combined socket handle for stdout and stderr and hooks them to same socket.
/// It also wraps the socket into stream and returns it back.
pub fn create_std_combined_log_stream(
) -> Result<(LoggerStream, zx::Handle, zx::Handle), LoggerError> {
    let (client, log) =
        zx::Socket::create(zx::SocketOpts::STREAM).map_err(LoggerError::CreateSocket)?;
    let mut stderr_file_handle = zx::sys::ZX_HANDLE_INVALID;

    let std_file: std::fs::File =
        fdio::create_fd(log.into()).map_err(|s| LoggerError::Fdio(FdioError::Create(s)))?;

    unsafe {
        let status = fdio_sys::fdio_fd_clone(
            std_file.as_raw_fd(),
            &mut stderr_file_handle as *mut zx::sys::zx_handle_t,
        );
        if let Err(s) = zx::Status::ok(status) {
            return Err(LoggerError::Fdio(FdioError::Clone(s)));
        }
    }

    let stdout_file_handle =
        fdio::transfer_fd(std_file).map_err(|s| LoggerError::Fdio(FdioError::Transfer(s)))?;

    unsafe {
        Ok((
            LoggerStream::new(client).map_err(LoggerError::InvalidSocket)?,
            stdout_file_handle,
            zx::Handle::from_raw(stderr_file_handle),
        ))
    }
}

/// Creates a socket handle for stdout/stderr and hooks it to a file handle.
/// It also wraps the socket into stream and returns it back.
pub fn create_log_stream() -> Result<(LoggerStream, zx::Handle), LoggerError> {
    let (client, log) =
        zx::Socket::create(zx::SocketOpts::STREAM).map_err(LoggerError::CreateSocket)?;

    let std_file =
        fdio::create_fd(log.into()).map_err(|s| LoggerError::Fdio(FdioError::Create(s)))?;

    let file_handle =
        fdio::transfer_fd(std_file).map_err(|s| LoggerError::Fdio(FdioError::Transfer(s)))?;

    Ok((LoggerStream::new(client).map_err(LoggerError::InvalidSocket)?, file_handle))
}
/// Collects logs in background and gives a way to collect those logs.
pub struct LogStreamReader {
    fut: future::RemoteHandle<Result<Vec<u8>, LogError>>,
}

impl LogStreamReader {
    pub fn new(logger: LoggerStream) -> Self {
        let (logger_handle, logger_fut) = logger.read_to_end().remote_handle();
        fasync::Task::spawn(logger_handle).detach();
        Self { fut: logger_fut }
    }

    /// Retrieve all logs.
    pub async fn get_logs(self) -> Result<Vec<u8>, LogError> {
        self.fut.await
    }
}

/// A stream bound to a socket where a source stream is captured.
/// For example, stdout and stderr streams can be redirected to the contained
/// socket and captured.
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

    /// Reads all bytes from socket.
    pub async fn read_to_end(mut self) -> Result<Vec<u8>, LogError> {
        let mut buffer: Vec<u8> = Vec::new();
        let _bytes_read = self.socket.read_to_end(&mut buffer).await.map_err(LogError::Read)?;
        Ok(buffer)
    }

    /// Drain the `stream` and write all of its contents to `writer`. Bytes are
    /// delimited by newline and each line will be passed to `writer.write`.
    pub async fn buffer_and_drain(mut self, writer: &mut SocketLogWriter) -> Result<(), LogError> {
        let mut message_buffer: Vec<u8> = Vec::new();
        let mut socket_buffer: Vec<u8> = vec![0; SOCKET_BUFFER_SIZE];

        while let Some(bytes_read) = NonZeroUsize::new(
            self.socket.read(&mut socket_buffer[..]).await.map_err(LogError::Read)?,
        ) {
            let bytes_read = bytes_read.get();
            message_buffer.extend(&socket_buffer[..bytes_read]);

            if let Some(last_newline_pos) = message_buffer.iter().rposition(|&x| x == NEWLINE) {
                let () = writer.write(message_buffer.drain(..=last_newline_pos).as_slice()).await?;
            }

            while message_buffer.len() >= SOCKET_BUFFER_SIZE {
                let () = writer.write(message_buffer.drain(..).as_slice()).await?;
            }
        }

        if !message_buffer.is_empty() {
            let () = writer.write(&message_buffer[..]).await?;
        }

        Ok(())
    }

    /// Take the underlying socket of this object.
    pub fn take_socket(self) -> fasync::Socket {
        self.socket
    }
}

/// Utility struct to write to socket asynchrously.
pub struct SocketLogWriter {
    logger: fasync::Socket,
}

impl SocketLogWriter {
    pub fn new(logger: fasync::Socket) -> Self {
        Self { logger }
    }

    pub async fn write_str(&mut self, s: &str) -> Result<(), LogError> {
        self.write(s.as_bytes()).await
    }

    pub async fn write(&mut self, bytes: &[u8]) -> Result<(), LogError> {
        self.logger.write_all(bytes).await.map_err(LogError::Write)
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        anyhow::{format_err, Context, Error},
        assert_matches::assert_matches,
        fuchsia_async::{self as fasync, futures::try_join},
        fuchsia_zircon as zx,
        rand::{
            distributions::{Alphanumeric, DistString as _},
            thread_rng,
        },
        std::mem::drop,
        test_case::test_case,
    };

    #[fuchsia_async::run_singlethreaded(test)]
    async fn log_writer_reader_work() {
        let (sock1, sock2) = zx::Socket::create(zx::SocketOpts::STREAM).unwrap();
        let mut log_writer = SocketLogWriter::new(fasync::Socket::from_socket(sock1).unwrap());

        let reader = LoggerStream::new(sock2).unwrap();
        let reader = LogStreamReader::new(reader);

        log_writer.write_str("this is string one.").await.unwrap();
        log_writer.write_str("this is string two.").await.unwrap();
        drop(log_writer);

        let actual = reader.get_logs().await.unwrap();
        let actual = std::str::from_utf8(&actual).unwrap();
        assert_eq!(actual, "this is string one.this is string two.".to_owned());
    }

    #[test_case(String::from("Hello World!") ; "consumes_simple_msg")]
    #[test_case(get_random_string(10000) ; "consumes_large_msg")]
    #[fasync::run_singlethreaded(test)]
    async fn logger_stream_read_to_end(msg: String) -> Result<(), Error> {
        let (stream, tx) = create_logger_stream()?;

        let () = take_and_write_to_socket(tx, &msg)?;
        let result = stream.read_to_end().await.context("Failed to read from socket")?;
        let actual = std::str::from_utf8(&result).context("Failed to parse bytes")?.to_owned();

        assert_eq!(actual, msg);
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn logger_stream_read_to_end_consumes_concat_msgs() -> Result<(), Error> {
        let (stream, tx) = create_logger_stream()?;
        let msgs =
            vec!["Hello World!".to_owned(), "Hola Mundo!".to_owned(), "你好，世界!".to_owned()];

        for msg in msgs.iter() {
            let () = write_to_socket(&tx, &msg)?;
        }
        std::mem::drop(tx);
        let result = stream.read_to_end().await.context("Failed to read from socket")?;
        let actual = std::str::from_utf8(&result).context("Failed to parse bytes")?.to_owned();

        assert_eq!(actual, msgs.join(""));
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn buffer_and_drain_reads_message_until_last_newline() -> Result<(), Error> {
        let (stream, tx) = create_logger_stream()?;
        let (mut logger, rx) = create_datagram_logger()?;
        let msg = "Hello World\nHola Mundo!\n你好，世界!";

        let () = take_and_write_to_socket(tx, msg)?;
        let (actual, ()) = try_join!(read_all_messages(rx), async move {
            stream.buffer_and_drain(&mut logger).await.context("Failed to drain stream")
        },)?;

        assert_eq!(
            actual,
            vec![String::from("Hello World\nHola Mundo!\n"), String::from("你好，世界!")],
        );

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn buffer_and_drain_dumps_full_buffer_if_no_newline_seen() -> Result<(), Error> {
        let (stream, tx) = create_logger_stream()?;
        let (mut logger, rx) = create_datagram_logger()?;

        let ((), ()) = try_join!(
            async move {
                let msg = get_random_string(SOCKET_BUFFER_SIZE);
                // First write up to (SOCKET_BUFFER_SIZE - 1) so that we can
                // assert that buffer isn't drained prematurely.
                let () = write_to_socket(&tx, &msg[..SOCKET_BUFFER_SIZE - 1])?;

                // Temporarily convert fasync::Socket back to zx::Socket so that
                // we can use non-blocking `read` call.
                let rx = rx.into_zx_socket();
                let mut buffer = vec![0u8; SOCKET_BUFFER_SIZE];
                let maybe_bytes_read = rx.read(&mut buffer);
                assert_eq!(maybe_bytes_read, Err(zx::Status::SHOULD_WAIT));

                // Write last byte and convert zx::Socket back to fasync::Socket.
                let () = write_to_socket(&tx, &msg[SOCKET_BUFFER_SIZE - 1..SOCKET_BUFFER_SIZE])?;
                let mut rx = fasync::Socket::from_socket(rx)
                    .context("Failed to convert to fasync::Socket")?;
                let bytes_read =
                    rx.read(&mut buffer).await.context("Failed to read from socket")?;
                let msg_written = std::str::from_utf8(&buffer).context("Failed to parse bytes")?;

                assert_eq!(bytes_read, SOCKET_BUFFER_SIZE);
                assert_eq!(msg_written, msg);

                Ok(())
            },
            async move { stream.buffer_and_drain(&mut logger).await.context("Failed to drain stream") },
        )?;

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn buffer_and_drain_return_error_if_stream_polls_err() -> Result<(), Error> {
        let (tx, rx) =
            zx::Socket::create(zx::SocketOpts::STREAM).context("Failed to create socket")?;
        // A closed socket should yield an error when stream is polled.
        let () = rx.half_close()?;
        let () = tx.half_close()?;
        let stream = LoggerStream::new(rx).context("Failed to create LoggerStream")?;
        let (mut logger, _rx) = create_datagram_logger()?;

        let result = stream.buffer_and_drain(&mut logger).await;

        assert_matches!(result, Err(LogError::Read(_)));
        Ok(())
    }

    async fn read_all_messages(socket: fasync::Socket) -> Result<Vec<String>, Error> {
        let mut results = Vec::new();
        let mut stream = socket.into_datagram_stream();
        while let Some(bytes) = stream.try_next().await.context("Failed to read socket stream")? {
            results.push(
                std::str::from_utf8(&bytes).context("Failed to parse bytes into utf8")?.to_owned(),
            );
        }

        Ok(results)
    }

    fn take_and_write_to_socket(socket: zx::Socket, message: &str) -> Result<(), Error> {
        write_to_socket(&socket, &message)
    }

    fn write_to_socket(socket: &zx::Socket, message: &str) -> Result<(), Error> {
        let bytes_written =
            socket.write(message.as_bytes()).context("Failed to write to socket")?;
        match bytes_written == message.len() {
            true => Ok(()),
            false => Err(format_err!("Bytes written to socket doesn't match len of message. Message len = {}. Bytes written = {}", message.len(), bytes_written)),
        }
    }

    fn create_datagram_logger() -> Result<(SocketLogWriter, fasync::Socket), Error> {
        let (tx, rx) =
            zx::Socket::create(zx::SocketOpts::DATAGRAM).context("Failed to create zx::Socket")?;
        let logger = SocketLogWriter::new(
            fasync::Socket::from_socket(tx).context("Failed to create fasync::Socket")?,
        );
        let rx = fasync::Socket::from_socket(rx).context("Failed to create fasync::Socket")?;
        Ok((logger, rx))
    }

    fn create_logger_stream() -> Result<(LoggerStream, zx::Socket), Error> {
        let (tx, rx) =
            zx::Socket::create(zx::SocketOpts::STREAM).context("Failed to create socket")?;
        let stream = LoggerStream::new(rx).context("Failed to create LoggerStream")?;
        Ok((stream, tx))
    }

    fn get_random_string(size: usize) -> String {
        Alphanumeric.sample_string(&mut thread_rng(), size)
    }
}

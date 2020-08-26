// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Helpers for capturing logs from Fuchsia processes.

use {
    crate::errors::FdioError,
    fdio::fdio_sys,
    fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::{
        io::{self, AsyncRead},
        prelude::*,
        ready,
        task::{Context, Poll},
    },
    std::{cell::RefCell, pin::Pin},
    thiserror::Error,
    zx::HandleBased,
};

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

/// Logger stream to read logs from a socket
#[must_use = "futures/streams"]
pub struct LoggerStream {
    socket: fasync::Socket,
}

impl Unpin for LoggerStream {}
thread_local! {
    pub static BUFFER:
        RefCell<[u8; 4096]> = RefCell::new([0; 4096]);
}

impl LoggerStream {
    /// Creates a new `LoggerStream` for given `socket`.
    pub fn new(socket: zx::Socket) -> Result<LoggerStream, LoggerError> {
        let l = LoggerStream {
            socket: fasync::Socket::from_socket(socket).map_err(LoggerError::InvalidSocket)?,
        };
        Ok(l)
    }
}

fn process_log_bytes(bytes: &[u8]) -> Vec<u8> {
    bytes.to_vec()
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
            Poll::Ready(Some(process_log_bytes(&b[0..len])).map(Ok))
        })
    }
}

/// Creates socket handle for stdout and stderr and hooks them to same socket.
/// It also wraps the socket into stream and returns it back.
pub fn create_log_stream() -> Result<(LoggerStream, zx::Handle, zx::Handle), LoggerError> {
    let (client, log) =
        zx::Socket::create(zx::SocketOpts::STREAM).map_err(LoggerError::CreateSocket)?;
    let mut stdout_file_handle = zx::sys::ZX_HANDLE_INVALID;
    let mut stderr_file_handle = zx::sys::ZX_HANDLE_INVALID;

    unsafe {
        let mut std_fd: i32 = -1;

        let mut status = fdio::fdio_sys::fdio_fd_create(log.into_raw(), &mut std_fd);
        if let Err(s) = zx::Status::ok(status) {
            return Err(LoggerError::Fdio(FdioError::Create(s)));
        }

        status =
            fdio_sys::fdio_fd_clone(std_fd, &mut stderr_file_handle as *mut zx::sys::zx_handle_t);
        if let Err(s) = zx::Status::ok(status) {
            return Err(LoggerError::Fdio(FdioError::Clone(s)));
        }

        status = fdio_sys::fdio_fd_transfer(
            std_fd,
            &mut stdout_file_handle as *mut zx::sys::zx_handle_t,
        );
        if let Err(s) = zx::Status::ok(status) {
            return Err(LoggerError::Fdio(FdioError::Transfer(s)));
        }

        Ok((
            LoggerStream::new(client)?,
            zx::Handle::from_raw(stdout_file_handle),
            zx::Handle::from_raw(stderr_file_handle),
        ))
    }
}

/// Error while reading/writing log using socket
#[derive(Debug, Error)]
pub enum LogError {
    #[error("can't get logs: {:?}", _0)]
    Read(std::io::Error),

    #[error("can't write logs: {:?}", _0)]
    Write(std::io::Error),
}

/// Collects logs in background and gives a way to collect those logs.
pub struct LogStreamReader {
    fut: future::RemoteHandle<Result<Vec<u8>, std::io::Error>>,
}

impl LogStreamReader {
    pub fn new(logger: LoggerStream) -> Self {
        let (logger_handle, logger_fut) = logger.try_concat().remote_handle();
        fasync::Task::local(logger_handle).detach();
        Self { fut: logger_fut }
    }

    /// Retrive all logs.
    pub async fn get_logs(self) -> Result<Vec<u8>, LogError> {
        self.fut.await.map_err(LogError::Read)
    }
}

/// Utility struct to write to socket asynchrously.
pub struct LogWriter {
    logger: fasync::Socket,
}

impl LogWriter {
    pub fn new(logger: fasync::Socket) -> Self {
        Self { logger }
    }

    pub async fn write_str(&mut self, s: &str) -> Result<usize, LogError> {
        self.write(s.as_bytes()).await
    }

    pub async fn write(&mut self, bytes: &[u8]) -> Result<usize, LogError> {
        self.logger.write(bytes).await.map_err(LogError::Write)
    }
}

#[cfg(test)]
mod tests {
    use {super::*, fuchsia_zircon as zx, std::mem::drop};

    #[fuchsia_async::run_singlethreaded(test)]
    async fn log_writer_reader_work() {
        let (sock1, sock2) = zx::Socket::create(zx::SocketOpts::DATAGRAM).unwrap();
        let mut log_writer = LogWriter::new(fasync::Socket::from_socket(sock1).unwrap());

        let reader = LoggerStream::new(sock2).unwrap();
        let reader = LogStreamReader::new(reader);

        log_writer.write_str("this is string one.").await.unwrap();
        log_writer.write_str("this is string two.").await.unwrap();
        drop(log_writer);

        let actual = reader.get_logs().await.unwrap();
        let actual = std::str::from_utf8(&actual).unwrap();
        assert_eq!(actual, "this is string one.this is string two.".to_owned());
    }
}

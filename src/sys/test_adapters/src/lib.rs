// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::Fail,
    fdio::fdio_sys,
    fuchsia_async as fasync, fuchsia_runtime as runtime, fuchsia_zircon as zx,
    futures::{
        io::{self, AsyncRead},
        prelude::*,
        ready,
        task::{Context, Poll},
    },
    runtime::{HandleInfo, HandleType},
    std::{
        cell::RefCell,
        ffi::{CStr, CString},
        path::Path,
        pin::Pin,
    },
    zx::{HandleBased, Process},
};

/// An error encountered while calling fdio operations.
#[derive(Debug, PartialEq, Eq, Fail)]
pub enum FdioError {
    #[fail(display = "Cannot create file descriptor: {:?}", _0)]
    Create(#[cause] zx::Status),

    #[fail(display = "Cannot clone file descriptor: {:?}", _0)]
    Clone(#[cause] zx::Status),

    #[fail(display = "Cannot transfer file descriptor: {:?}", _0)]
    Transfer(#[cause] zx::Status),

    #[fail(display = "Cannot create process: {:?}, {}", _0, _1)]
    ProcessCreation(#[cause] zx::Status, String),
}

/// Error returned by this library.
#[derive(Debug, PartialEq, Eq, Fail)]
pub enum Error {
    #[fail(display = "Path {} does not exist.", _0)]
    PathDoesNotExist(String),

    #[fail(display = "Failed to parse file name from {}", _0)]
    NoFileName(String),

    #[fail(display = "fdio error: {:?}", _0)]
    Fdio(#[cause] FdioError),

    #[fail(display = "cannot create socket: {:?}", _0)]
    CreateSocket(#[cause] zx::Status),

    #[fail(display = "invalid socket: {:?}", _0)]
    InvalidSocket(#[cause] zx::Status),

    #[fail(display = "cannot create job: {:?}", _0)]
    JobCreation(#[cause] zx::Status),
}

impl From<FdioError> for Error {
    fn from(e: FdioError) -> Self {
        Error::Fdio(e)
    }
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
    pub fn new(socket: zx::Socket) -> Result<LoggerStream, Error> {
        let l = LoggerStream {
            socket: fasync::Socket::from_socket(socket).map_err(Error::InvalidSocket)?,
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
pub fn create_log_stream() -> Result<(LoggerStream, zx::Handle, zx::Handle), Error> {
    let (client, log) =
        zx::Socket::create(zx::SocketOpts::DATAGRAM).map_err(Error::CreateSocket)?;
    let mut stdout_file_handle = zx::sys::ZX_HANDLE_INVALID;
    let mut stderr_file_handle = zx::sys::ZX_HANDLE_INVALID;

    unsafe {
        let mut std_fd: i32 = -1;

        let mut status = fdio::fdio_sys::fdio_fd_create(log.into_raw(), &mut std_fd);
        if let Err(s) = zx::Status::ok(status) {
            return Err(Error::Fdio(FdioError::Create(s)));
        }

        status =
            fdio_sys::fdio_fd_clone(std_fd, &mut stderr_file_handle as *mut zx::sys::zx_handle_t);
        if let Err(s) = zx::Status::ok(status) {
            return Err(Error::Fdio(FdioError::Clone(s)));
        }

        status = fdio_sys::fdio_fd_transfer(
            std_fd,
            &mut stdout_file_handle as *mut zx::sys::zx_handle_t,
        );
        if let Err(s) = zx::Status::ok(status) {
            return Err(Error::Fdio(FdioError::Transfer(s)));
        }

        Ok((
            LoggerStream::new(client)?,
            zx::Handle::from_raw(stdout_file_handle),
            zx::Handle::from_raw(stderr_file_handle),
        ))
    }
}

/// Parses path in adapter's namespace and returns its file_name if path exists.
/// This code will not port to runners and we will use elf_runner's code.
pub fn extract_test_filename(test_path: &String) -> Result<String, Error> {
    let bin_path = Path::new(&test_path);
    if !bin_path.exists() {
        return Err(Error::PathDoesNotExist(test_path.clone()));
    }
    let file_name: &str;
    match bin_path.file_name() {
        Some(name) => file_name = name.to_str().ok_or(Error::NoFileName(test_path.clone()))?,
        None => return Err(Error::NoFileName(test_path.clone())),
    }
    Ok(file_name.to_owned())
}

/// Launches a process and return process and logger stream(which is hooked to
/// stdout and stderr of the process).
pub fn launch_process(
    test_path: &CStr,
    process_name: &CString,
    arguments: &[&CStr],
) -> Result<(Process, LoggerStream), Error> {
    const STDOUT: u16 = 1;
    const STDERR: u16 = 2;

    let mut actions = vec![];

    //TODO(anmittal): don't use fdio when porting to runners, refactor and use elf runner's code.
    actions.push(fdio::SpawnAction::set_name(process_name));

    let (logger, stdout_handle, stderr_handle) = create_log_stream()?;
    actions.push(fdio::SpawnAction::add_handle(
        HandleInfo::new(HandleType::FileDescriptor, STDOUT),
        stdout_handle,
    ));
    actions.push(fdio::SpawnAction::add_handle(
        HandleInfo::new(HandleType::FileDescriptor, STDERR),
        stderr_handle,
    ));

    let test_job = runtime::job_default().create_child_job().map_err(Error::JobCreation)?;

    let process = fdio::spawn_etc(
        &test_job,
        fdio::SpawnOptions::CLONE_ALL,
        &test_path,
        arguments,
        None,
        &mut actions[..],
    )
    .map_err(|(s, m)| FdioError::ProcessCreation(s, m))?;
    Ok((process, logger))
}

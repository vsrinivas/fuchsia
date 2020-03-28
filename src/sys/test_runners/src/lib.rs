// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod elf_component;

use {
    anyhow::Error,
    fdio::fdio_sys,
    fidl_fuchsia_process as fproc, fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service,
    fuchsia_runtime as runtime, fuchsia_zircon as zx,
    futures::{
        io::{self, AsyncRead},
        prelude::*,
        ready,
        task::{Context, Poll},
    },
    runner::component::ComponentNamespace,
    runtime::{HandleInfo, HandleType},
    std::{cell::RefCell, pin::Pin},
    thiserror::Error,
    zx::{AsHandleRef, HandleBased, Process, Task},
};

/// Error encountered while calling fdio operations.
#[derive(Debug, PartialEq, Eq, Error)]
pub enum FdioError {
    #[error("Cannot create file descriptor: {:?}", _0)]
    Create(zx::Status),

    #[error("Cannot clone file descriptor: {:?}", _0)]
    Clone(zx::Status),

    #[error("Cannot transfer file descriptor: {:?}", _0)]
    Transfer(zx::Status),
}

/// Error returned by this library.
#[derive(Debug, PartialEq, Eq, Error)]
pub enum LoggerError {
    #[error("fdio error: {:?}", _0)]
    Fdio(FdioError),

    #[error("cannot create socket: {:?}", _0)]
    CreateSocket(zx::Status),

    #[error("invalid socket: {:?}", _0)]
    InvalidSocket(zx::Status),
}

impl From<FdioError> for LoggerError {
    fn from(e: FdioError) -> Self {
        LoggerError::Fdio(e)
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
        zx::Socket::create(zx::SocketOpts::DATAGRAM).map_err(LoggerError::CreateSocket)?;
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

/// Error returned by this library.
#[derive(Debug, Error)]
pub enum LaunchError {
    #[error("{:?}", _0)]
    Logger(LoggerError),

    #[error("Error connecting to launcher: {:?}", _0)]
    Launcher(Error),

    #[error("{:?}", _0)]
    LoadInfo(runner::component::LaunchError),

    #[error("Error launching process: {:?}", _0)]
    LaunchCall(fidl::Error),

    #[error("Error launching process: {:?}", _0)]
    ProcessLaunch(zx::Status),

    #[error("unexpected error")]
    UnExpectedError,
}

/// Launches process, assigns a logger as stdout/stderr to launched process.
///
///  # Arguments
///
/// * `bin_path` - relative binary path to /pkg
/// * `process_name` - Name of the binary to add to process.
/// * `job` - Job used launch process, if None, a new child of default_job() is used.
/// * `ns` - Namespace for binary process to be launched.
/// * `args` - Arguments to binary. Binary name is automatically appended as first argument.
/// * `names_info` - Extra names to add to namespace. by default only names from `ns` are added.
/// * `environs` - Process environment variables.
pub async fn launch_process(
    bin_path: &str,
    process_name: &str,
    job: Option<zx::Job>,
    ns: ComponentNamespace,
    args: Option<Vec<String>>,
    name_infos: Option<Vec<fproc::NameInfo>>,
    environs: Option<Vec<String>>,
) -> Result<(Process, ScopedJob, LoggerStream), LaunchError> {
    let launcher = connect_to_service::<fproc::LauncherMarker>().map_err(LaunchError::Launcher)?;

    const STDOUT: u16 = 1;
    const STDERR: u16 = 2;

    let (logger, stdout_handle, stderr_handle) =
        create_log_stream().map_err(LaunchError::Logger)?;

    let mut handle_infos = vec![];

    handle_infos.push(fproc::HandleInfo {
        handle: stdout_handle,
        id: HandleInfo::new(HandleType::FileDescriptor, STDOUT).as_raw(),
    });

    handle_infos.push(fproc::HandleInfo {
        handle: stderr_handle,
        id: HandleInfo::new(HandleType::FileDescriptor, STDERR).as_raw(),
    });

    // Load the component
    let mut launch_info =
        runner::component::configure_launcher(runner::component::LauncherConfigArgs {
            bin_path,
            name: process_name,
            args,
            ns,
            job,
            handle_infos: Some(handle_infos),
            name_infos,
            environs,
            launcher: &launcher,
        })
        .await
        .map_err(LaunchError::LoadInfo)?;

    let component_job = launch_info
        .job
        .as_handle_ref()
        .duplicate(zx::Rights::SAME_RIGHTS)
        .expect("handle duplication failed!");

    let (status, process) =
        launcher.launch(&mut launch_info).await.map_err(LaunchError::LaunchCall)?;

    let status = zx::Status::from_raw(status);
    if status != zx::Status::OK {
        return Err(LaunchError::ProcessLaunch(status));
    }

    let process = process.ok_or_else(|| LaunchError::UnExpectedError)?;

    Ok((process, ScopedJob::new(zx::Job::from_handle(component_job)), logger))
}

// Structure to guard job and kill it when going out of scope.
pub struct ScopedJob {
    pub object: Option<zx::Job>,
}

impl ScopedJob {
    pub fn new(job: zx::Job) -> Self {
        Self { object: Some(job) }
    }

    /// Return the job back from this scoped object
    pub fn take(mut self) -> zx::Job {
        self.object.take().unwrap()
    }
}

impl Drop for ScopedJob {
    fn drop(&mut self) {
        if let Some(job) = self.object.take() {
            job.kill().ok();
        }
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
        fasync::spawn_local(async move {
            logger_handle.await;
        });
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

    pub async fn write_str(&mut self, s: String) -> Result<usize, LogError> {
        self.logger.write(s.as_bytes()).await.map_err(LogError::Write)
    }
}

#[cfg(test)]
mod tests {
    use {super::*, fuchsia_runtime::job_default, fuchsia_zircon as zx, std::mem::drop};

    #[test]
    fn scoped_job_works() {
        let new_job = job_default().create_child_job().unwrap();
        let job_dup = new_job.duplicate_handle(zx::Rights::SAME_RIGHTS).unwrap();

        // create new child job, else killing a job has no effect.
        let _child_job = new_job.create_child_job().unwrap();

        // check that job is alive
        let info = job_dup.info().unwrap();
        assert!(!info.exited);
        {
            let _job_about_to_die = ScopedJob::new(new_job);
        }

        // check that job was killed
        let info = job_dup.info().unwrap();
        assert!(info.exited);
    }

    #[test]
    fn scoped_job_take_works() {
        let new_job = job_default().create_child_job().unwrap();
        let raw_handle = new_job.raw_handle();

        let scoped = ScopedJob::new(new_job);

        let ret_job = scoped.take();

        // make sure we got back same job handle.
        assert_eq!(ret_job.raw_handle(), raw_handle);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn log_writer_reader_work() {
        let (sock1, sock2) = zx::Socket::create(zx::SocketOpts::DATAGRAM).unwrap();
        let mut log_writer = LogWriter::new(fasync::Socket::from_socket(sock1).unwrap());

        let reader = LoggerStream::new(sock2).unwrap();
        let reader = LogStreamReader::new(reader);

        log_writer.write_str("this is string one.".to_owned()).await.unwrap();
        log_writer.write_str("this is string two.".to_owned()).await.unwrap();
        drop(log_writer);

        let actual = reader.get_logs().await.unwrap();
        let actual = std::str::from_utf8(&actual).unwrap();
        assert_eq!(actual, "this is string one.this is string two.".to_owned());
    }
}

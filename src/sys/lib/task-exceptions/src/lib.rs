// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fuchsia_async as fasync,
    fuchsia_zircon::{self as zx, HandleBased},
    fuchsia_zircon_sys as zx_sys, futures,
    std::{convert::TryFrom, mem, ptr, task::Poll},
};

#[derive(Debug, PartialEq, Clone)]
pub enum ExceptionType {
    General,
    FatalPageFault,
    UndefinedInstruction,
    SwBreakpoint,
    HwBreakpoint,
    UnalignedAccess,
    ThreadStarting,
    ThreadExiting,
    PolicyError,
    ProcessStarting,
}

impl TryFrom<u32> for ExceptionType {
    type Error = zx::Status;

    /// Maps to the types defined in `zx_excp_type_t`.
    /// If zircon/syscalls/exception.h changes, this needs to be updates as well to
    /// reflect that.
    fn try_from(value: u32) -> Result<Self, Self::Error> {
        match value {
            0x8 => Ok(ExceptionType::General),
            0x108 => Ok(ExceptionType::FatalPageFault),
            0x208 => Ok(ExceptionType::UndefinedInstruction),
            0x308 => Ok(ExceptionType::SwBreakpoint),
            0x408 => Ok(ExceptionType::HwBreakpoint),
            0x508 => Ok(ExceptionType::UnalignedAccess),
            0x8008 => Ok(ExceptionType::ThreadStarting),
            0x8108 => Ok(ExceptionType::ThreadExiting),
            0x8208 => Ok(ExceptionType::PolicyError),
            0x8308 => Ok(ExceptionType::ProcessStarting),
            _ => Err(zx::Status::INVALID_ARGS),
        }
    }
}

pub struct ExceptionInfo {
    pub process: zx::Process,
    pub thread: zx::Thread,
    pub type_: ExceptionType,

    pub exception_handle: zx::Exception,
}

#[repr(C)]
struct ZxExceptionInfo {
    pid: zx_sys::zx_koid_t,
    tid: zx_sys::zx_koid_t,
    type_: u32,
    padding1: [u8; 4],
}

pub struct ExceptionsStream {
    inner: fasync::Channel,
    is_terminated: bool,
}

impl ExceptionsStream {
    pub fn register_with_task<T>(task: &T) -> Result<Self, Error>
    where
        T: zx::Task,
    {
        Self::from_channel(task.create_exception_channel()?)
    }

    pub fn from_channel(chan: zx::Channel) -> Result<Self, Error> {
        Ok(Self { inner: fasync::Channel::from_channel(chan)?, is_terminated: false })
    }
}

impl futures::Stream for ExceptionsStream {
    type Item = Result<ExceptionInfo, zx::Status>;

    fn poll_next(
        mut self: ::std::pin::Pin<&mut Self>,
        cx: &mut core::task::Context<'_>,
    ) -> Poll<Option<Self::Item>> {
        let this = &mut *self;

        if this.is_terminated {
            return Poll::Ready(None);
        }

        let mut msg_buf = zx::MessageBuf::new();
        msg_buf.ensure_capacity_bytes(mem::size_of::<ZxExceptionInfo>());
        msg_buf.ensure_capacity_handles(1);

        match this.inner.recv_from(cx, &mut msg_buf) {
            Poll::Pending => {
                return Poll::Pending;
            }
            Poll::Ready(Err(zx::Status::PEER_CLOSED)) => {
                this.is_terminated = true;
                return Poll::Ready(None);
            }
            Poll::Ready(Err(status)) => {
                this.is_terminated = true;
                return Poll::Ready(Some(Err(status)));
            }
            Poll::Ready(Ok(())) => {
                if msg_buf.n_handles() != 1 {
                    return Poll::Ready(Some(Err(zx::Status::BAD_HANDLE)));
                }
                let exception_handle = zx::Exception::from_handle(msg_buf.take_handle(0).unwrap());
                let zx_exception_info: ZxExceptionInfo =
                    unsafe { ptr::read(msg_buf.bytes().as_ptr() as *const _) };
                return Poll::Ready(Some(Ok(ExceptionInfo {
                    process: exception_handle.get_process()?,
                    thread: exception_handle.get_thread()?,
                    type_: ExceptionType::try_from(zx_exception_info.type_)?,
                    exception_handle,
                })));
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        anyhow::{format_err, Context},
        fidl_fuchsia_io as fio, fidl_fuchsia_process as fprocess,
        fuchsia_component::client as fclient,
        fuchsia_runtime as fruntime,
        fuchsia_zircon::HandleBased,
        futures::TryStreamExt,
        io_util,
        std::sync::Arc,
    };

    #[fasync::run_singlethreaded(test)]
    async fn catch_exception() -> Result<(), Error> {
        // Create a new job
        let child_job =
            fruntime::job_default().create_child_job().context("failed to create child job")?;

        // Register ourselves as its exception handler
        let mut exceptions_stream = ExceptionsStream::register_with_task(&child_job)
            .context("failed to register with task ")?;

        // Connect to the process launcher
        let launcher_proxy = fclient::connect_to_protocol::<fprocess::LauncherMarker>()?;

        // Set up a new library loader and provide it to the loader service
        let (ll_client_chan, ll_service_chan) = zx::Channel::create()?;
        library_loader::start(
            Arc::new(io_util::open_directory_in_namespace(
                "/pkg/lib",
                fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_EXECUTABLE,
            )?),
            ll_service_chan,
        );
        let mut handle_infos = vec![fprocess::HandleInfo {
            handle: ll_client_chan.into_handle(),
            id: fruntime::HandleInfo::new(fruntime::HandleType::LdsvcLoader, 0).as_raw(),
        }];
        launcher_proxy
            .add_handles(&mut handle_infos.iter_mut())
            .context("failed to add loader service handle")?;

        // Load the executable into a vmo
        let executable_file_proxy = io_util::open_file_in_namespace(
            "/pkg/bin/panic_on_start",
            fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_EXECUTABLE,
        )?;
        let (status, fidlbuf) =
            executable_file_proxy.get_buffer(fio::VMO_FLAG_READ | fio::VMO_FLAG_EXEC).await?;
        zx::Status::ok(status).context("failed to get VMO of executable")?;

        // Launch the process
        let child_job_dup = child_job.duplicate_handle(zx::Rights::SAME_RIGHTS)?;
        let mut launch_info = fprocess::LaunchInfo {
            executable: fidlbuf.ok_or(format_err!("no buffer returned from GetBuffer"))?.vmo,
            job: child_job_dup,
            name: "panic_on_start".to_string(),
        };
        let (status, _process) =
            launcher_proxy.launch(&mut launch_info).await.context("failed to launch process")?;
        zx::Status::ok(status).context("error returned by process launcher")?;

        // The process panics when it starts, so wait for a message on the exceptions stream
        match exceptions_stream.try_next().await {
            Ok(Some(_)) => (),
            Ok(None) => return Err(format_err!("the exceptions stream ended unexpectedly")),
            Err(e) => return Err(format_err!("exceptions stream returned an error: {:?}", e)),
        }
        Ok(())
    }
}

// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Helpers for launching components.

use {
    crate::errors::FdioError,
    crate::logs::{create_log_stream, LoggerError, LoggerStream},
    anyhow::Error,
    fidl_fuchsia_process as fproc,
    fuchsia_component::client::connect_to_protocol,
    fuchsia_runtime as runtime, fuchsia_zircon as zx,
    runner::component::ComponentNamespace,
    runtime::{HandleInfo, HandleType},
    thiserror::Error,
    zx::{AsHandleRef, HandleBased, Process, Rights, Task},
};

/// Error encountered while launching a component.
#[derive(Debug, Error)]
pub enum LaunchError {
    #[error("{:?}", _0)]
    Logger(#[from] LoggerError),

    #[error("{:?}", _0)]
    Fdio(#[from] FdioError),

    #[error("Error connecting to launcher: {:?}", _0)]
    Launcher(Error),

    #[error("{:?}", _0)]
    LoadInfo(runner::component::LaunchError),

    #[error("Error launching process: {:?}", _0)]
    LaunchCall(fidl::Error),

    #[error("Error launching process: {:?}", _0)]
    ProcessLaunch(zx::Status),

    #[error("Error launching process, cannot create socket {:?}", _0)]
    CreateSocket(zx::Status),

    #[error("Error cloning UTC clock: {:?}", _0)]
    UtcClock(zx::Status),

    #[error("unexpected error")]
    UnExpectedError,
}

/// Arguments to launch_process.
pub struct LaunchProcessArgs<'a> {
    /// Relative binary path to /pkg.
    pub bin_path: &'a str,
    /// Name of the binary to add to process.
    pub process_name: &'a str,
    ///Job used launch process, if None, a new child of default_job() is used.
    pub job: Option<zx::Job>,
    /// Namespace for binary process to be launched.
    pub ns: ComponentNamespace,
    /// Arguments to binary. Binary name is automatically appended as first argument.
    pub args: Option<Vec<String>>,
    /// Extra names to add to namespace. by default only names from `ns` are added.
    pub name_infos: Option<Vec<fproc::NameInfo>>,
    /// Process environment variables.
    pub environs: Option<Vec<String>>,
    /// Extra handle infos to add. Handles for stdout, stderr, and utc_clock are added.
    /// The UTC clock handle is cloned from the current process.
    pub handle_infos: Option<Vec<fproc::HandleInfo>>,
}

/// Launches process, assigns a logger as stdout/stderr to launched process.
pub async fn launch_process(
    args: LaunchProcessArgs<'_>,
) -> Result<(Process, ScopedJob, LoggerStream), LaunchError> {
    let launcher = connect_to_protocol::<fproc::LauncherMarker>().map_err(LaunchError::Launcher)?;
    launch_process_impl(args, launcher).await
}

async fn launch_process_impl(
    args: LaunchProcessArgs<'_>,
    launcher: fproc::LauncherProxy,
) -> Result<(Process, ScopedJob, LoggerStream), LaunchError> {
    const STDOUT: u16 = 1;
    const STDERR: u16 = 2;

    let (logger, stdout_handle, stderr_handle) =
        create_log_stream().map_err(LaunchError::Logger)?;

    let mut handle_infos = args.handle_infos.unwrap_or(vec![]);

    handle_infos.push(fproc::HandleInfo {
        handle: stdout_handle,
        id: HandleInfo::new(HandleType::FileDescriptor, STDOUT).as_raw(),
    });

    handle_infos.push(fproc::HandleInfo {
        handle: stderr_handle,
        id: HandleInfo::new(HandleType::FileDescriptor, STDERR).as_raw(),
    });

    handle_infos.push(fproc::HandleInfo {
        handle: runtime::duplicate_utc_clock_handle(
            Rights::DUPLICATE | Rights::READ | Rights::WAIT | Rights::TRANSFER,
        )
        .map_err(LaunchError::UtcClock)?
        .into_handle(),
        id: HandleInfo::new(HandleType::ClockUtc, 0).as_raw(),
    });

    // Load the component
    let mut launch_info =
        runner::component::configure_launcher(runner::component::LauncherConfigArgs {
            bin_path: args.bin_path,
            name: args.process_name,
            args: args.args,
            ns: args.ns,
            job: args.job,
            handle_infos: Some(handle_infos),
            name_infos: args.name_infos,
            environs: args.environs,
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

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl::endpoints::{create_proxy_and_stream, ClientEnd, Proxy},
        fidl_fuchsia_component_runner as fcrunner, fidl_fuchsia_io as fio, fuchsia_async as fasync,
        fuchsia_runtime::{job_default, process_self, swap_utc_clock_handle},
        fuchsia_zircon as zx,
        futures::prelude::*,
        std::convert::TryInto,
    };

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

    #[fasync::run_singlethreaded(test)]
    async fn utc_clock_is_cloned() {
        let clock =
            zx::Clock::create(zx::ClockOpts::MONOTONIC, None).expect("failed to create clock");
        let expected_clock_koid =
            clock.as_handle_ref().get_koid().expect("failed to get clock koid");

        // We are affecting the process-wide clock here, but since Rust test cases are run in their
        // own process, this won't interact with other running tests.
        let _ = swap_utc_clock_handle(clock).expect("failed to swap clocks");

        // We can't fake all the arguments, as there is actual IO happening. Pass in the bare
        // minimum that a process needs, and use this test's process handle for real values.
        let pkg = io_util::open_directory_in_namespace(
            "/pkg",
            fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_EXECUTABLE,
        )
        .expect("failed to open pkg");
        let args = LaunchProcessArgs {
            bin_path: "bin/test_runners_lib_lib_test", // path to this binary
            environs: None,
            args: None,
            job: None,
            process_name: "foo",
            name_infos: None,
            handle_infos: None,
            ns: vec![fcrunner::ComponentNamespaceEntry {
                path: Some("/pkg".into()),
                directory: Some(ClientEnd::new(pkg.into_channel().unwrap().into_zx_channel())),
                ..fcrunner::ComponentNamespaceEntry::EMPTY
            }]
            .try_into()
            .unwrap(),
        };
        let (mock_proxy, mut mock_stream) = create_proxy_and_stream::<fproc::LauncherMarker>()
            .expect("failed to create mock handles");
        let mock_fut = async move {
            let mut all_handles = vec![];
            while let Some(request) =
                mock_stream.try_next().await.expect("failed to get next message")
            {
                match request {
                    fproc::LauncherRequest::AddHandles { handles, .. } => {
                        all_handles.extend(handles);
                    }
                    fproc::LauncherRequest::Launch { responder, .. } => {
                        responder
                            .send(
                                zx::Status::OK.into_raw(),
                                Some(
                                    process_self()
                                        .duplicate(Rights::SAME_RIGHTS)
                                        .expect("failed to duplicate process handle"),
                                ),
                            )
                            .expect("failed to send reply");
                    }
                    _ => {}
                }
            }
            return all_handles;
        };
        let client_fut = async move {
            let _ = launch_process_impl(args, mock_proxy).await.expect("failed to launch process");
        };

        let (all_handles, ()) = futures::future::join(mock_fut, client_fut).await;
        let clock_id = HandleInfo::new(HandleType::ClockUtc, 0).as_raw();

        let utc_clock_handle = all_handles
            .into_iter()
            .find_map(
                |hi: fproc::HandleInfo| if hi.id == clock_id { Some(hi.handle) } else { None },
            )
            .expect("UTC clock handle");
        let clock_koid = utc_clock_handle.get_koid().expect("failed to get koid");
        assert_eq!(expected_clock_koid, clock_koid);
    }
}

// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::builtin::crash_introspect::{ComponentCrashInfo, CrashRecords},
    anyhow::Error,
    fuchsia_async as fasync,
    fuchsia_zircon::{self as zx, AsHandleRef},
    futures::TryStreamExt,
    moniker::AbsoluteMoniker,
    task_exceptions,
    tracing::error,
};

// Registers with the job to catch exceptions raised by it. Whenever we see an exception from this
// job, record that the crash happened, and inform zircon that it should proceed to the next crash
// handler. No actual handling of the crash occurs here, we merely save some data on it.
pub fn run_exceptions_server(
    component_job: &zx::Job,
    moniker: AbsoluteMoniker,
    resolved_url: String,
    crash_records: CrashRecords,
) -> Result<(), Error> {
    let mut task_exceptions_stream =
        task_exceptions::ExceptionsStream::register_with_task(component_job)?;
    fasync::Task::spawn(async move {
        loop {
            match task_exceptions_stream.try_next().await {
                Ok(Some(exception_info)) => {
                    if let Err(error) = record_exception(
                        resolved_url.clone(),
                        moniker.clone(),
                        exception_info,
                        &crash_records,
                    )
                    .await
                    {
                        error!(url=%resolved_url, ?error, "failed to handle exception");
                    }
                }
                Ok(None) => break,
                Err(error) => {
                    error!(
                        url=%resolved_url, ?error,
                        "failed to read message stream for fuchsia.sys2.CrashIntrospect",
                    );
                    break;
                }
            }
        }
    })
    .detach();
    Ok(())
}

async fn record_exception(
    resolved_url: String,
    moniker: AbsoluteMoniker,
    exception_info: task_exceptions::ExceptionInfo,
    crash_records: &CrashRecords,
) -> Result<(), Error> {
    // An exception has occurred, record information about the crash so that it may be retrieved
    // later.
    let thread_koid = exception_info.thread.get_koid()?;
    crash_records.add_report(thread_koid, ComponentCrashInfo { url: resolved_url, moniker }).await;

    // We've stored all the information we need, so mark the exception handle such that the next
    // exception handler should be attempted.
    exception_info
        .exception_handle
        .set_exception_state(&zx::sys::ZX_EXCEPTION_STATE_TRY_NEXT)
        .expect("failed to set exception state");

    // Returning drops exception_info.exception_handle, which allows zircon to proceed with
    // exception handling.
    Ok(())
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        anyhow::Context as _,
        fidl_fuchsia_io as fio, fidl_fuchsia_process as fprocess,
        fuchsia_component::client as fclient,
        fuchsia_fs, fuchsia_runtime as fruntime,
        fuchsia_zircon::HandleBased,
        std::{convert::TryInto, sync::Arc},
    };

    #[fuchsia::test]
    async fn crash_test() -> Result<(), Error> {
        let crash_records = CrashRecords::new();
        let url = "example://component#url".to_string();
        let moniker = AbsoluteMoniker::from(vec!["a"]);

        let child_job =
            fruntime::job_default().create_child_job().context("failed to create child job")?;

        run_exceptions_server(&child_job, moniker.clone(), url.clone(), crash_records.clone())?;

        // Connect to the process launcher
        let launcher_proxy = fclient::connect_to_protocol::<fprocess::LauncherMarker>()?;

        // Set up a new library loader and provide it to the loader service
        let (ll_client_chan, ll_service_chan) = zx::Channel::create()?;
        library_loader::start(
            Arc::new(fuchsia_fs::open_directory_in_namespace(
                "/pkg/lib",
                fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_EXECUTABLE,
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
        let executable_file_proxy = fuchsia_fs::file::open_in_namespace(
            "/pkg/bin/panic_on_start",
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_EXECUTABLE,
        )?;
        let vmo = executable_file_proxy
            .get_backing_memory(fio::VmoFlags::READ | fio::VmoFlags::EXECUTE)
            .await?
            .map_err(zx::Status::from_raw)
            .context("failed to get VMO of executable")?;

        // Create the process, but don't start it yet
        let child_job_dup = child_job.duplicate_handle(zx::Rights::SAME_RIGHTS)?;
        let mut launch_info = fprocess::LaunchInfo {
            executable: vmo,
            job: child_job_dup,
            name: "panic_on_start".to_string(),
        };
        let (status, process_start_data) = launcher_proxy
            .create_without_starting(&mut launch_info)
            .await
            .context("failed to launch process")?;
        zx::Status::ok(status).context("error returned by process launcher")?;
        let process_start_data = process_start_data.unwrap();

        // Get the thread's koid, so that we know which thread to look for in the records once it
        // crashes
        let thread_koid = process_start_data.thread.get_koid()?;

        // We've got the thread koid, so now we can actually start the process
        process_start_data.process.start(
            &process_start_data.thread,
            // Some of these values are u64, but this function is expecting usize
            process_start_data.entry.try_into().unwrap(),
            process_start_data.stack.try_into().unwrap(),
            process_start_data.bootstrap.into_handle(),
            process_start_data.vdso_base.try_into().unwrap(),
        )?;

        // The process panics when it starts, so wait for the job to be killed
        fasync::OnSignals::new(&process_start_data.process, zx::Signals::PROCESS_TERMINATED)
            .await?;
        let crash_info = crash_records
            .take_report(&thread_koid)
            .await
            .expect("crash_records is missing crash information");
        assert_eq!(ComponentCrashInfo { url, moniker }, crash_info);
        Ok(())
    }
}

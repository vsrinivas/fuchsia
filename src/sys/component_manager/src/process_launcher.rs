// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{Error, Fail},
    fidl::encoding::OutOfLine,
    fidl_fuchsia_process as fproc,
    fuchsia_runtime::{HandleInfo, HandleInfoError},
    fuchsia_zircon::{self as zx, AsHandleRef},
    futures::prelude::*,
    log::warn,
    process_builder::{
        BuiltProcess, NamespaceEntry, ProcessBuilder, ProcessBuilderError, StartupHandle,
    },
    std::convert::TryFrom,
    std::ffi::CString,
};

/// Internal error type for ProcessLauncherService which conveniently wraps errors that might
/// result during process launching and allows for mapping them to an equivalent zx::Status, which
/// is what actually gets returned through the protocol.
#[derive(Fail, Debug)]
enum LauncherError {
    #[fail(display = "{}", _0)]
    InvalidArg(&'static str),
    #[fail(display = "{}", _0)]
    BuilderError(#[cause] ProcessBuilderError),
    #[fail(display = "{}", _0)]
    HandleInfoError(#[cause] HandleInfoError),
}

impl LauncherError {
    pub fn as_zx_status(&self) -> zx::Status {
        match self {
            LauncherError::InvalidArg(_) => zx::Status::INVALID_ARGS,
            LauncherError::BuilderError(e) => e.as_zx_status(),
            LauncherError::HandleInfoError(_) => zx::Status::INVALID_ARGS,
        }
    }
}

impl From<ProcessBuilderError> for LauncherError {
    fn from(err: ProcessBuilderError) -> Self {
        LauncherError::BuilderError(err)
    }
}

impl From<HandleInfoError> for LauncherError {
    fn from(err: HandleInfoError) -> Self {
        LauncherError::HandleInfoError(err)
    }
}

/// An implementation of the `fuchsia.process.Launcher protocol using the process_builder crate.
pub struct ProcessLauncherService;

#[derive(Default, Debug)]
struct ProcessLauncherState {
    args: Vec<Vec<u8>>,
    environ: Vec<Vec<u8>>,
    name_info: Vec<fproc::NameInfo>,
    handles: Vec<fproc::HandleInfo>,
}

impl ProcessLauncherService {
    /// Serves an instance of the `fuchsia.process.Launcher` protocol given an appropriate
    /// RequestStream. Returns when the channel backing the RequestStream is closed or an
    /// unrecoverable error, like a failure to read from the stream, occurs.
    pub async fn serve(mut stream: fproc::LauncherRequestStream) -> Result<(), Error> {
        // `fuchsia.process.Launcher is stateful. The Add methods accumulate state that is
        // consumed/reset by either Launch or CreateWithoutStarting.
        let mut state = ProcessLauncherState::default();

        while let Some(req) = await!(stream.try_next())? {
            match req {
                fproc::LauncherRequest::Launch { info, responder } => {
                    let job_koid = info.job.get_koid();
                    let name = info.name.clone();

                    match await!(Self::launch_process(info, state)) {
                        Ok(process) => {
                            responder.send(zx::Status::OK.into_raw(), Some(process))?;
                        }
                        Err(err) => {
                            warn!(
                                "Failed to launch process '{}' in job {}: {}",
                                name,
                                koid_to_string(job_koid),
                                err
                            );
                            responder.send(err.as_zx_status().into_raw(), None)?;
                        }
                    }

                    // Reset state to defaults.
                    state = ProcessLauncherState::default();
                }
                fproc::LauncherRequest::CreateWithoutStarting { info, responder } => {
                    let job_koid = info.job.get_koid();
                    let name = info.name.clone();

                    match await!(Self::create_process(info, state)) {
                        Ok(built) => {
                            let mut process_data = fproc::ProcessStartData {
                                process: built.process,
                                root_vmar: built.root_vmar,
                                thread: built.thread,
                                entry: built.entry as u64,
                                stack: built.stack as u64,
                                bootstrap: built.bootstrap,
                                vdso_base: built.vdso_base as u64,
                                base: built.elf_base as u64,
                            };
                            responder.send(
                                zx::Status::OK.into_raw(),
                                Some(OutOfLine(&mut process_data)),
                            )?;
                        }
                        Err(err) => {
                            warn!(
                                "Failed to create process '{}' in job {}: {}",
                                name,
                                koid_to_string(job_koid),
                                err
                            );
                            responder.send(err.as_zx_status().into_raw(), None)?;
                        }
                    }

                    // Reset state to defaults.
                    state = ProcessLauncherState::default();
                }
                fproc::LauncherRequest::AddArgs { mut args, control_handle: _ } => {
                    state.args.append(&mut args);
                }
                fproc::LauncherRequest::AddEnvirons { mut environ, control_handle: _ } => {
                    state.environ.append(&mut environ);
                }
                fproc::LauncherRequest::AddNames { mut names, control_handle: _ } => {
                    state.name_info.append(&mut names);
                }
                fproc::LauncherRequest::AddHandles { mut handles, control_handle: _ } => {
                    state.handles.append(&mut handles);
                }
            }
        }
        Ok(())
    }

    async fn launch_process(
        info: fproc::LaunchInfo,
        state: ProcessLauncherState,
    ) -> Result<zx::Process, LauncherError> {
        Ok(await!(Self::create_process(info, state))?.start()?)
    }

    async fn create_process(
        info: fproc::LaunchInfo,
        state: ProcessLauncherState,
    ) -> Result<BuiltProcess, LauncherError> {
        Ok(await!(Self::create_process_builder(info, state)?.build())?)
    }

    fn create_process_builder(
        info: fproc::LaunchInfo,
        state: ProcessLauncherState,
    ) -> Result<ProcessBuilder, LauncherError> {
        let proc_name = CString::new(info.name)
            .map_err(|_| LauncherError::InvalidArg("Process name contained null byte"))?;
        let mut b = ProcessBuilder::new(&proc_name, &info.job, info.executable)?;

        let arg_cstr = state
            .args
            .into_iter()
            .map(|a| CString::new(a))
            .collect::<Result<_, _>>()
            .map_err(|_| LauncherError::InvalidArg("Argument contained null byte"))?;
        b.add_arguments(arg_cstr);

        let env_cstr = state
            .environ
            .into_iter()
            .map(|e| CString::new(e))
            .collect::<Result<_, _>>()
            .map_err(|_| LauncherError::InvalidArg("Environment string contained null byte"))?;
        b.add_environment_variables(env_cstr);

        let entries = state
            .name_info
            .into_iter()
            .map(|n| Self::new_namespace_entry(n))
            .collect::<Result<_, _>>()?;
        b.add_namespace_entries(entries)?;

        // Note that clients of ``fuchsia.process.Launcher` provide the `fuchsia.ldsvc.Loader`
        // through AddHandles, with a handle type of [HandleType::LdsvcLoader].
        // [ProcessBuilder::add_handles] automatically handles that for convenience.
        let handles = state
            .handles
            .into_iter()
            .map(|h| Self::new_startup_handle(h))
            .collect::<Result<_, _>>()?;
        b.add_handles(handles)?;

        Ok(b)
    }

    // Can't impl TryFrom for these because both types are from external crates. :(
    // Could wrap in a newtype, but then have to unwrap, so this is simplest.
    fn new_namespace_entry(info: fproc::NameInfo) -> Result<NamespaceEntry, LauncherError> {
        let cstr = CString::new(info.path)
            .map_err(|_| LauncherError::InvalidArg("Namespace path contained null byte"))?;
        Ok(NamespaceEntry { path: cstr, directory: info.directory })
    }

    fn new_startup_handle(info: fproc::HandleInfo) -> Result<StartupHandle, LauncherError> {
        Ok(StartupHandle { handle: info.handle, info: HandleInfo::try_from(info.id)? })
    }
}

fn koid_to_string(koid: Result<zx::Koid, zx::Status>) -> String {
    koid.map(|j| j.raw_koid().to_string()).unwrap_or("<unknown>".to_string())
}

// These tests are very similar to the tests in process_builder itself, and even reuse the test
// util from that, since the process_builder API is close to 1:1 with the process launcher service.
#[cfg(test)]
mod tests {
    use {
        super::*,
        failure::ResultExt,
        fidl::endpoints::{ClientEnd, Proxy, ServerEnd, ServiceMarker},
        fidl_fuchsia_io as fio,
        fidl_test_processbuilder::{UtilMarker, UtilProxy},
        fuchsia_async as fasync,
        fuchsia_runtime::{job_default, HandleType},
        fuchsia_vfs_pseudo_fs::{
            directory::entry::DirectoryEntry, file::simple::read_only, pseudo_directory,
        },
        fuchsia_zircon::HandleBased,
        std::{fs::File, iter, mem, path::Path},
    };

    extern "C" {
        fn dl_clone_loader_service(handle: *mut zx::sys::zx_handle_t) -> zx::sys::zx_status_t;
    }

    // Clone the current loader service to provide to the new test processes.
    fn clone_loader_service() -> Result<zx::Handle, zx::Status> {
        let mut raw = 0;
        let status = unsafe { dl_clone_loader_service(&mut raw) };
        zx::Status::ok(status)?;

        let handle = unsafe { zx::Handle::from_raw(raw) };
        Ok(handle)
    }

    // It is not possible to test this process launcher service, which uses the process_builder
    // crate, except in an environment where zx_process_start is allowed (generally, when the test
    // process runs in the root job). We return early from the tests otherwise.
    fn expect_access_denied() -> bool {
        // This is somewhat fragile but intentionally so, so that this will fail if the binary
        // names change and get updated properly.
        let bin = std::env::args().next();
        match bin.as_ref().map(String::as_ref) {
            Some("/pkg/test/component_manager_tests") => true,
            Some("/pkg/test/component_manager_boot_env_tests") => false,
            _ => panic!("Unexpected test binary name {:?}", bin),
        }
    }

    fn serve_launcher() -> Result<fproc::LauncherProxy, Error> {
        let (proxy, stream) = fidl::endpoints::create_proxy_and_stream::<fproc::LauncherMarker>()?;
        fasync::spawn_local(
            ProcessLauncherService::serve(stream)
                .unwrap_or_else(|e| panic!("Error while serving process launcher: {}", e)),
        );
        Ok(proxy)
    }

    fn connect_util(client: &zx::Channel) -> Result<UtilProxy, Error> {
        let (proxy, server) = zx::Channel::create()?;
        fdio::service_connect_at(&client, UtilMarker::NAME, server)
            .context("failed to connect to util service")?;
        Ok(UtilProxy::from_channel(fasync::Channel::from_channel(proxy)?))
    }

    fn check_process_running(process: &zx::Process) -> Result<(), Error> {
        let info = process.info()?;
        assert_eq!(
            info,
            zx::ProcessInfo {
                return_code: 0,
                started: true,
                exited: false,
                debugger_attached: false
            }
        );
        Ok(())
    }

    async fn check_process_exited_ok(process: &zx::Process) -> Result<(), Error> {
        await!(fasync::OnSignals::new(process, zx::Signals::PROCESS_TERMINATED))?;

        let info = process.info()?;
        assert_eq!(
            info,
            zx::ProcessInfo {
                return_code: 0,
                started: true,
                exited: true,
                debugger_attached: false
            }
        );
        Ok(())
    }

    // Common setup for all tests that start a test util process through the launcher service.
    fn setup_test_util(
        launcher: &fproc::LauncherProxy,
    ) -> Result<(fproc::LaunchInfo, UtilProxy), Error> {
        const TEST_UTIL_BIN: &'static str = "/pkg/bin/process_builder_test_util";
        let binpath = Path::new(TEST_UTIL_BIN);
        let vmo = fdio::get_vmo_copy_from_file(&File::open(binpath)?)?.replace_as_executable()?;
        let job = job_default();

        let (dir_client, dir_server) = zx::Channel::create()?;
        let mut handles = vec![
            fproc::HandleInfo {
                handle: dir_server.into_handle(),
                id: HandleInfo::new(HandleType::DirectoryRequest, 0).as_raw(),
            },
            fproc::HandleInfo {
                handle: clone_loader_service()?,
                id: HandleInfo::new(HandleType::LdsvcLoader, 0).as_raw(),
            },
        ];
        launcher.add_handles(&mut handles.iter_mut())?;

        let launch_info = fproc::LaunchInfo {
            name: TEST_UTIL_BIN.to_owned(),
            executable: vmo,
            job: job.duplicate(zx::Rights::SAME_RIGHTS)?,
        };
        let util_proxy = connect_util(&dir_client)?;
        Ok((launch_info, util_proxy))
    }

    #[fasync::run_singlethreaded(test)]
    async fn start_util_with_args() -> Result<(), Error> {
        let launcher = serve_launcher()?;
        let (mut launch_info, proxy) = setup_test_util(&launcher)?;

        let test_args = vec!["arg0", "arg1", "arg2"];
        let mut test_args_iters: Vec<_> = test_args.iter().map(|s| s.bytes()).collect();
        let mut test_args_iters =
            test_args_iters.iter_mut().map(|iter| iter as &mut dyn ExactSizeIterator<Item = u8>);
        launcher.add_args(&mut test_args_iters)?;

        let (status, process) = await!(launcher.launch(&mut launch_info))?;
        if expect_access_denied() {
            assert_eq!(zx::Status::from_raw(status), zx::Status::ACCESS_DENIED);
            return Ok(());
        }
        zx::Status::ok(status).context("Failed to launch test util process")?;
        let process = process.expect("Status was OK but no process returned");
        check_process_running(&process)?;

        let proc_args = await!(proxy.get_arguments()).context("failed to get args from util")?;
        assert_eq!(proc_args, test_args);

        mem::drop(proxy);
        await!(check_process_exited_ok(&process))?;
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn start_util_with_env() -> Result<(), Error> {
        let launcher = serve_launcher()?;
        let (mut launch_info, proxy) = setup_test_util(&launcher)?;

        let test_env = vec![("VAR1", "value2"), ("VAR2", "value2")];
        let test_env_strs: Vec<_> = test_env.iter().map(|v| format!("{}={}", v.0, v.1)).collect();
        let mut test_env_iters: Vec<_> = test_env_strs.iter().map(|s| s.bytes()).collect();
        let mut test_env_iters =
            test_env_iters.iter_mut().map(|iter| iter as &mut dyn ExactSizeIterator<Item = u8>);
        launcher.add_environs(&mut test_env_iters)?;

        let (status, process) = await!(launcher.launch(&mut launch_info))?;
        if expect_access_denied() {
            assert_eq!(zx::Status::from_raw(status), zx::Status::ACCESS_DENIED);
            return Ok(());
        }
        zx::Status::ok(status).context("Failed to launch test util process")?;
        let process = process.expect("Status was OK but no process returned");
        check_process_running(&process)?;

        let proc_env = await!(proxy.get_environment()).context("failed to get env from util")?;
        let proc_env_tuple: Vec<(&str, &str)> =
            proc_env.iter().map(|v| (&*v.key, &*v.value)).collect();
        assert_eq!(proc_env_tuple, test_env);
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn start_util_with_namespace_entries() -> Result<(), Error> {
        let launcher = serve_launcher()?;
        let (mut launch_info, proxy) = setup_test_util(&launcher)?;

        let mut randbuf = [0; 8];
        zx::cprng_draw(&mut randbuf)?;
        let test_content = format!("test content {}", u64::from_le_bytes(randbuf));

        let test_content_bytes = test_content.clone().into_bytes();
        let (dir_server, dir_client) = zx::Channel::create()?;
        fasync::spawn(async move {
            let mut dir = pseudo_directory! {
                "test_file" => read_only(|| Ok(test_content_bytes.clone())),
            };
            dir.open(
                fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE,
                fio::MODE_TYPE_DIRECTORY,
                &mut iter::empty(),
                ServerEnd::new(dir_server),
            );
            await!(dir);
            panic!("Pseudo dir stopped serving!");
        });

        let mut name_infos = vec![fproc::NameInfo {
            path: "/dir".to_string(),
            directory: ClientEnd::new(dir_client),
        }];
        launcher.add_names(&mut name_infos.iter_mut())?;

        let (status, process) = await!(launcher.launch(&mut launch_info))?;
        if expect_access_denied() {
            assert_eq!(zx::Status::from_raw(status), zx::Status::ACCESS_DENIED);
            return Ok(());
        }
        zx::Status::ok(status).context("Failed to launch test util process")?;
        let process = process.expect("Status was OK but no process returned");
        check_process_running(&process)?;

        let namespace_dump = await!(proxy.dump_namespace()).context("failed to dump namespace")?;
        assert_eq!(namespace_dump, "/dir, /dir/test_file");
        let dir_contents =
            await!(proxy.read_file("/dir/test_file")).context("failed to read file via util")?;
        assert_eq!(dir_contents, test_content);
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn create_without_starting() -> Result<(), Error> {
        let launcher = serve_launcher()?;
        let (mut launch_info, proxy) = setup_test_util(&launcher)?;

        let test_args = vec!["arg0", "arg1", "arg2"];
        let mut test_args_iters: Vec<_> = test_args.iter().map(|s| s.bytes()).collect();
        let mut test_args_iters =
            test_args_iters.iter_mut().map(|iter| iter as &mut dyn ExactSizeIterator<Item = u8>);
        launcher.add_args(&mut test_args_iters)?;

        let (status, start_data) = await!(launcher.create_without_starting(&mut launch_info))?;
        if expect_access_denied() {
            assert_eq!(zx::Status::from_raw(status), zx::Status::ACCESS_DENIED);
            return Ok(());
        }
        zx::Status::ok(status).context("Failed to launch test util process")?;
        let start_data = start_data.expect("Status was OK but no ProcessStartData returned");

        // Process should exist & be valid but not yet be running.
        let info = start_data.process.info()?;
        assert_eq!(
            info,
            zx::ProcessInfo {
                return_code: 0,
                started: false,
                exited: false,
                debugger_attached: false
            }
        );

        // Start the process manually using the info from ProcessStartData.
        start_data
            .process
            .start(
                &start_data.thread,
                start_data.entry as usize,
                start_data.stack as usize,
                start_data.bootstrap.into_handle(),
                start_data.vdso_base as usize,
            )
            .context("Failed to start process from ProcessStartData")?;
        check_process_running(&start_data.process)?;

        let proc_args = await!(proxy.get_arguments()).context("failed to get args from util")?;
        assert_eq!(proc_args, test_args);

        mem::drop(proxy);
        await!(check_process_exited_ok(&start_data.process))?;
        Ok(())
    }
}

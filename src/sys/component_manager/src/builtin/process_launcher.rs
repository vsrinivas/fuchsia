// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        capability::{CapabilityProvider, CapabilitySource, InternalCapability},
        channel,
        model::{
            error::ModelError,
            hooks::{Event, EventPayload, EventType, Hook, HooksRegistration},
        },
    },
    anyhow::Error,
    async_trait::async_trait,
    cm_rust::{CapabilityNameOrPath, CapabilityPath},
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_process as fproc, fuchsia_async as fasync,
    fuchsia_runtime::{HandleInfo, HandleInfoError},
    fuchsia_zircon::{self as zx, AsHandleRef},
    futures::prelude::*,
    lazy_static::lazy_static,
    log::warn,
    process_builder::{
        BuiltProcess, NamespaceEntry, ProcessBuilder, ProcessBuilderError, StartupHandle,
    },
    std::{
        convert::{TryFrom, TryInto},
        ffi::CString,
        path::PathBuf,
        sync::{Arc, Weak},
    },
    thiserror::Error,
};

lazy_static! {
    pub static ref PROCESS_LAUNCHER_CAPABILITY_PATH: CapabilityPath =
        "/svc/fuchsia.process.Launcher".try_into().unwrap();
}

/// Internal error type for ProcessLauncher which conveniently wraps errors that might
/// result during process launching and allows for mapping them to an equivalent zx::Status, which
/// is what actually gets returned through the protocol.
#[derive(Error, Debug)]
enum LauncherError {
    #[error("Invalid arg: {}", _0)]
    InvalidArg(&'static str),
    #[error("Failed to build new process: {}", _0)]
    BuilderError(ProcessBuilderError),
    #[error("Invalid handle info: {}", _0)]
    HandleInfoError(HandleInfoError),
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

#[derive(Default, Debug)]
struct ProcessLauncherState {
    args: Vec<Vec<u8>>,
    environ: Vec<Vec<u8>>,
    name_info: Vec<fproc::NameInfo>,
    handles: Vec<fproc::HandleInfo>,
}

/// An implementation of the `fuchsia.process.Launcher` protocol using the `process_builder` crate.
pub struct ProcessLauncher;

impl ProcessLauncher {
    pub fn new() -> Self {
        Self
    }

    pub fn hooks(self: &Arc<Self>) -> Vec<HooksRegistration> {
        vec![HooksRegistration::new(
            "ProcessLauncher",
            vec![EventType::CapabilityRouted],
            Arc::downgrade(self) as Weak<dyn Hook>,
        )]
    }

    async fn on_framework_capability_routed_async<'a>(
        self: Arc<Self>,
        capability: &'a InternalCapability,
        capability_provider: Option<Box<dyn CapabilityProvider>>,
    ) -> Result<Option<Box<dyn CapabilityProvider>>, ModelError> {
        match capability {
            InternalCapability::Protocol(CapabilityNameOrPath::Path(capability_path))
                if *capability_path == *PROCESS_LAUNCHER_CAPABILITY_PATH =>
            {
                Ok(Some(Box::new(ProcessLauncherCapabilityProvider::new())
                    as Box<dyn CapabilityProvider>))
            }
            _ => Ok(capability_provider),
        }
    }

    /// Serves an instance of the `fuchsia.process.Launcher` protocol given an appropriate
    /// RequestStream. Returns when the channel backing the RequestStream is closed or an
    /// unrecoverable error, like a failure to read from the stream, occurs.
    pub async fn serve(mut stream: fproc::LauncherRequestStream) -> Result<(), Error> {
        // `fuchsia.process.Launcher is stateful. The Add methods accumulate state that is
        // consumed/reset by either Launch or CreateWithoutStarting.
        let mut state = ProcessLauncherState::default();

        while let Some(req) = stream.try_next().await? {
            match req {
                fproc::LauncherRequest::Launch { info, responder } => {
                    let job_koid = info.job.get_koid();
                    let name = info.name.clone();

                    match Self::launch_process(info, state).await {
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

                    match Self::create_process(info, state).await {
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
                            responder.send(zx::Status::OK.into_raw(), Some(&mut process_data))?;
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
        Ok(Self::create_process(info, state).await?.start()?)
    }

    async fn create_process(
        info: fproc::LaunchInfo,
        state: ProcessLauncherState,
    ) -> Result<BuiltProcess, LauncherError> {
        Ok(Self::create_process_builder(info, state)?.build().await?)
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

#[async_trait]
impl Hook for ProcessLauncher {
    async fn on(self: Arc<Self>, event: &Event) -> Result<(), ModelError> {
        if let Ok(EventPayload::CapabilityRouted {
            source: CapabilitySource::AboveRoot { capability },
            capability_provider,
        }) = &event.result
        {
            let mut capability_provider = capability_provider.lock().await;
            *capability_provider = self
                .on_framework_capability_routed_async(&capability, capability_provider.take())
                .await?;
        };
        Ok(())
    }
}

struct ProcessLauncherCapabilityProvider;

impl ProcessLauncherCapabilityProvider {
    pub fn new() -> Self {
        Self {}
    }
}

#[async_trait]
impl CapabilityProvider for ProcessLauncherCapabilityProvider {
    async fn open(
        self: Box<Self>,
        _flags: u32,
        _open_mode: u32,
        _relative_path: PathBuf,
        server_end: &mut zx::Channel,
    ) -> Result<(), ModelError> {
        let server_end = channel::take_channel(server_end);
        let server_end = ServerEnd::<fproc::LauncherMarker>::new(server_end);
        let stream: fproc::LauncherRequestStream = server_end.into_stream().unwrap();
        fasync::Task::spawn(async move {
            let result = ProcessLauncher::serve(stream).await;
            if let Err(e) = result {
                warn!("ProcessLauncher.serve failed: {}", e);
            }
        })
        .detach();
        Ok(())
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
        crate::model::{hooks::Hooks, moniker::AbsoluteMoniker},
        anyhow::{format_err, Context},
        fidl::endpoints::{ClientEnd, Proxy, ServerEnd, ServiceMarker},
        fidl_fuchsia_io as fio,
        fidl_test_processbuilder::{UtilMarker, UtilProxy},
        fuchsia_async::{self as fasync, EHandle},
        fuchsia_runtime::{job_default, HandleType},
        fuchsia_zircon::HandleBased,
        futures::lock::Mutex,
        std::mem,
        vfs::{
            directory::entry::DirectoryEntry, execution_scope::ExecutionScope,
            file::pcb::asynchronous::read_only_static, path, pseudo_directory,
        },
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
            Some("/pkg/bin/component_manager_test") => true,
            Some("/pkg/bin/component_manager_boot_env_test") => false,
            _ => panic!("Unexpected test binary name {:?}", bin),
        }
    }

    async fn serve_launcher() -> Result<(fproc::LauncherProxy, Arc<ProcessLauncher>), Error> {
        let process_launcher = Arc::new(ProcessLauncher::new());
        let hooks = Hooks::new(None);
        hooks.install(process_launcher.hooks()).await;

        let capability_provider = Arc::new(Mutex::new(None));
        let source = CapabilitySource::AboveRoot {
            capability: InternalCapability::Protocol(CapabilityNameOrPath::Path(
                PROCESS_LAUNCHER_CAPABILITY_PATH.clone(),
            )),
        };

        let (client, mut server) = zx::Channel::create()?;

        let event = Event::new_for_test(
            AbsoluteMoniker::root(),
            "fuchsia-pkg://root",
            Ok(EventPayload::CapabilityRouted {
                source,
                capability_provider: capability_provider.clone(),
            }),
        );
        hooks.dispatch(&event).await?;

        let capability_provider = capability_provider.lock().await.take();
        if let Some(capability_provider) = capability_provider {
            capability_provider.open(0, 0, PathBuf::new(), &mut server).await?;
        }

        let launcher_proxy = ClientEnd::<fproc::LauncherMarker>::new(client)
            .into_proxy()
            .expect("failed to create launcher proxy");
        Ok((launcher_proxy, process_launcher))
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
        fasync::OnSignals::new(process, zx::Signals::PROCESS_TERMINATED).await?;

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
    async fn setup_test_util(
        launcher: &fproc::LauncherProxy,
    ) -> Result<(fproc::LaunchInfo, UtilProxy), Error> {
        const TEST_UTIL_BIN: &'static str = "/pkg/bin/process_builder_test_util";
        let file_proxy = io_util::open_file_in_namespace(
            TEST_UTIL_BIN,
            fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_EXECUTABLE,
        )?;
        let (status, fidlbuf) = file_proxy
            .get_buffer(fio::VMO_FLAG_READ | fio::VMO_FLAG_EXEC)
            .await
            .map_err(|e| format_err!("getting test_util as exec failed: {}", e))?;
        zx::Status::ok(status)
            .map_err(|e| format_err!("getting test_util as exec failed: {}", e))?;
        let vmo = fidlbuf.ok_or(format_err!("no buffer returned from GetBuffer"))?.vmo;
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
        let (launcher, _process_launcher) = serve_launcher().await?;
        let (mut launch_info, proxy) = setup_test_util(&launcher).await?;

        let test_args = vec!["arg0", "arg1", "arg2"];
        let test_args_bytes: Vec<_> = test_args.iter().map(|s| s.as_bytes()).collect();
        launcher.add_args(&mut test_args_bytes.into_iter())?;

        let (status, process) = launcher.launch(&mut launch_info).await?;
        if expect_access_denied() {
            assert_eq!(zx::Status::from_raw(status), zx::Status::ACCESS_DENIED);
            return Ok(());
        }
        zx::Status::ok(status).context("Failed to launch test util process")?;
        let process = process.expect("Status was OK but no process returned");
        check_process_running(&process)?;

        let proc_args = proxy.get_arguments().await.context("failed to get args from util")?;
        assert_eq!(proc_args, test_args);

        mem::drop(proxy);
        check_process_exited_ok(&process).await?;
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn start_util_with_env() -> Result<(), Error> {
        let (launcher, _process_launcher) = serve_launcher().await?;
        let (mut launch_info, proxy) = setup_test_util(&launcher).await?;

        let test_env = vec![("VAR1", "value2"), ("VAR2", "value2")];
        let test_env_strs: Vec<_> = test_env.iter().map(|v| format!("{}={}", v.0, v.1)).collect();
        let test_env_bytes: Vec<_> = test_env_strs.iter().map(|s| s.as_bytes()).collect();
        launcher.add_environs(&mut test_env_bytes.into_iter())?;

        let (status, process) = launcher.launch(&mut launch_info).await?;
        if expect_access_denied() {
            assert_eq!(zx::Status::from_raw(status), zx::Status::ACCESS_DENIED);
            return Ok(());
        }
        zx::Status::ok(status).context("Failed to launch test util process")?;
        let process = process.expect("Status was OK but no process returned");
        check_process_running(&process)?;

        let proc_env = proxy.get_environment().await.context("failed to get env from util")?;
        let proc_env_tuple: Vec<(&str, &str)> =
            proc_env.iter().map(|v| (&*v.key, &*v.value)).collect();
        assert_eq!(proc_env_tuple, test_env);
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn start_util_with_namespace_entries() -> Result<(), Error> {
        let (launcher, _process_launcher) = serve_launcher().await?;
        let (mut launch_info, proxy) = setup_test_util(&launcher).await?;

        let mut randbuf = [0; 8];
        zx::cprng_draw(&mut randbuf)?;
        let test_content = format!("test content {}", u64::from_le_bytes(randbuf));

        let test_content_bytes = test_content.clone().into_bytes();
        let (dir_server, dir_client) = zx::Channel::create()?;
        let dir = pseudo_directory! {
            "test_file" => read_only_static(test_content_bytes),
        };
        dir.clone().open(
            ExecutionScope::from_executor(Box::new(EHandle::local())),
            fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE,
            fio::MODE_TYPE_DIRECTORY,
            path::Path::empty(),
            ServerEnd::new(dir_server),
        );

        let mut name_infos = vec![fproc::NameInfo {
            path: "/dir".to_string(),
            directory: ClientEnd::new(dir_client),
        }];
        launcher.add_names(&mut name_infos.iter_mut())?;

        let (status, process) = launcher.launch(&mut launch_info).await?;
        if expect_access_denied() {
            assert_eq!(zx::Status::from_raw(status), zx::Status::ACCESS_DENIED);
            return Ok(());
        }
        zx::Status::ok(status).context("Failed to launch test util process")?;
        let process = process.expect("Status was OK but no process returned");
        check_process_running(&process)?;

        let namespace_dump = proxy.dump_namespace().await.context("failed to dump namespace")?;
        assert_eq!(namespace_dump, "/dir, /dir/test_file");
        let dir_contents =
            proxy.read_file("/dir/test_file").await.context("failed to read file via util")?;
        assert_eq!(dir_contents, test_content);
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn create_without_starting() -> Result<(), Error> {
        let (launcher, _process_launcher) = serve_launcher().await?;
        let (mut launch_info, proxy) = setup_test_util(&launcher).await?;

        let test_args = vec!["arg0", "arg1", "arg2"];
        let test_args_bytes: Vec<_> = test_args.iter().map(|s| s.as_bytes()).collect();
        launcher.add_args(&mut test_args_bytes.into_iter())?;

        let (status, start_data) = launcher.create_without_starting(&mut launch_info).await?;
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

        let proc_args = proxy.get_arguments().await.context("failed to get args from util")?;
        assert_eq!(proc_args, test_args);

        mem::drop(proxy);
        check_process_exited_ok(&start_data.process).await?;
        Ok(())
    }
}

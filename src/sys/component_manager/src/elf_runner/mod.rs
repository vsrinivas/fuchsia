// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        builtin::process_launcher::ProcessLauncher,
        model::runner::{Runner, RunnerError},
        startup::Arguments,
    },
    anyhow::{format_err, Context as _, Error},
    async_trait::async_trait,
    clonable_error::ClonableError,
    fdio::fdio_sys,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_component_runner as fcrunner, fidl_fuchsia_data as fdata,
    fidl_fuchsia_io::{DirectoryMarker, NodeMarker, OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE},
    fidl_fuchsia_process as fproc,
    fidl_fuchsia_process_lifecycle::{LifecycleMarker, LifecycleProxy},
    fuchsia_async as fasync,
    fuchsia_async::EHandle,
    fuchsia_component::client,
    fuchsia_runtime::{job_default, HandleInfo, HandleType},
    fuchsia_zircon::{self as zx, AsHandleRef, HandleBased, Job, Task},
    futures::future::{BoxFuture, FutureExt},
    log::{error, warn},
    std::convert::TryFrom,
    std::{fmt, path::Path, sync::Arc},
    thiserror::Error,
    vfs::{
        directory::entry::DirectoryEntry, directory::entry_container::DirectlyMutable,
        directory::immutable::simple as pfs, execution_scope::ExecutionScope,
        file::pcb::asynchronous::read_only_static, path::Path as fvfsPath, pseudo_directory,
        tree_builder::TreeBuilder,
    },
};

// Simple directory type which is used to implement `ComponentStartInfo.runtime_directory`.
type RuntimeDirectory = Arc<pfs::Simple>;

// Minimum timer slack amount and default mode. The amount should be large enough to allow for some
// coalescing of timers, but small enough to ensure applications don't miss deadlines.
//
// TODO(fxb/43934): For now, set the value to 50us to avoid delaying performance-critical
// timers in Scenic and other system services.
const TIMER_SLACK_DURATION: zx::Duration = zx::Duration::from_micros(50);

/// Errors produced by `ElfRunner`.
#[derive(Debug, Clone, Error)]
pub enum ElfRunnerError {
    #[error("failed to retrieve process koid for component with url \"{}\": {}", url, err)]
    ComponentProcessIdError {
        url: String,
        #[source]
        err: ClonableError,
    },
    #[error("failed to retrieve job koid for component with url \"{}\": {}", url, err)]
    ComponentJobIdError {
        url: String,
        #[source]
        err: ClonableError,
    },
    #[error("failed to set job policy for component with url \"{}\": {}", url, err)]
    ComponentJobPolicyError {
        url: String,
        #[source]
        err: ClonableError,
    },
    #[error("failed to create job for component with url \"{}\": {}", url, err)]
    ComponentJobCreationError {
        url: String,
        #[source]
        err: ClonableError,
    },
    #[error("failed to duplicate job for component with url \"{}\": {}", url, err)]
    ComponentJobDuplicationError {
        url: String,
        #[source]
        err: ClonableError,
    },
    #[error("failed to add runtime/elf directory for component with url \"{}\".", url)]
    ComponentElfDirectoryError { url: String },
    #[error(
        "the lifecyle event declaration(s) are invalid for the component with url \"{}\".",
        url
    )]
    LifecycleDeclarationError { url: String },
}

impl ElfRunnerError {
    pub fn component_process_id_error(
        url: impl Into<String>,
        err: impl Into<Error>,
    ) -> ElfRunnerError {
        ElfRunnerError::ComponentProcessIdError { url: url.into(), err: err.into().into() }
    }

    pub fn component_job_id_error(url: impl Into<String>, err: impl Into<Error>) -> ElfRunnerError {
        ElfRunnerError::ComponentJobIdError { url: url.into(), err: err.into().into() }
    }

    pub fn component_job_policy_error(
        url: impl Into<String>,
        err: impl Into<Error>,
    ) -> ElfRunnerError {
        ElfRunnerError::ComponentJobPolicyError { url: url.into(), err: err.into().into() }
    }

    pub fn component_job_creation_error(
        url: impl Into<String>,
        err: impl Into<Error>,
    ) -> ElfRunnerError {
        ElfRunnerError::ComponentJobCreationError { url: url.into(), err: err.into().into() }
    }

    pub fn component_job_duplication_error(
        url: impl Into<String>,
        err: impl Into<Error>,
    ) -> ElfRunnerError {
        ElfRunnerError::ComponentJobDuplicationError { url: url.into(), err: err.into().into() }
    }

    pub fn component_elf_directory_error(url: impl Into<String>) -> ElfRunnerError {
        ElfRunnerError::ComponentElfDirectoryError { url: url.into() }
    }
}

impl From<ElfRunnerError> for RunnerError {
    fn from(err: ElfRunnerError) -> Self {
        RunnerError::component_runtime_directory_error(err)
    }
}

/// Runs components with ELF binaries.
pub struct ElfRunner {
    launcher_connector: ProcessLauncherConnector,
}

enum LifecycleInterest {
    Notify,
    Ignore,
}

impl fmt::Display for LifecycleInterest {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        let out = {
            match self {
                Self::Notify => "notify",
                Self::Ignore => "ignore",
            }
        };
        writeln!(f, "{}", out)
    }
}

fn handle_info_from_fd(fd: i32) -> Result<Option<fproc::HandleInfo>, Error> {
    // TODO(CF-592): fdio is not guaranteed to be asynchronous, replace with native rust solution
    unsafe {
        let mut fd_handle = zx::sys::ZX_HANDLE_INVALID;
        let status = fdio_sys::fdio_fd_clone(fd, &mut fd_handle as *mut zx::sys::zx_handle_t);
        if status == zx::sys::ZX_ERR_INVALID_ARGS || status == zx::sys::ZX_ERR_NOT_SUPPORTED {
            // This file descriptor is closed or not clone-able.
            // We just skip it rather than generating an error.
            return Ok(None);
        }
        if status != zx::sys::ZX_OK {
            return Err(format_err!("failed to clone fd {}: {}", fd, status));
        }
        Ok(Some(fproc::HandleInfo {
            handle: zx::Handle::from_raw(fd_handle),
            id: HandleInfo::new(HandleType::FileDescriptor, fd as u16).as_raw(),
        }))
    }
}

impl ElfRunner {
    pub fn new(launcher_connector: ProcessLauncherConnector) -> ElfRunner {
        ElfRunner { launcher_connector }
    }

    async fn create_runtime_directory<'a>(
        &'a self,
        runtime_dir: ServerEnd<DirectoryMarker>,
        args: &'a Vec<String>,
    ) -> RuntimeDirectory {
        let mut runtime_tree_builder = TreeBuilder::empty_dir();
        let mut count: u32 = 0;
        for arg in args.iter() {
            let arg_copy = arg.clone();
            runtime_tree_builder
                .add_entry(["args", &count.to_string()], read_only_static(arg_copy.clone()))
                .expect("Failed to add arg to runtime directory");
            count += 1;
        }

        let runtime_directory = runtime_tree_builder.build();
        runtime_directory.clone().open(
            ExecutionScope::from_executor(Box::new(EHandle::local())),
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            0,
            fvfsPath::empty(),
            ServerEnd::<NodeMarker>::new(runtime_dir.into_channel()),
        );
        runtime_directory
    }

    async fn create_elf_directory(
        &self,
        runtime_dir: &RuntimeDirectory,
        resolved_url: &String,
        process_id: u64,
        job_id: u64,
    ) -> Result<(), RunnerError> {
        let elf_dir = pseudo_directory!(
            "process_id" => read_only_static(process_id.to_string()),
            "job_id" => read_only_static(job_id.to_string()),
        );
        runtime_dir
            .clone()
            .add_entry("elf", elf_dir)
            .map_err(|_| ElfRunnerError::component_elf_directory_error(resolved_url.clone()))?;
        Ok(())
    }

    fn find<'a>(dict: &'a fdata::Dictionary, key: &str) -> Option<&'a fdata::DictionaryValue> {
        match &dict.entries {
            Some(entries) => {
                for entry in entries {
                    if entry.key == key {
                        return entry.value.as_ref().map(|val| &**val);
                    }
                }
                None
            }
            _ => None,
        }
    }

    async fn configure_launcher(
        &self,
        resolved_url: &String,
        start_info: fcrunner::ComponentStartInfo,
        job: zx::Job,
        launcher: &fidl_fuchsia_process::LauncherProxy,
        lifecycle_server: Option<zx::Channel>,
    ) -> Result<Option<(fidl_fuchsia_process::LaunchInfo, RuntimeDirectory)>, RunnerError> {
        let bin_path = runner::get_program_binary(&start_info)
            .map_err(|e| RunnerError::invalid_args(resolved_url.clone(), e))?;

        let args = runner::get_program_args(&start_info)
            .map_err(|e| RunnerError::invalid_args(resolved_url.clone(), e))?;

        // TODO(fxb/45586): runtime_dir may be unavailable in tests. We should fix tests so
        // that we don't have to have this check here.
        let runtime_dir = match start_info.runtime_dir {
            Some(dir) => self.create_runtime_directory(dir, &args).await,
            None => return Ok(None),
        };

        let name = Path::new(&resolved_url)
            .file_name()
            .ok_or(format_err!("invalid url"))
            .map_err(|e| RunnerError::invalid_args(resolved_url.clone(), e))?
            .to_str()
            .ok_or(format_err!("invalid url"))
            .map_err(|e| RunnerError::invalid_args(resolved_url.clone(), e))?;

        // Convert the directories into proxies, so we can find "/pkg" and open "lib" and bin_path
        let ns = runner::component::ComponentNamespace::try_from(
            start_info.ns.unwrap_or_else(|| vec![]),
        )
        .map_err(|e| RunnerError::invalid_args(resolved_url.clone(), e))?;

        let mut handle_infos = vec![];

        // Copy standard input, output and error to process.
        for fd in 0..3 {
            handle_infos.extend(
                handle_info_from_fd(fd)
                    .map_err(|e| RunnerError::component_load_error(resolved_url.clone(), e))?,
            );
        }

        if let Some(outgoing_dir) = start_info.outgoing_dir {
            handle_infos.push(fproc::HandleInfo {
                handle: outgoing_dir.into_handle(),
                id: HandleInfo::new(HandleType::DirectoryRequest, 0).as_raw(),
            });
        }

        if let Some(lifecycle_chan) = lifecycle_server {
            handle_infos.push(fproc::HandleInfo {
                handle: lifecycle_chan.into_handle(),
                id: HandleInfo::new(HandleType::Lifecycle, 0).as_raw(),
            })
        };

        // Load the component
        let launch_info =
            runner::component::configure_launcher(runner::component::LauncherConfigArgs {
                bin_path: &bin_path,
                name: &name,
                args: Some(args),
                ns: ns,
                job: Some(job),
                handle_infos: Some(handle_infos),
                name_infos: None,
                environs: None,
                launcher: &launcher,
            })
            .await
            .map_err(|e| RunnerError::component_load_error(resolved_url.clone(), e))?;

        Ok(Some((launch_info, runtime_dir)))
    }

    async fn start_component(
        &self,
        start_info: fcrunner::ComponentStartInfo,
    ) -> Result<Option<ElfComponent>, RunnerError> {
        let resolved_url =
            runner::get_resolved_url(&start_info).map_err(|e| RunnerError::invalid_args("", e))?;

        let launcher = self
            .launcher_connector
            .connect()
            .context("failed to connect to launcher service")
            .map_err(|e| RunnerError::component_load_error(resolved_url.clone(), e))?;

        let component_job = job_default()
            .create_child_job()
            .map_err(|e| ElfRunnerError::component_job_creation_error(resolved_url.clone(), e))?;

        // Set timer slack.
        //
        // Why Late and not Center or Early? Timers firing a little later than requested is not
        // uncommon in non-realtime systems. Programs are generally tolerant of some
        // delays. However, timers firing before their deadline can be unexpected and lead to bugs.
        component_job
            .set_policy(zx::JobPolicy::TimerSlack(
                TIMER_SLACK_DURATION,
                zx::JobDefaultTimerMode::Late,
            ))
            .map_err(|e| ElfRunnerError::component_job_policy_error(resolved_url.clone(), e))?;

        // TODO(fxb/39947): The hermetic-decompressor library used in fshost requires the ability
        // to directly create new processes, and this policy breaks that.
        if resolved_url != "fuchsia-boot:///#meta/fshost.cm" {
            component_job
                .set_policy(zx::JobPolicy::Basic(
                    zx::JobPolicyOption::Absolute,
                    vec![(zx::JobCondition::NewProcess, zx::JobAction::Deny)],
                ))
                .map_err(|e| ElfRunnerError::component_job_policy_error(resolved_url.clone(), e))?;
        }

        let job_dup = component_job.duplicate_handle(zx::Rights::SAME_RIGHTS).map_err(|e| {
            ElfRunnerError::component_job_duplication_error(resolved_url.clone(), e)
        })?;

        let lifecycle_interest = {
            if let Some(program) = &start_info.program {
                match Self::find(&program, "lifecycle.stop_event") {
                    Some(fdata::DictionaryValue::Str(str_val)) => match &str_val[..] {
                        "notify" => Ok(LifecycleInterest::Notify),
                        "ignore" => Ok(LifecycleInterest::Ignore),
                        _ => Err(ElfRunnerError::LifecycleDeclarationError {
                            url: resolved_url.clone(),
                        }),
                    },
                    Some(_) => {
                        Err(ElfRunnerError::LifecycleDeclarationError { url: resolved_url.clone() })
                    }
                    None => Ok(LifecycleInterest::Ignore),
                }
            } else {
                Ok(LifecycleInterest::Ignore)
            }
        }?;

        let (lifecycle_client, lifecycle_server) = match lifecycle_interest {
            LifecycleInterest::Notify => fidl::endpoints::create_endpoints::<LifecycleMarker>()
                .map(|(c, s)| (Some(c), Some(s))),
            LifecycleInterest::Ignore => Ok((None, None)),
        }
        .map_err(|_| RunnerError::Unsupported)?;

        let lifecycle_server =
            lifecycle_server.map(|lifecycle_server| lifecycle_server.into_channel());
        let lifecycle_client = lifecycle_client.map(|lifecycle_client| {
            lifecycle_client.into_proxy().expect("converion to proxy failed")
        });

        let (mut launch_info, runtime_dir) = match self
            .configure_launcher(&resolved_url, start_info, job_dup, &launcher, lifecycle_server)
            .await?
        {
            Some(s) => s,
            None => return Ok(None),
        };

        let job_koid = component_job
            .get_koid()
            .map_err(|e| ElfRunnerError::component_job_id_error(resolved_url.clone(), e))?
            .raw_koid();

        // Launch the component
        let process_koid = async {
            let (status, process) = launcher
                .launch(&mut launch_info)
                .await
                .map_err(|e| RunnerError::component_launch_error(&*resolved_url, e))?;
            if zx::Status::from_raw(status) != zx::Status::OK {
                return Err(RunnerError::component_launch_error(
                    resolved_url.clone(),
                    format_err!("failed to launch component: {}", status),
                ));
            }

            let mut process_koid = 0;
            if let Some(process) = &process {
                process_koid = process
                    .get_koid()
                    .map_err(|e| {
                        ElfRunnerError::component_process_id_error(resolved_url.clone(), e)
                    })?
                    .raw_koid();
            }
            Ok(process_koid)
        }
        .await
        .map_err(|e| RunnerError::component_launch_error(resolved_url.clone(), e))?;

        self.create_elf_directory(&runtime_dir, &resolved_url, process_koid, job_koid).await?;
        Ok(Some(ElfComponent::new(runtime_dir, Job::from(component_job), lifecycle_client)))
    }
}

/// Structure representing a running elf component.
struct ElfComponent {
    /// namespace directory for this component, kept just as a reference to
    /// keep the namespace alive
    _runtime_dir: RuntimeDirectory,

    /// job in which the underlying process is running.
    job: Arc<Job>,

    lifecycle_channel: Option<LifecycleProxy>,
}

impl ElfComponent {
    pub fn new(
        _runtime_dir: RuntimeDirectory,
        job: Job,
        lifecycle_channel: Option<LifecycleProxy>,
    ) -> Self {
        Self { _runtime_dir, job: Arc::new(job), lifecycle_channel }
    }
}

#[async_trait]
impl runner::component::Controllable for ElfComponent {
    async fn kill(mut self) {
        let _ = self.job.kill().map_err(|e| error!("failed killing job during kill: {}", e));
    }

    fn stop<'a>(&mut self) -> BoxFuture<'a, ()> {
        if let Some(lifecycle_chan) = self.lifecycle_channel.take() {
            let _ = lifecycle_chan.stop();

            let job = self.job.clone();
            async move {
                let _ = fasync::OnSignals::new(
                    &lifecycle_chan.as_handle_ref(),
                    zx::Signals::CHANNEL_PEER_CLOSED,
                )
                .await
                .map_err(|e| { error!("killing component's job after failure waiting on lifecycle channel, err: {}", e) } );
                let _ = job.kill().map_err(|e| error!("failed killing job in stop after lifecycle channel closed: {}", e));
            }
            .boxed()
        } else {
            let _ = self.job.kill().map_err(|e| {
                error!("failed killing job for component with no lifecycle channel: {}", e)
            });
            async {}.boxed()
        }
    }
}

impl Drop for ElfComponent {
    fn drop(&mut self) {
        // just in case we haven't killed the job already
        let _ = self.job.kill().map_err(|e| error!("failed to kill job in drop: {}", e));
    }
}

#[async_trait]
impl Runner for ElfRunner {
    /// Starts a component by creating a new Job and Process for the component.
    /// The Runner also creates and hosts a namespace for the component. The
    /// namespace and other runtime state of the component will live until the
    /// Future returned is dropped or the `server_end` is sent either
    /// `ComponentController.Stop` or `ComponentController.Kill`. Sending
    /// `ComponentController.Stop` or `ComponentController.Kill` causes the
    /// Future to complete.
    async fn start(
        &self,
        start_info: fcrunner::ComponentStartInfo,
        server_end: ServerEnd<fcrunner::ComponentControllerMarker>,
    ) {
        // start the component and move the Controller into a new async
        // execution context.
        match self.start_component(start_info).await {
            Ok(Some(elf_component)) => {
                // This future completes when the
                // Controller is told to stop/kill the component.
                fasync::spawn(async move {
                    let server_stream = server_end.into_stream().expect("failed to convert");
                    runner::component::Controller::new(elf_component, server_stream)
                        .serve()
                        .await
                        .unwrap_or_else(|e| warn!("serving ComponentController failed: {}", e));
                });
            }
            Ok(None) => {
                // TODO(fxb/): Should this signal an error?
            }
            Err(err) => {
                // Deliver any errors as epitaphs over ComponentController.
                server_end.close_with_epitaph(err.as_zx_status()).unwrap_or_else(|e| {
                    warn!("failed to send epitaph on ComponentController channel: {}", e);
                });
            }
        }
    }
}

/// Connects to the appropriate `fuchsia.process.Launcher` service based on the options provided in
/// `ProcessLauncherConnector::new`.
///
/// This exists so that callers can make a new connection to `fuchsia.process.Launcher` for each use
/// because the service is stateful per connection, so it is not safe to share a connection between
/// multiple asynchronous process launchers.
///
/// If `LibraryOpts::use_builtin_process_launcher` is true, this will connect to the built-in
/// `fuchsia.process.Launcher` service using the provided `ProcessLauncher`. Otherwise, this connects
/// to the launcher service under /svc in component_manager's namespace.
pub struct ProcessLauncherConnector {
    use_builtin: bool,
}

impl ProcessLauncherConnector {
    pub fn new(args: &Arguments) -> Self {
        Self { use_builtin: args.use_builtin_process_launcher }
    }

    pub fn connect(&self) -> Result<fproc::LauncherProxy, Error> {
        let proxy = if self.use_builtin {
            let (proxy, stream) =
                fidl::endpoints::create_proxy_and_stream::<fproc::LauncherMarker>()?;
            fasync::spawn(async move {
                let result = ProcessLauncher::serve(stream).await;
                if let Err(e) = result {
                    warn!("ProcessLauncherConnector.connect failed: {}", e);
                }
            });
            proxy
        } else {
            client::connect_to_service::<fproc::LauncherMarker>()
                .context("failed to connect to external launcher service")?
        };
        Ok(proxy)
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl::endpoints::{create_proxy, ClientEnd, Proxy},
        fidl_fuchsia_data as fdata,
        fidl_fuchsia_io::DirectoryProxy,
        fuchsia_async as fasync,
        futures::stream::StreamExt,
        io_util,
        matches::assert_matches,
        runner::component::Controllable,
        std::task::Poll,
    };

    // Rust's test harness does not allow passing through arbitrary arguments, so to get coverage
    // for the different LibraryOpts configurations (which would normally be set based on
    // arguments) we switch based on the test binary name.
    fn should_use_builtin_process_launcher() -> bool {
        // This is somewhat fragile but intentionally so, so that this will fail if the binary
        // names change and get updated properly.
        let bin = std::env::args().next();
        match bin.as_ref().map(String::as_ref) {
            Some("/pkg/test/component_manager_tests") => false,
            Some("/pkg/test/component_manager_boot_env_tests") => true,
            _ => panic!("Unexpected test binary name {:?}", bin),
        }
    }

    fn hello_world_startinfo(
        runtime_dir: Option<ServerEnd<DirectoryMarker>>,
    ) -> fcrunner::ComponentStartInfo {
        // Get a handle to /pkg
        let pkg_path = "/pkg".to_string();
        let pkg_chan = io_util::open_directory_in_namespace("/pkg", OPEN_RIGHT_READABLE)
            .unwrap()
            .into_channel()
            .unwrap()
            .into_zx_channel();
        let pkg_handle = ClientEnd::new(pkg_chan);

        let ns = vec![fcrunner::ComponentNamespaceEntry {
            path: Some(pkg_path),
            directory: Some(pkg_handle),
        }];

        fcrunner::ComponentStartInfo {
            resolved_url: Some(
                "fuchsia-pkg://fuchsia.com/hello_world_hippo#meta/hello_world.cm".to_string(),
            ),
            program: Some(fdata::Dictionary {
                entries: Some(vec![
                    fdata::DictionaryEntry {
                        key: "args".to_string(),
                        value: Some(Box::new(fdata::DictionaryValue::StrVec(vec![
                            "foo".to_string(),
                            "bar".to_string(),
                        ]))),
                    },
                    fdata::DictionaryEntry {
                        key: "binary".to_string(),
                        value: Some(Box::new(fdata::DictionaryValue::Str(
                            "bin/hello_world".to_string(),
                        ))),
                    },
                ]),
            }),
            ns: Some(ns),
            outgoing_dir: None,
            runtime_dir,
        }
    }

    // TODO(fsamuel): A variation of this is used in a couple of places. We should consider
    // refactoring this into a test util file.
    async fn read_file<'a>(root_proxy: &'a DirectoryProxy, path: &'a str) -> String {
        let file_proxy =
            io_util::open_file(&root_proxy, &Path::new(path), io_util::OPEN_RIGHT_READABLE)
                .expect("Failed to open file.");
        let res = io_util::read_file(&file_proxy).await;
        res.expect("Unable to read file.")
    }

    #[fasync::run_singlethreaded(test)]
    async fn hello_world_test() -> Result<(), Error> {
        let (runtime_dir_client, runtime_dir_server) = zx::Channel::create()?;
        let start_info = hello_world_startinfo(Some(ServerEnd::new(runtime_dir_server)));

        let runtime_dir_proxy = DirectoryProxy::from_channel(
            fasync::Channel::from_channel(runtime_dir_client).unwrap(),
        );

        let args = Arguments {
            use_builtin_process_launcher: should_use_builtin_process_launcher(),
            ..Default::default()
        };
        let launcher_connector = ProcessLauncherConnector::new(&args);
        let runner = ElfRunner::new(launcher_connector);
        let (controller, server_controller) = create_proxy::<fcrunner::ComponentControllerMarker>()
            .expect("could not create component controller endpoints");

        // TODO: This test currently results in a bunch of log spew when this test process exits
        // because this does not stop the component, which means its loader service suddenly goes
        // away. Stop the component when the Runner trait provides a way to do so.
        runner.start(start_info, server_controller).await;

        // Verify that args are added to the runtime directory.
        assert_eq!("foo", read_file(&runtime_dir_proxy, "args/0").await);
        assert_eq!("bar", read_file(&runtime_dir_proxy, "args/1").await);

        // Process Id and Job Id will vary with every run of this test. Here we verify that
        // they exist in the runtime directory, they can be parsed as unsigned integers, they're
        // greater than zero and they are not the same value. Those are about the only invariants
        // we can verify across test runs.
        let process_id = read_file(&runtime_dir_proxy, "elf/process_id").await.parse::<u64>()?;
        let job_id = read_file(&runtime_dir_proxy, "elf/job_id").await.parse::<u64>()?;
        assert!(process_id > 0);
        assert!(job_id > 0);
        assert_ne!(process_id, job_id);

        // Kill the process before finishing the test so that it doesn't pagefault due to an
        // invalid stdout handle.
        controller.kill().expect("kill failed");
        fasync::OnSignals::new(&controller.as_handle_ref(), zx::Signals::CHANNEL_PEER_CLOSED)
            .await
            .expect("failed waiting for channel to close");
        Ok(())
    }

    // This test checks that starting a component fails if we use the wrong built-in process
    // launcher setting for the test environment. This helps ensure that the test above isn't
    // succeeding for an unexpected reason, e.g. that it isn't using a fuchsia.process.Launcher
    // from the test's namespace instead of serving and using a built-in one.
    #[fasync::run_singlethreaded(test)]
    async fn hello_world_fail_test() -> Result<(), Error> {
        let (_runtime_dir_client, runtime_dir_server) = zx::Channel::create()?;
        let start_info = hello_world_startinfo(Some(ServerEnd::new(runtime_dir_server)));

        // Note that value of should_use... is negated
        let args = Arguments {
            use_builtin_process_launcher: !should_use_builtin_process_launcher(),
            ..Default::default()
        };
        let launcher_connector = ProcessLauncherConnector::new(&args);
        let runner = ElfRunner::new(launcher_connector);
        let (client_controller, server_controller) =
            create_proxy::<fcrunner::ComponentControllerMarker>()
                .expect("could not create component controller endpoints");

        runner.start(start_info, server_controller).await;
        assert_matches!(
            client_controller.take_event_stream().next().await.unwrap(),
            Err(fidl::Error::ClientChannelClosed(_))
        );
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_kill_component() -> Result<(), Error> {
        let job = job_default().create_child_job()?;

        // create a nested job as empty job can't be killed for some reason.
        let _child_job = job.create_child_job()?;

        let job_copy = Job::from(
            job.as_handle_ref()
                .duplicate(zx::Rights::SAME_RIGHTS)
                .expect("job handle duplication failed"),
        );

        let component = ElfComponent::new(TreeBuilder::empty_dir().build(), job, None);

        let job_info = job_copy.info()?;
        assert!(!job_info.exited);

        component.kill().await;

        let h = job_copy.as_handle_ref();
        fasync::OnSignals::new(&h, zx::Signals::TASK_TERMINATED)
            .await
            .expect("failed waiting for termination signal");

        let job_info = job_copy.info()?;
        assert!(job_info.exited);
        Ok(())
    }

    #[test]
    fn test_stop_component() -> Result<(), Error> {
        let mut exec = fasync::Executor::new().expect("Unable to create new executor");
        let (lifecycle_client, lifecycle_server) =
            fidl::endpoints::create_proxy_and_stream::<LifecycleMarker>()?;

        let job = job_default().create_child_job()?;

        // create a nested job as empty job can't be killed for some reason.
        let _child_job = job.create_child_job()?;

        let job_copy = Job::from(
            job.as_handle_ref()
                .duplicate(zx::Rights::SAME_RIGHTS)
                .expect("job handle duplication failed"),
        );

        let mut component =
            ElfComponent::new(TreeBuilder::empty_dir().build(), job, Some(lifecycle_client));

        let job_info = job_copy.info()?;
        assert!(!job_info.exited);

        // Ask the runner to stop the component, it returns a future which
        // completes when the component closes its side of the lifecycle
        // channel
        let mut completes_when_stopped = component.stop();

        // The returned future shouldn't complete because we're holding the
        // lifecycle channel open.
        match exec.run_until_stalled(&mut completes_when_stopped) {
            Poll::Ready(_) => {
                panic!("runner should still be waiting for lifecycle channel to stop");
            }
            _ => {}
        }

        // Close our side of the lifecycle channel
        drop(lifecycle_server);

        // We now expect the future to complete
        match exec.run_until_stalled(&mut completes_when_stopped) {
            Poll::Ready(_) => {}
            _ => {
                panic!("runner future should have completed, lifecycle channel is closed");
            }
        }

        // Check that the runner killed the job hosting the exited component.
        let h = job_copy.as_handle_ref();
        let termination_fut = async move {
            fasync::OnSignals::new(&h, zx::Signals::TASK_TERMINATED)
                .await
                .expect("failed waiting for termination signal");
        };
        exec.run_singlethreaded(termination_fut);

        let job_info = job_copy.info()?;
        assert!(job_info.exited);
        Ok(())
    }

    /// Stopping a component which doesn't have a lifecycle channel should be
    /// equivalent to killing a component directly.
    #[fasync::run_singlethreaded(test)]
    async fn test_stop_component_without_lifecycle() -> Result<(), Error> {
        let job = job_default().create_child_job()?;

        // create a nested job as empty job can't be killed for some reason.
        let _child_job = job.create_child_job()?;

        let job_copy = Job::from(
            job.as_handle_ref()
                .duplicate(zx::Rights::SAME_RIGHTS)
                .expect("job handle duplication failed"),
        );

        let mut component = ElfComponent::new(TreeBuilder::empty_dir().build(), job, None);

        let job_info = job_copy.info()?;
        assert!(!job_info.exited);

        component.stop().await;

        let h = job_copy.as_handle_ref();
        fasync::OnSignals::new(&h, zx::Signals::TASK_TERMINATED)
            .await
            .expect("failed waiting for termination signal");

        let job_info = job_copy.info()?;
        assert!(job_info.exited);
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_stop_component_with_closed_lifecycle() -> Result<(), Error> {
        let job = job_default().create_child_job()?;
        let (lifecycle_client, lifecycle_server) =
            fidl::endpoints::create_proxy_and_stream::<LifecycleMarker>()?;
        // Create a nested job as empty job can't be killed for some reason.
        let _child_job = job.create_child_job()?;

        let job_copy = Job::from(
            job.as_handle_ref()
                .duplicate(zx::Rights::SAME_RIGHTS)
                .expect("job handle duplication failed"),
        );

        let mut component =
            ElfComponent::new(TreeBuilder::empty_dir().build(), job, Some(lifecycle_client));

        let job_info = job_copy.info()?;
        assert!(!job_info.exited);

        // Close the lifecycle channel
        drop(lifecycle_server);
        component.stop().await;

        let h = job_copy.as_handle_ref();
        fasync::OnSignals::new(&h, zx::Signals::TASK_TERMINATED)
            .await
            .expect("failed waiting for termination signal");

        let job_info = job_copy.info()?;
        assert!(job_info.exited);
        Ok(())
    }

    /// Dropping the component should kill the job hosting it.
    #[fasync::run_singlethreaded(test)]
    async fn test_drop() -> Result<(), Error> {
        let job = job_default().create_child_job()?;

        // create a nested job as empty job can't be killed for some reason.
        let _child_job = job.create_child_job()?;

        let job_copy = Job::from(
            job.as_handle_ref()
                .duplicate(zx::Rights::SAME_RIGHTS)
                .expect("job handle duplication failed"),
        );

        let component = ElfComponent::new(TreeBuilder::empty_dir().build(), job, None);

        let job_info = job_copy.info()?;
        assert!(!job_info.exited);

        drop(component);

        let h = job_copy.as_handle_ref();
        fasync::OnSignals::new(&h, zx::Signals::TASK_TERMINATED)
            .await
            .expect("failed waiting for termination signal");

        let job_info = job_copy.info()?;
        assert!(job_info.exited);
        Ok(())
    }
}

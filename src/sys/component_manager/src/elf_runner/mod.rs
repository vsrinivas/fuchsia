// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        builtin::process_launcher::ProcessLauncher,
        constants::PKG_PATH,
        model::runner::{Runner, RunnerError},
        startup::Arguments,
    },
    anyhow::{format_err, Context as _, Error},
    async_trait::async_trait,
    clonable_error::ClonableError,
    fdio::fdio_sys,
    fidl::endpoints::{ClientEnd, ServerEnd},
    fidl_fuchsia_io::{
        DirectoryMarker, DirectoryProxy, NodeMarker, OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE,
    },
    fidl_fuchsia_process as fproc, fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync,
    fuchsia_async::EHandle,
    fuchsia_component::client,
    fuchsia_runtime::{job_default, HandleInfo, HandleType},
    fuchsia_vfs_pseudo_fs_mt::{
        directory::entry::DirectoryEntry, directory::entry_container::DirectlyMutable,
        directory::immutable::simple as pfs, execution_scope::ExecutionScope,
        file::pcb::asynchronous::read_only_static, path::Path as fvfsPath, pseudo_directory,
        tree_builder::TreeBuilder,
    },
    fuchsia_zircon::{self as zx, AsHandleRef, HandleBased, Job, Task},
    futures::future::BoxFuture,
    library_loader,
    log::warn,
    std::{path::Path, sync::Arc},
    thiserror::Error,
};

// Simple directory type which is used to implement `ComponentStartInfo.runtime_directory`.
type RuntimeDirectory = Arc<pfs::Simple>;

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
    #[error("failed to add runtime/elf directory for component with url \"{}\".", url)]
    ComponentElfDirectoryError { url: String },
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

    async fn load_launch_info(
        &self,
        url: &str,
        start_info: fsys::ComponentStartInfo,
        launcher: &fproc::LauncherProxy,
    ) -> Result<(Option<RuntimeDirectory>, fproc::LaunchInfo), Error> {
        let bin_path = runner::get_program_binary(&start_info)
            .map_err(|e| RunnerError::invalid_args(url, e))?;
        let bin_arg = &[String::from(
            PKG_PATH.join(&bin_path).to_str().ok_or(format_err!("invalid binary path"))?,
        )];
        let args = runner::get_program_args(&start_info)?;

        let name = Path::new(url)
            .file_name()
            .ok_or(format_err!("invalid url"))?
            .to_str()
            .ok_or(format_err!("invalid url"))?;

        // Convert the directories into proxies, so we can find "/pkg" and open "lib" and bin_path
        let ns = start_info
            .ns
            .unwrap_or(fsys::ComponentNamespace { paths: vec![], directories: vec![] });
        let mut paths = ns.paths;
        let directories: Result<Vec<DirectoryProxy>, fidl::Error> =
            ns.directories.into_iter().map(|d| d.into_proxy()).collect();
        let mut directories = directories?;

        // Start the library loader service
        let pkg_str = PKG_PATH.to_str().unwrap();
        let (ll_client_chan, ll_service_chan) = zx::Channel::create()?;
        let (_, pkg_proxy) = paths
            .iter()
            .zip(directories.iter())
            .find(|(p, _)| p.as_str() == pkg_str)
            .ok_or(format_err!("/pkg missing from namespace"))?;

        let lib_proxy = io_util::open_directory(pkg_proxy, &Path::new("lib"), OPEN_RIGHT_READABLE)?;

        // The loader service should only be able to load files from `/pkg/lib`. Giving it a larger
        // scope is potentially a security vulnerability, as it could make it trivial for parts of
        // applications to get handles to things the application author didn't intend.
        library_loader::start(lib_proxy, ll_service_chan);

        let executable_vmo = library_loader::load_vmo(pkg_proxy, &bin_path)
            .await
            .context("error loading executable")?;

        let child_job = job_default().create_child_job()?;

        // Set the minimum timer slack amount and default mode. The amount should be large enough to
        // allow for some coalescing of timers, but small enough to ensure applications don't miss
        // deadlines.
        //
        // Why Late and not Center or Early? Timers firing a little later than requested is not
        // uncommon in non-realtime systems. Programs are generally tolerant of some
        // delays. However, timers firing before their dealine can be unexpected and lead to bugs.
        //
        // TODO(fxb/43934): For now, set the value to 50us to avoid delaying performance-critical
        // timers in Scenic and other system services.
        child_job
            .set_policy(zx::JobPolicy::TimerSlack(
                zx::Duration::from_micros(50),
                zx::JobDefaultTimerMode::Late,
            ))
            .context("error setting job policy to configure timer slack")?;

        // TODO(fxb/39947): The hermetic-decompressor library used in fshost requires the ability
        // to directly create new processes, and this policy breaks that.
        if url != "fuchsia-boot:///#meta/fshost.cm" {
            child_job
                .set_policy(zx::JobPolicy::Basic(
                    zx::JobPolicyOption::Absolute,
                    vec![(zx::JobCondition::NewProcess, zx::JobAction::Deny)],
                ))
                .context("error setting job policy to deny new processes")?;
        }

        let child_job_dup = child_job.duplicate_handle(zx::Rights::SAME_RIGHTS)?;

        let mut string_iters: Vec<_> =
            bin_arg.iter().chain(args.iter()).map(|s| s.bytes()).collect();
        launcher.add_args(
            &mut string_iters.iter_mut().map(|iter| iter as &mut dyn ExactSizeIterator<Item = u8>),
        )?;
        // TODO: launcher.AddEnvirons

        let mut handle_infos = vec![];
        for fd in 0..3 {
            handle_infos.extend(handle_info_from_fd(fd)?);
        }

        handle_infos.append(&mut vec![
            fproc::HandleInfo {
                handle: ll_client_chan.into_handle(),
                id: HandleInfo::new(HandleType::LdsvcLoader, 0).as_raw(),
            },
            fproc::HandleInfo {
                handle: child_job_dup.into_handle(),
                id: HandleInfo::new(HandleType::DefaultJob, 0).as_raw(),
            },
        ]);
        if let Some(outgoing_dir) = start_info.outgoing_dir {
            handle_infos.push(fproc::HandleInfo {
                handle: outgoing_dir.into_handle(),
                id: HandleInfo::new(HandleType::DirectoryRequest, 0).as_raw(),
            });
        }
        launcher.add_handles(&mut handle_infos.iter_mut())?;

        let mut name_infos = vec![];
        while let Some(path) = paths.pop() {
            if let Some(directory) = directories.pop() {
                let directory = ClientEnd::new(
                    directory
                        .into_channel()
                        .map_err(|_| format_err!("into_channel failed"))?
                        .into_zx_channel(),
                );
                name_infos.push(fproc::NameInfo { path, directory });
            }
        }
        launcher.add_names(&mut name_infos.iter_mut())?;

        let mut runtime_dir = None;
        // TODO(fsamuel): runtime_dir may be unavailable in tests. We should fix tests so
        // that we don't have to have this check here.
        if let Some(dir) = start_info.runtime_dir {
            runtime_dir = Some(self.create_runtime_directory(dir, &args).await);
        }

        Ok((
            runtime_dir,
            fproc::LaunchInfo { executable: executable_vmo, job: child_job, name: name.to_owned() },
        ))
    }

    async fn start_component(
        &self,
        start_info: fsys::ComponentStartInfo,
        server_end: ServerEnd<fsys::ComponentControllerMarker>,
    ) -> Result<Option<runner::component::Controller<ElfComponent>>, RunnerError> {
        let resolved_url =
            runner::get_resolved_url(&start_info).map_err(|e| RunnerError::invalid_args("", e))?;

        let launcher = self
            .launcher_connector
            .connect()
            .context("failed to connect to launcher service")
            .map_err(|e| RunnerError::component_load_error(&*resolved_url, e))?;

        // Load the component
        let (runtime_dir, mut launch_info) = self
            .load_launch_info(&resolved_url, start_info, &launcher)
            .await
            .context("loading launch info failed")
            .map_err(|e| RunnerError::component_load_error(&*resolved_url, e))?;

        let job_koid = launch_info
            .job
            .get_koid()
            .map_err(|e| ElfRunnerError::component_job_id_error(resolved_url.clone(), e))?
            .raw_koid();

        let component_job = launch_info
            .job
            .as_handle_ref()
            .duplicate(zx::Rights::SAME_RIGHTS)
            .expect("handle duplication failed!");

        // Launch the component
        let process_koid = async {
            let (status, process) = launcher.launch(&mut launch_info).await?;
            if zx::Status::from_raw(status) != zx::Status::OK {
                return Err(format_err!("failed to launch component: {}", status));
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

        if let Some(runtime_dir) = runtime_dir {
            self.create_elf_directory(&runtime_dir, &resolved_url, process_koid, job_koid).await?;
            let server_stream = server_end.into_stream().expect("failed to convert");
            let controller = runner::component::Controller::new(
                ElfComponent::new(runtime_dir, Job::from(component_job)),
                server_stream,
            );
            Ok(Some(controller))
        } else {
            Ok(None)
        }
    }

    async fn start_async(
        &self,
        start_info: fsys::ComponentStartInfo,
        server_end: ServerEnd<fsys::ComponentControllerMarker>,
    ) -> Result<(), RunnerError> {
        // start the component and move any Controller into a new async
        // execution context. This future completes when the
        // Controller is told to stop/kill the component.
        self.start_component(start_info, server_end)
            .await
            .map(|controller_opt| -> Option<()> {
                controller_opt.and_then(|controller| {
                    fasync::spawn(async move {
                        let _ = controller.serve().await;
                    });
                    Some(())
                })
            })
            .map(|_option_unit| ())
    }
}

/// Structure representing a running elf component.
struct ElfComponent {
    /// namespace directory for this component, kept just as a reference to
    /// keep the namespace alive
    _runtime_dir: RuntimeDirectory,

    /// job in which the underlying process is running.
    job: Job,
}

impl ElfComponent {
    pub fn new(_runtime_dir: RuntimeDirectory, job: Job) -> Self {
        Self { _runtime_dir, job }
    }
}

#[async_trait]
impl runner::component::Killable for ElfComponent {
    async fn kill(self) {
        let _ = self.job.kill();
    }
}

impl Runner for ElfRunner {
    /// Starts a component by creating a new Job and Process for the component.
    /// The Runner also creates and hosts a namespace for the component. The
    /// namespace and other runtime state of the component will live until the
    /// Future returned is dropped or the `server_end` is sent either
    /// `ComponentController.Stop` or `ComponentController.Kill`. Sending
    /// `ComponentController.Stop` or `ComponentController.Kill` causes the
    /// Future to complete.
    fn start(
        &self,
        start_info: fsys::ComponentStartInfo,
        server_end: ServerEnd<fsys::ComponentControllerMarker>,
    ) -> BoxFuture<Result<(), RunnerError>> {
        Box::pin(self.start_async(start_info, server_end))
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
        fidl_fuchsia_data as fdata, fuchsia_async as fasync, io_util,
        runner::component::Killable,
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
    ) -> fsys::ComponentStartInfo {
        // Get a handle to /pkg
        let pkg_path = "/pkg".to_string();
        let pkg_chan = io_util::open_directory_in_namespace("/pkg", OPEN_RIGHT_READABLE)
            .unwrap()
            .into_channel()
            .unwrap()
            .into_zx_channel();
        let pkg_handle = ClientEnd::new(pkg_chan);

        let ns = fsys::ComponentNamespace { paths: vec![pkg_path], directories: vec![pkg_handle] };

        fsys::ComponentStartInfo {
            resolved_url: Some(
                "fuchsia-pkg://fuchsia.com/hello_world_hippo#meta/hello_world.cm".to_string(),
            ),
            program: Some(fdata::Dictionary {
                entries: vec![
                    fdata::Entry {
                        key: "binary".to_string(),
                        value: Some(Box::new(fdata::Value::Str("bin/hello_world".to_string()))),
                    },
                    fdata::Entry {
                        key: "args".to_string(),
                        value: Some(Box::new(fdata::Value::Vec(fdata::Vector {
                            values: vec![
                                Some(Box::new(fdata::Value::Str("foo".to_string()))),
                                Some(Box::new(fdata::Value::Str("bar".to_string()))),
                            ],
                        }))),
                    },
                ],
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
        let (controller, server_controller) = create_proxy::<fsys::ComponentControllerMarker>()
            .expect("could not create component controller endpoints");

        // TODO: This test currently results in a bunch of log spew when this test process exits
        // because this does not stop the component, which means its loader service suddenly goes
        // away. Stop the component when the Runner trait provides a way to do so.
        runner
            .start_async(start_info, server_controller)
            .await
            .expect("hello_world_test start failed");

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
        let start_info = hello_world_startinfo(None);

        // Note that value of should_use... is negated
        let args = Arguments {
            use_builtin_process_launcher: !should_use_builtin_process_launcher(),
            ..Default::default()
        };
        let launcher_connector = ProcessLauncherConnector::new(&args);
        let runner = ElfRunner::new(launcher_connector);
        let (_controller, server_controller) = create_proxy::<fsys::ComponentControllerMarker>()
            .expect("could not create component controller endpoints");

        match runner.start_async(start_info, server_controller).await {
            Ok(_) => Err(format_err!("hello_world_fail_test succeeded unexpectedly")),
            Err(_) => Ok(()),
        }
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

        let component = ElfComponent::new(TreeBuilder::empty_dir().build(), job);

        let job_info = job_copy.info()?;
        if job_info.exited {
            return Err(format_err!("job exited unexpectedly."));
        }

        component.kill().await;

        let h = job_copy.as_handle_ref();
        fasync::OnSignals::new(&h, zx::Signals::TASK_TERMINATED)
            .await
            .expect("failed waiting for termination signal");

        let job_info = job_copy.info()?;
        if !job_info.exited {
            return Err(format_err!("job should have exited, but did not."));
        }
        Ok(())
    }
}

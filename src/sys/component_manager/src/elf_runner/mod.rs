// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        constants::PKG_PATH,
        model::{Runner, RunnerError},
        process_launcher::ProcessLauncher,
        startup::Arguments,
    },
    clonable_error::ClonableError,
    cm_rust::data::DictionaryExt,
    failure::{err_msg, format_err, Error, Fail, ResultExt},
    fdio::fdio_sys,
    fidl::endpoints::{ClientEnd, ServerEnd},
    fidl_fuchsia_data as fdata,
    fidl_fuchsia_io::{
        DirectoryMarker, DirectoryProxy, NodeMarker, OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE,
    },
    fidl_fuchsia_process as fproc, fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync,
    fuchsia_component::client,
    fuchsia_runtime::{job_default, HandleInfo, HandleType},
    fuchsia_vfs_pseudo_fs::{
        directory::{self, entry::DirectoryEntry},
        file::simple::read_only,
    },
    fuchsia_zircon::{self as zx, AsHandleRef, HandleBased, Job, Task},
    futures::{
        future::{AbortHandle, Abortable, BoxFuture},
        TryStreamExt,
    },
    library_loader,
    log::warn,
    std::{boxed::Box, iter, path::Path},
};

// TODO(fsamuel): We might want to store other things in this struct in the
// future, in which case, RuntimeDirectory might not be an appropriate name.
pub struct RuntimeDirectory {
    pub controller: directory::controlled::Controller<'static>,
    /// Called when a component's execution is terminated to drop the 'runtime'
    /// directory.
    abort_handle: AbortHandle,
}

impl Drop for RuntimeDirectory {
    fn drop(&mut self) {
        self.abort_handle.abort();
    }
}

/// Errors produced by `ElfRunner`.
#[derive(Debug, Clone, Fail)]
pub enum ElfRunnerError {
    #[fail(
        display = "failed to retrieve process koid for component with url \"{}\": {}",
        url, err
    )]
    ComponentProcessIdError {
        url: String,
        #[fail(cause)]
        err: ClonableError,
    },
    #[fail(display = "failed to retrieve job koid for component with url \"{}\": {}", url, err)]
    ComponentJobIdError {
        url: String,
        #[fail(cause)]
        err: ClonableError,
    },
    #[fail(display = "failed to add runtime/elf directory for component with url \"{}\".", url)]
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

fn get_resolved_url(start_info: &fsys::ComponentStartInfo) -> Result<String, Error> {
    match &start_info.resolved_url {
        Some(url) => Ok(url.to_string()),
        _ => Err(err_msg("missing url")),
    }
}

fn get_program_binary(start_info: &fsys::ComponentStartInfo) -> Result<String, Error> {
    if let Some(program) = &start_info.program {
        if let Some(binary) = program.find("binary") {
            if let fdata::Value::Str(bin) = binary {
                return Ok(bin.to_string());
            }
        }
    }
    Err(err_msg("\"binary\" must be specified"))
}

fn get_program_args(start_info: &fsys::ComponentStartInfo) -> Result<Vec<String>, Error> {
    if let Some(program) = &start_info.program {
        if let Some(args) = program.find("args") {
            if let fdata::Value::Vec(vec) = args {
                return vec
                    .values
                    .iter()
                    .map(|v| {
                        if let Some(fdata::Value::Str(a)) = v.as_ref().map(|x| &**x) {
                            Ok(a.clone())
                        } else {
                            Err(err_msg("invalid type in arguments"))
                        }
                    })
                    .collect();
            }
        }
    }
    Ok(vec![])
}

fn handle_info_from_fd(fd: i32) -> Result<Option<fproc::HandleInfo>, Error> {
    // TODO(CF-592): fdio is not guaranteed to be asynchronous, replace with native rust solution
    unsafe {
        let mut fd_handle = zx::sys::ZX_HANDLE_INVALID;
        let status = fdio_sys::fdio_fd_clone(fd, &mut fd_handle as *mut zx::sys::zx_handle_t);
        if status == zx::sys::ZX_ERR_INVALID_ARGS {
            // This file descriptor is closed. We just skip it rather than
            // generating an error.
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
        let (runtime_controller, mut runtime_controlled) =
            directory::controlled::controlled(directory::simple::empty());
        runtime_controlled.open(
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            0,
            &mut iter::empty(),
            ServerEnd::<NodeMarker>::new(runtime_dir.into_channel()),
        );

        let mut count: u32 = 0;
        let mut args_dir = directory::simple::empty();
        for arg in args.iter() {
            let arg_copy = arg.clone();
            let _ = args_dir.add_entry(&count.to_string(), {
                read_only(move || Ok(arg_copy.clone().into_bytes()))
            });
            count += 1;
        }
        let _ = runtime_controlled.add_entry("args", args_dir);

        let (abort_handle, abort_registration) = AbortHandle::new_pair();
        let future = Abortable::new(runtime_controlled, abort_registration);
        fasync::spawn(async move {
            let _ = future.await;
        });

        RuntimeDirectory { controller: runtime_controller, abort_handle }
    }

    async fn create_elf_directory<'a>(
        &'a self,
        runtime_dir: &'a RuntimeDirectory,
        resolved_url: &'a String,
        process_id: u64,
        job_id: u64,
    ) -> Result<(), RunnerError> {
        let mut elf_dir = directory::simple::empty();
        elf_dir
            .add_entry("process_id", { read_only(move || Ok(Vec::from(process_id.to_string()))) })
            .map_err(|_| ElfRunnerError::component_elf_directory_error(resolved_url.clone()))?;
        elf_dir
            .add_entry("job_id", { read_only(move || Ok(Vec::from(job_id.to_string()))) })
            .map_err(|_| ElfRunnerError::component_elf_directory_error(resolved_url.clone()))?;
        runtime_dir
            .controller
            .add_entry_res("elf", elf_dir)
            .await
            .map_err(|_| ElfRunnerError::component_elf_directory_error(resolved_url.clone()))?;
        Ok(())
    }

    async fn load_launch_info<'a>(
        &'a self,
        url: &'a str,
        start_info: fsys::ComponentStartInfo,
        launcher: &'a fproc::LauncherProxy,
    ) -> Result<(Option<RuntimeDirectory>, fproc::LaunchInfo), Error> {
        let bin_path =
            get_program_binary(&start_info).map_err(|e| RunnerError::invalid_args(url, e))?;
        let bin_arg = &[String::from(
            PKG_PATH.join(&bin_path).to_str().ok_or(err_msg("invalid binary path"))?,
        )];
        let args = get_program_args(&start_info)?;

        let name = Path::new(url)
            .file_name()
            .ok_or(err_msg("invalid url"))?
            .to_str()
            .ok_or(err_msg("invalid url"))?;

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
            .ok_or(err_msg("/pkg missing from namespace"))?;

        let lib_proxy = io_util::open_directory(pkg_proxy, &Path::new("lib"), OPEN_RIGHT_READABLE)?;

        // The loader service should only be able to load files from `/pkg/lib`. Giving it a larger
        // scope is potentially a security vulnerability, as it could make it trivial for parts of
        // applications to get handles to things the application author didn't intend.
        library_loader::start(lib_proxy, ll_service_chan);

        let executable_vmo = library_loader::load_vmo(pkg_proxy, &bin_path)
            .await
            .context("error loading executable")?;

        let child_job = job_default().create_child_job()?;

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
                        .map_err(|_| err_msg("into_channel failed"))?
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
    ) -> Result<Option<Box<ElfController>>, RunnerError> {
        let resolved_url =
            get_resolved_url(&start_info).map_err(|e| RunnerError::invalid_args("", e))?;

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
            let controller =
                Box::new(ElfController::new(runtime_dir, Job::from(component_job), server_stream));
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
        // start the component and move any ElfController into a new async
        // execution context. The ElfController lives until the Future from
        // `ElfController.serve()` completes. This future completes when the
        // ElfController is told to stop the component.
        self.start_component(start_info, server_end)
            .await
            .map(|controller_opt| -> Option<()> {
                controller_opt.and_then(|mut controller| {
                    fasync::spawn(async move {
                        let _ = controller.as_mut().serve().await;
                    });
                    Some(())
                })
            })
            .map(|_option_unit| ())
    }
}

/// Holds information about the Elf component that allows the controller to
/// interact with and control the remove component.
struct ElfController {
    // namespace directory for this component, kept just as a reference to
    // keep the namespace alive
    _runtime_dir: RuntimeDirectory,
    // stream via which the component manager will ask the controller to
    // manipulate the component
    request_stream: fsys::ComponentControllerRequestStream,
    // zircon job to kill when this component exits
    job: Job,
}

impl ElfController {
    pub fn new(
        runtime_dir: RuntimeDirectory,
        job: Job,
        requests: fsys::ComponentControllerRequestStream,
    ) -> ElfController {
        // wrap the runtime_dir in an Option so it is explicitly dropped in
        // `kill()`. In most cases this has no practical effect because the
        // ElfController typically is dropped after the `serve()` future
        // returns.
        return ElfController { _runtime_dir: runtime_dir, job, request_stream: requests };
    }

    /// Serve the request stream held by this ElfController. The job holding
    /// the component controlled by this object will be terminated before the
    /// future completes.
    pub async fn serve(&mut self) -> Result<(), Error> {
        while let Ok(Some(request)) = self.request_stream.try_next().await {
            match request {
                fsys::ComponentControllerRequest::Stop { control_handle: c } => {
                    // for now, treat a stop the same as a kill because this
                    // is not yet implementing proper behavior to first ask the
                    // remote process to exit
                    // let _ = self.job.kill();
                    // c.shutdown();
                    self.kill(c);
                    break;
                }
                fsys::ComponentControllerRequest::Kill { control_handle: c } => {
                    self.kill(c);
                    // let _ = self.job.kill();
                    // c.shutdown();
                    break;
                }
            }
        }

        Ok(())
    }

    /// Kill the job and shutdown control handle supplied to this function.
    fn kill(&self, control_handle: fsys::ComponentControllerControlHandle) {
        let _ = self.job.kill();
        control_handle.shutdown();
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
        fidl::endpoints::{create_endpoints, ClientEnd, Proxy},
        fuchsia_async as fasync, io_util,
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
        let (_client_endpoint, server_endpoint) =
            create_endpoints::<fsys::ComponentControllerMarker>()
                .expect("could not create component controller endpoints");

        // TODO: This test currently results in a bunch of log spew when this test process exits
        // because this does not stop the component, which means its loader service suddenly goes
        // away. Stop the component when the Runner trait provides a way to do so.
        runner
            .start_async(start_info, server_endpoint)
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
        let (_client_endpoint, server_endpoint) =
            create_endpoints::<fsys::ComponentControllerMarker>()
                .expect("could not create component controller endpoints");

        match runner.start_async(start_info, server_endpoint).await {
            Ok(_) => Err(format_err!("hello_world_fail_test succeeded unexpectedly")),
            Err(_) => Ok(()),
        }
    }

    fn new_args_set(args: Vec<Option<Box<fdata::Value>>>) -> fsys::ComponentStartInfo {
        fsys::ComponentStartInfo {
            program: Some(fdata::Dictionary {
                entries: vec![fdata::Entry {
                    key: "args".to_string(),
                    value: Some(Box::new(fdata::Value::Vec(fdata::Vector { values: args }))),
                }],
            }),
            ns: None,
            outgoing_dir: None,
            runtime_dir: None,
            resolved_url: None,
        }
    }

    #[test]
    fn get_program_args_test() {
        let e: Vec<String> = vec![];

        assert_eq!(
            e,
            get_program_args(&fsys::ComponentStartInfo {
                program: Some(fdata::Dictionary { entries: vec![] }),
                ns: None,
                outgoing_dir: None,
                runtime_dir: None,
                resolved_url: None,
            })
            .unwrap()
        );

        assert_eq!(e, get_program_args(&new_args_set(vec![])).unwrap());

        assert_eq!(
            vec!["a".to_string()],
            get_program_args(&new_args_set(vec![Some(Box::new(fdata::Value::Str(
                "a".to_string()
            )))]))
            .unwrap()
        );

        assert_eq!(
            vec!["a".to_string(), "b".to_string()],
            get_program_args(&new_args_set(vec![
                Some(Box::new(fdata::Value::Str("a".to_string()))),
                Some(Box::new(fdata::Value::Str("b".to_string()))),
            ]))
            .unwrap()
        );

        assert_eq!(
            format!("{:?}", err_msg("invalid type in arguments")),
            format!(
                "{:?}",
                get_program_args(&new_args_set(vec![
                    Some(Box::new(fdata::Value::Str("a".to_string()))),
                    None,
                ]))
                .unwrap_err()
            )
        );

        assert_eq!(
            format!("{:?}", err_msg("invalid type in arguments")),
            format!(
                "{:?}",
                get_program_args(&new_args_set(vec![Some(Box::new(fdata::Value::Inum(1))),]))
                    .unwrap_err()
            )
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_kill_component() -> Result<(), Error> {
        let (runtime_dir_client, runtime_dir_server) = zx::Channel::create()?;
        let start_info = hello_world_startinfo(Some(ServerEnd::new(runtime_dir_server)));

        let _runtime_dir_proxy = DirectoryProxy::from_channel(
            fasync::Channel::from_channel(runtime_dir_client).unwrap(),
        );

        let args = Arguments {
            use_builtin_process_launcher: should_use_builtin_process_launcher(),
            ..Default::default()
        };
        let launcher_connector = ProcessLauncherConnector::new(&args);
        let runner = ElfRunner::new(launcher_connector);
        let (client_endpoint, server_endpoint) =
            create_endpoints::<fsys::ComponentControllerMarker>()
                .expect("could not create component controller endpoints");

        let mut controller = runner
            .start_component(start_info, server_endpoint)
            .await
            .expect("hello_world start failed")
            .unwrap();

        let job_copy = Job::from(
            controller
                .job
                .as_handle_ref()
                .duplicate(zx::Rights::SAME_RIGHTS)
                .expect("job handle duplication failed"),
        );

        fasync::spawn(async move {
            let _ = controller.as_mut().serve().await;
        });

        let job_info = job_copy.info()?;
        if job_info.exited {
            return Err(format_err!("job exited unexpectedly."));
        }

        client_endpoint
            .into_proxy()
            .expect("conversion to proxy failed.")
            .stop()
            .expect("FIDL error returned from stop request to controller");

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

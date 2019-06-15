// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod library_loader;

use {
    crate::{
        constants::PKG_PATH,
        model::{Runner, RunnerError},
        startup::{Arguments, BuiltinRootServices},
    },
    cm_rust::data::DictionaryExt,
    failure::{err_msg, format_err, Error, ResultExt},
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
    fuchsia_zircon::{self as zx, HandleBased},
    futures::{
        future::{AbortHandle, Abortable, FutureObj},
        lock::Mutex,
    },
    rand::Rng,
    std::{collections::HashMap, iter, path::PathBuf, sync::Arc},
};

// TODO(fsamuel): We might want to store other things in this struct in the
// future, in which case, RuntimeDirectory might not be an appropriate name.
pub struct RuntimeDirectory {
    /// Called when a component's execution is terminated to drop the 'runtime'
    /// directory.
    abort_handle: AbortHandle,
}

impl Drop for RuntimeDirectory {
    fn drop(&mut self) {
        self.abort_handle.abort();
    }
}

/// Runs components with ELF binaries.
type ExecId = u32;
pub struct ElfRunner {
    launcher_connector: ProcessLauncherConnector,
    /// Each RuntimeDirectory is keyed by a unique execution Id.
    instances: Mutex<HashMap<ExecId, RuntimeDirectory>>,
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
        ElfRunner { launcher_connector, instances: Mutex::new(HashMap::new()) }
    }

    async fn install_runtime_directory<'a>(
        &'a self,
        runtime_dir: ServerEnd<DirectoryMarker>,
        args: &'a Vec<String>,
    ) {
        let mut directory = directory::simple::empty();
        directory.open(
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
        let _ = directory.add_entry("args", args_dir);

        let mut instances = await!(self.instances.lock());
        let (abort_handle, abort_registration) = AbortHandle::new_pair();
        let future = Abortable::new(directory, abort_registration);
        fasync::spawn(async move {
            let _ = await!(future);
        });
        // TODO(fsamuel): This should be keyed off the to-be-implemented
        // ComponentController interface, and not some random number.
        let exec_id = {
            let mut rand_num_generator = rand::thread_rng();
            rand_num_generator.gen::<u32>()
        };
        instances.insert(exec_id, RuntimeDirectory { abort_handle });
    }

    async fn load_launch_info<'a>(
        &'a self,
        url: String,
        start_info: fsys::ComponentStartInfo,
        launcher: &'a fproc::LauncherProxy,
    ) -> Result<fproc::LaunchInfo, Error> {
        let bin_path = get_program_binary(&start_info)
            .map_err(|e| RunnerError::invalid_args(url.as_ref(), e))?;
        let bin_arg = &[String::from(
            PKG_PATH.join(&bin_path).to_str().ok_or(err_msg("invalid binary path"))?,
        )];
        let args = get_program_args(&start_info)?;

        let name = PathBuf::from(url)
            .file_name()
            .ok_or(err_msg("invalid url"))?
            .to_str()
            .ok_or(err_msg("invalid url"))?
            .to_string();

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

        let lib_proxy = io_util::open_directory(pkg_proxy, &PathBuf::from("lib"))?;

        // The loader service should only be able to load files from `/pkg/lib`. Giving it a larger
        // scope is potentially a security vulnerability, as it could make it trivial for parts of
        // applications to get handles to things the application author didn't intend.
        library_loader::start(lib_proxy, ll_service_chan);

        let executable_vmo = await!(library_loader::load_vmo(pkg_proxy, &bin_path))
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

        if let Some(runtime_dir) = start_info.runtime_dir {
            await!(self.install_runtime_directory(runtime_dir, &args));
        }

        Ok(fproc::LaunchInfo { executable: executable_vmo, job: child_job, name: name })
    }

    async fn start_async(&self, start_info: fsys::ComponentStartInfo) -> Result<(), RunnerError> {
        let resolved_url =
            get_resolved_url(&start_info).map_err(|e| RunnerError::invalid_args("", e))?;

        let launcher = self
            .launcher_connector
            .connect()
            .context("failed to connect to launcher service")
            .map_err(|e| RunnerError::component_load_error(resolved_url.as_ref(), e))?;

        // Load the component
        let mut launch_info =
            await!(self.load_launch_info(resolved_url.clone(), start_info, &launcher))
                .map_err(|e| RunnerError::component_load_error(resolved_url.as_ref(), e))?;

        // Launch the component
        await!(async {
            let (status, _process) = await!(launcher.launch(&mut launch_info))?;
            if zx::Status::from_raw(status) != zx::Status::OK {
                return Err(format_err!("failed to launch component: {}", status));
            }
            Ok(())
        })
        .map_err(|e| RunnerError::component_launch_error(resolved_url, e))?;

        Ok(())
    }
}

impl Runner for ElfRunner {
    fn start(&self, start_info: fsys::ComponentStartInfo) -> FutureObj<Result<(), RunnerError>> {
        FutureObj::new(Box::new(self.start_async(start_info)))
    }
}

/// Connects to the appropriate fuchsia.process.Launcher service based on the options provided in
/// [ProcessLauncherConnector::new].
///
/// This exists so that callers can make a new connection to fuchsia.process.Launcher for each use
/// because the service is stateful per connection, so it is not safe to share a connection between
/// multiple asynchronous process launchers.
///
/// If [LibraryOpts::use_builtin_process_launcher] is true, this will connect to the built-in
/// fuchsia.process.Launcher service using the provided BuiltinRootServices. Otherwise, this connects
/// to the launcher service under /svc in component_manager's namespace.
pub struct ProcessLauncherConnector {
    // If Some(_), connect to the built-in service. Otherwise connect to a launcher from the
    // namespace.
    builtin: Option<Arc<BuiltinRootServices>>,
}

impl ProcessLauncherConnector {
    pub fn new(args: &Arguments, builtin: Arc<BuiltinRootServices>) -> ProcessLauncherConnector {
        let builtin = match args.use_builtin_process_launcher {
            true => Some(builtin),
            false => None,
        };
        ProcessLauncherConnector { builtin }
    }

    pub fn connect(&self) -> Result<fproc::LauncherProxy, Error> {
        let proxy = match &self.builtin {
            Some(builtin) => builtin
                .connect_to_service::<fproc::LauncherMarker>()
                .context("failed to connect to builtin launcher service")?,
            None => client::connect_to_service::<fproc::LauncherMarker>()
                .context("failed to connect to external launcher service")?,
        };
        Ok(proxy)
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl::endpoints::{ClientEnd, Proxy},
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
        let pkg_chan = io_util::open_directory_in_namespace("/pkg")
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
            io_util::open_file(&root_proxy, &PathBuf::from(path)).expect("Failed to open file.");
        let res = await!(io_util::read_file(&file_proxy));
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
        let builtin_services = Arc::new(BuiltinRootServices::new(&args)?);
        let launcher_connector = ProcessLauncherConnector::new(&args, builtin_services);
        let runner = ElfRunner::new(launcher_connector);

        // TODO: This test currently results in a bunch of log spew when this test process exits
        // because this does not stop the component, which means its loader service suddenly goes
        // away. Stop the component when the Runner trait provides a way to do so.
        await!(runner.start_async(start_info)).expect("hello_world_test start failed");

        // Verify that args are added to the runtime directory.
        assert_eq!("foo", await!(read_file(&runtime_dir_proxy, "args/0")));
        assert_eq!("bar", await!(read_file(&runtime_dir_proxy, "args/1")));
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
        let builtin_services = Arc::new(BuiltinRootServices::new(&args)?);
        let launcher_connector = ProcessLauncherConnector::new(&args, builtin_services);
        let runner = ElfRunner::new(launcher_connector);
        await!(runner.start_async(start_info))
            .expect_err("hello_world_fail_test succeeded unexpectedly");
        Ok(())
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
}

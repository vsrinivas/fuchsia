// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context as _, Error},
    by_addr::ByAddr,
    fdio::fdio_sys,
    fidl::endpoints::{ClientEnd, ServerEnd, ServiceMarker},
    fidl_fuchsia_io::{DirectoryProxy, OPEN_RIGHT_READABLE},
    fidl_fuchsia_process as fproc,
    fidl_fuchsia_sys::{
        ComponentControllerMarker, RunnerMarker, RunnerRequest, RunnerRequestStream,
    },
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_runtime::{job_default, HandleInfo, HandleType},
    fuchsia_syslog::{fx_log_err, fx_log_info},
    fuchsia_zircon as zx,
    futures::prelude::*,
    lazy_static::lazy_static,
    library_loader,
    std::{
        collections::HashSet,
        path::{Path, PathBuf},
        sync::{Arc, Mutex},
    },
    zx::HandleBased,
};

lazy_static! {
    pub static ref PKG_PATH: PathBuf = PathBuf::from("/pkg");
}

fn get_program_binary(startup_info: &fidl_fuchsia_sys::StartupInfo) -> Result<String, Error> {
    if let Some(metadata) = &startup_info.program_metadata {
        let m = metadata
            .iter()
            .find(|m| m.key == "binary")
            .ok_or(format_err!("\"binary\" must be specified"))?;
        return Ok(m.value.clone());
    }
    Err(format_err!("\"binary\" must be specified"))
}

fn get_program_args(startup_info: &fidl_fuchsia_sys::StartupInfo) -> Result<Vec<String>, Error> {
    if let Some(args) = &startup_info.launch_info.arguments {
        return Ok(args.clone());
    }
    Ok(vec![])
}

fn handle_info_from_fd(fd: i32) -> Result<Option<fproc::HandleInfo>, Error> {
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

/// Representation of Test Component which will implement 'fuchsia.test.Suite', launch gtest
/// process and map the output into our protocol and pass results back to test executor.
struct Component {
    /// Package definition of our test component.
    package: fidl_fuchsia_sys::Package,

    /// Startup info for this component pased to runner.
    startup_info: Option<fidl_fuchsia_sys::StartupInfo>,

    // TODO(anmittal): implement it. Currently keeping it so that it doesn't die.
    /// fuchsia.sys.Controller channel
    _controller: Option<ServerEnd<ComponentControllerMarker>>,

    /// True if this component is already running, i.e process was launched.
    running: bool,
}

impl Component {
    /// Create a new Component object
    pub fn new(
        package: fidl_fuchsia_sys::Package,
        startup_info: fidl_fuchsia_sys::StartupInfo,
        controller: Option<ServerEnd<ComponentControllerMarker>>,
    ) -> Self {
        // TODO(anmittal): clone things in startup_info so that we can launch test again and again.
        Component {
            package: package,
            startup_info: Some(startup_info),
            _controller: controller,
            running: false,
        }
    }

    // TODO(anmittal): Explore how we can share code and abstract most part into a library.
    async fn load_launch_info(
        &mut self,
        launcher: &fproc::LauncherProxy,
    ) -> Result<fproc::LaunchInfo, Error> {
        let startup_info = self.startup_info.take().unwrap();
        let bin_path = get_program_binary(&startup_info).map_err(|e| {
            format_err!("invalid metadata for: {}, {}", self.package.resolved_url, e)
        })?;

        let bin_arg = &[String::from(
            PKG_PATH.join(&bin_path).to_str().ok_or(format_err!("invalid binary path"))?,
        )];

        let args = get_program_args(&startup_info)?;

        let name = Path::new(&self.package.resolved_url)
            .file_name()
            .ok_or(format_err!("invalid url"))?
            .to_str()
            .ok_or(format_err!("invalid url"))?;

        // Convert the directories into proxies, so we can find "/pkg" and open "lib" and bin_path
        let ns = startup_info.flat_namespace;
        let mut paths = ns.paths;
        let directories: Result<Vec<DirectoryProxy>, std::io::Error> = ns
            .directories
            .into_iter()
            .map(|d| {
                let chan = fasync::Channel::from_channel(d)?;
                Ok(DirectoryProxy::new(chan))
            })
            .collect();
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

        let child_job_dup = child_job.duplicate_handle(zx::Rights::SAME_RIGHTS)?;

        let mut string_iters: Vec<_> =
            bin_arg.iter().chain(args.iter()).map(|s| s.bytes()).collect();
        launcher.add_args(
            &mut string_iters.iter_mut().map(|iter| iter as &mut dyn ExactSizeIterator<Item = u8>),
        )?;

        // TODO(anmittal): capture these fds and read them to implement fuchsia.test.Suite
        // currently implementing stdout so that we can debug till we implement fuchsia.test.Suite
        // and put a reader on stdout.
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

        // TODO(anmittal): Implement fuchsia.test.Suite instead of passing it to test process.
        if let Some(outgoing_dir) = startup_info.launch_info.directory_request {
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

        Ok(fproc::LaunchInfo { executable: executable_vmo, job: child_job, name: name.to_owned() })
    }

    // TODO(anmittal): Call list on gtest and keep test list
    pub async fn start(&mut self) -> Result<(), Error> {
        if !self.running {
            self.running = true;
        } else {
            panic!("run called two times");
        }
        fx_log_info!("Received runner request for component {}", self.package.resolved_url);

        let launcher = fuchsia_component::client::connect_to_service::<fproc::LauncherMarker>()
            .expect("cannot connect to Launcher");

        let mut launch_info = self.load_launch_info(&launcher).await?;

        // TODO(anmittal): Wait on process to terminate.
        let (status, _process) = launcher.launch(&mut launch_info).await?;
        if zx::Status::from_raw(status) != zx::Status::OK {
            return Err(format_err!("failed to launch component: {}", status));
        }

        Ok(())
    }
}

struct State {
    /// safe keep map of components to prevent them from dying.
    pub components: HashSet<ByAddr<Component>>,
}

impl State {
    pub fn new() -> Self {
        State { components: HashSet::new() }
    }
}

async fn run_runner_server(
    mut stream: RunnerRequestStream,
    state: Arc<Mutex<State>>,
) -> Result<(), Error> {
    while let Some(RunnerRequest::StartComponent {
        package,
        startup_info,
        controller,
        control_handle: _,
    }) = stream.try_next().await.context("error running server")?
    {
        let mut component = Component::new(package, startup_info, controller);
        component.start().await?;

        // TODO(anmittal): figure out when to kill this component and prevent leak.
        let mut state = state.lock().unwrap();
        state.components.insert(ByAddr::new(Arc::new(component)));
    }
    Ok(())
}

fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["gtest_v1_runner"])?;
    fx_log_info!("runner started");

    let mut executor = fasync::Executor::new().context("Error creating executor")?;
    let mut fs = ServiceFs::new_local();
    let state = Arc::new(Mutex::new(State::new()));
    fs.dir("svc").add_fidl_service_at(RunnerMarker::NAME, move |stream| {
        let state = state.clone();
        fasync::spawn(
            async move {
                run_runner_server(stream, state).await?;
                Ok(())
            }
            .unwrap_or_else(|e: anyhow::Error| fx_log_err!("runner failed: {:?}", e)),
        );
    });
    fs.take_and_serve_directory_handle()?;
    executor.run_singlethreaded(fs.collect::<()>());

    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    // TODO(anmittal): Add a unit test to launch a process.

    fn default_startup_info() -> fidl_fuchsia_sys::StartupInfo {
        fidl_fuchsia_sys::StartupInfo {
            launch_info: fidl_fuchsia_sys::LaunchInfo {
                url: "".to_string(),
                arguments: None,
                out: None,
                err: None,
                directory_request: None,
                flat_namespace: None,
                additional_services: None,
            },
            flat_namespace: fidl_fuchsia_sys::FlatNamespace { paths: vec![], directories: vec![] },
            program_metadata: Some(vec![]),
        }
    }

    #[test]
    fn get_program_args_test() {
        let mut e: Vec<String> = vec![];

        let mut startup_info = default_startup_info();
        // no arguments
        assert_eq!(e, get_program_args(&startup_info).unwrap());

        // empty arguments
        startup_info.launch_info.arguments = Some(vec![]);
        assert_eq!(e, get_program_args(&startup_info).unwrap());

        // one argument
        e = vec!["a1".to_string()];
        startup_info.launch_info.arguments = Some(e.clone());
        assert_eq!(e, get_program_args(&startup_info).unwrap());

        // two argument
        e = vec!["a1".to_string(), "a2".to_string()];
        startup_info.launch_info.arguments = Some(e.clone());
        assert_eq!(e, get_program_args(&startup_info).unwrap());

        // more than two argument
        e = vec!["a1".to_string(), "a2".to_string(), "random".to_string(), "random2".to_string()];
        startup_info.launch_info.arguments = Some(e.clone());
        assert_eq!(e, get_program_args(&startup_info).unwrap());
    }

    #[test]
    fn get_program_binary_test() {
        let mut startup_info = default_startup_info();
        // no program metadata
        assert!(get_program_binary(&startup_info).is_err());

        // blank program metadata
        startup_info.program_metadata = Some(vec![fidl_fuchsia_sys::ProgramMetadata {
            key: "".to_string(),
            value: "".to_string(),
        }]);
        assert!(get_program_binary(&startup_info).is_err());

        // no binary program metadata
        startup_info.program_metadata = Some(vec![fidl_fuchsia_sys::ProgramMetadata {
            key: "data".to_string(),
            value: "sample_data".to_string(),
        }]);
        assert!(get_program_binary(&startup_info).is_err());

        // no binary program metadata with multiple metadata
        startup_info.program_metadata = Some(vec![
            fidl_fuchsia_sys::ProgramMetadata {
                key: "data".to_string(),
                value: "sample_data".to_string(),
            },
            fidl_fuchsia_sys::ProgramMetadata {
                key: "arg".to_string(),
                value: "hello".to_string(),
            },
        ]);
        assert!(get_program_binary(&startup_info).is_err());

        // binary program metadata with one metadata
        startup_info.program_metadata = Some(vec![fidl_fuchsia_sys::ProgramMetadata {
            key: "binary".to_string(),
            value: "/app".to_string(),
        }]);
        assert_eq!("/app", get_program_binary(&startup_info).unwrap());

        // binary program metadata with multiple metadata
        startup_info.program_metadata = Some(vec![
            fidl_fuchsia_sys::ProgramMetadata {
                key: "binary".to_string(),
                value: "/app".to_string(),
            },
            fidl_fuchsia_sys::ProgramMetadata {
                key: "data".to_string(),
                value: "sample_data".to_string(),
            },
        ]);
        assert_eq!("/app", get_program_binary(&startup_info).unwrap());

        // binary program metadata with multiple metadata
        startup_info.program_metadata = Some(vec![
            fidl_fuchsia_sys::ProgramMetadata {
                key: "data".to_string(),
                value: "sample_data".to_string(),
            },
            fidl_fuchsia_sys::ProgramMetadata {
                key: "binary".to_string(),
                value: "/app".to_string(),
            },
        ]);
        assert_eq!("/app", get_program_binary(&startup_info).unwrap());
    }
}

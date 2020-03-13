// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    async_trait::async_trait,
    fidl::endpoints::ClientEnd,
    fidl_fuchsia_component_runner as fcrunner, fidl_fuchsia_io as fio,
    fidl_fuchsia_process as fproc, fuchsia_async as fasync,
    fuchsia_runtime::{job_default, HandleInfo, HandleType},
    fuchsia_zircon::{self as zx, HandleBased},
    futures::{future::BoxFuture, prelude::*},
    lazy_static::lazy_static,
    library_loader,
    std::convert::TryFrom,
    std::path::Path,
    std::path::PathBuf,
    thiserror::Error,
};

lazy_static! {
    pub static ref PKG_PATH: PathBuf = PathBuf::from("/pkg");
}

/// Object implementing this type can be killed by calling kill function.
#[async_trait]
pub trait Controllable {
    /// Should kill self and do cleanup.
    /// Should not return error or panic, should log error instead.
    async fn kill(mut self);

    /// Stop the component. Once the component is stopped, the
    /// ComponentControllerControlHandle should be closed. If the component is
    /// not stopped quickly enough, kill will be called. The amount of time
    /// `stop` is allowed may vary based on a variety of factors.
    fn stop<'a>(&mut self) -> BoxFuture<'a, ()>;
}

/// Holds information about the component that allows the controller to
/// interact with and control the component.
pub struct Controller<C: Controllable> {
    /// stream via which the component manager will ask the controller to
    /// manipulate the component
    request_stream: fcrunner::ComponentControllerRequestStream,

    /// Controllable object which controls the underlying component.
    /// This would be None once the object is killed.
    controllable: Option<C>,
}

impl<C: Controllable> Controller<C> {
    /// Creates new instance
    pub fn new(
        controllable: C,
        requests: fcrunner::ComponentControllerRequestStream,
    ) -> Controller<C> {
        Controller { controllable: Some(controllable), request_stream: requests }
    }

    /// Serve the request stream held by this Controller.
    pub async fn serve(mut self) -> Result<(), Error> {
        while let Ok(Some(request)) = self.request_stream.try_next().await {
            match request {
                fcrunner::ComponentControllerRequest::Stop { control_handle: c } => {
                    // Since stop takes some period of time to complete, call
                    // it in a future and then shut down the control handle
                    // which will knock `serve` out of this loop.
                    let stop_func = self.stop().await;
                    fasync::spawn(async move {
                        stop_func.await;
                        c.shutdown();
                    });
                }
                fcrunner::ComponentControllerRequest::Kill { control_handle: c } => {
                    self.kill().await;
                    c.shutdown();
                    break;
                }
            }
        }

        Ok(())
    }

    /// Kill the job and shutdown control handle supplied to this function.
    async fn kill(&mut self) {
        if let Some(controllable) = self.controllable.take() {
            controllable.kill().await;
        }
    }

    /// If we have a Controllable, ask it to stop the component.
    async fn stop<'a>(&mut self) -> BoxFuture<'a, ()> {
        if self.controllable.is_some() {
            self.controllable.as_mut().unwrap().stop()
        } else {
            async {}.boxed()
        }
    }
}

/// An error encountered trying convert Vec<fcrunner::ComponentNamespaceEntry>
#[derive(Debug, Error)]
pub enum ComponentNamespaceError {
    #[error("cannot clone directory for cloning namespace: {:?}", _0)]
    DirectoryClone(anyhow::Error),

    #[error("cannot convert directory handle to proxy: {}.", _0)]
    IntoProxy(fidl::Error),

    #[error("missing path in namespace entry")]
    MissingPath,
}

/// This represents Component namespace which is easier for other functions in this library to read
/// and operate on.
#[derive(Default)]
pub struct ComponentNamespace {
    /// Pair representing path and directory proxy.
    items: Vec<(String, fio::DirectoryProxy)>,
}

impl TryFrom<Vec<fcrunner::ComponentNamespaceEntry>> for ComponentNamespace {
    type Error = ComponentNamespaceError;

    fn try_from(mut ns: Vec<fcrunner::ComponentNamespaceEntry>) -> Result<Self, Self::Error> {
        let mut new_ns = Self { items: Vec::with_capacity(ns.len()) };

        while let Some(entry) = ns.pop() {
            let path = entry.path.ok_or(ComponentNamespaceError::MissingPath)?;
            if let Some(dir) = entry.directory {
                new_ns
                    .items
                    .push((path, dir.into_proxy().map_err(ComponentNamespaceError::IntoProxy)?));
            }
        }

        Ok(new_ns)
    }
}

impl ComponentNamespace {
    pub fn clone(&self) -> Result<Self, ComponentNamespaceError> {
        let mut ns = Self { items: Vec::with_capacity(self.items.len()) };
        for (path, proxy) in &self.items {
            let client_proxy = io_util::clone_directory(proxy, fio::CLONE_FLAG_SAME_RIGHTS)
                .map_err(ComponentNamespaceError::DirectoryClone)?;
            ns.items.push((path.clone(), client_proxy));
        }
        Ok(ns)
    }
}

/// An error encountered trying to launch a component.
#[derive(Debug, PartialEq, Eq, Error)]
pub enum LaunchError {
    #[error("invalid binary path {}", _0)]
    InvalidBinaryPath(String),

    #[error("/pkg missing in the namespace")]
    MissingPkg,

    #[error("error loading executable: {:?}", _0)]
    LoadingExecutable(String),

    #[error("cannot convert proxy to channel")]
    DirectoryToChannel,

    #[error("cannot create channels: {}", _0)]
    ChannelCreation(fuchsia_zircon_status::Status),

    #[error("error loading 'lib' in /pkg: {:?}", _0)]
    LibLoadError(String),

    #[error("cannot create job: {}", _0)]
    JobCreation(fuchsia_zircon_status::Status),

    #[error("cannot duplicate job: {}", _0)]
    DuplicateJob(fuchsia_zircon_status::Status),

    #[error("cannot add args to launcher: {:?}", _0)]
    AddArgs(String),

    #[error("cannot add args to launcher: {:?}", _0)]
    AddHandles(String),

    #[error("cannot add args to launcher: {:?}", _0)]
    AddNames(String),

    #[error("cannot add env to launcher: {:?}", _0)]
    AddEnvirons(String),
}

/// Arguments to `configure_launcher` function.
pub struct LauncherConfigArgs<'a> {
    /// relative binary path to /pkg in `ns`.
    pub bin_path: &'a str,

    /// Name of the binary to add to `LaunchInfo`.
    pub name: &'a str,

    ///  Arguments to binary.
    pub args: Option<Vec<String>>,

    /// Namespace for binary process to be launched.
    pub ns: ComponentNamespace,

    /// Job in which process is launched. If None, a child job would be created in default one.
    pub job: Option<zx::Job>,

    /// Extra handle infos to add. This function all ready adds handles for default job and svc
    /// loader.
    pub handle_infos: Option<Vec<fproc::HandleInfo>>,

    /// Extra names to add to namespace. by default only names from `ns` are added.
    pub name_infos: Option<Vec<fproc::NameInfo>>,

    /// Process environment to add to launcher.
    pub environs: Option<Vec<String>>,

    /// proxy for `fuchsia.proc.Launcher`.
    pub launcher: &'a fproc::LauncherProxy,
}

/// Configures launcher to launch process using passed params and creates launch info.
/// This starts a library loader service, that will live as long as the handle for it given to the
/// launcher is alive.
pub async fn configure_launcher(
    config_args: LauncherConfigArgs<'_>,
) -> Result<fproc::LaunchInfo, LaunchError> {
    // Locate the '/pkg' directory proxy previously added to the new component's namespace.
    let pkg_str = PKG_PATH.to_str().unwrap();
    let (_, pkg_proxy) = config_args
        .ns
        .items
        .iter()
        .find(|(p, _)| p.as_str() == pkg_str)
        .ok_or(LaunchError::MissingPkg)?;

    // library_loader provides a helper function that we use to load the main executable from the
    // package directory as a VMO in the same way that dynamic libraries are loaded. Doing this
    // first allows launching to fail quickly and clearly in case the main executable can't be
    // loaded with ZX_RIGHT_EXECUTE from the package directory.
    let executable_vmo = library_loader::load_vmo(pkg_proxy, &config_args.bin_path)
        .await
        .map_err(|e| LaunchError::LoadingExecutable(e.to_string()))?;

    // The loader service should only be able to load files from `/pkg/lib`. Giving it a larger
    // scope is potentially a security vulnerability, as it could make it trivial for parts of
    // applications to get handles to things the application author didn't intend.
    let lib_proxy = io_util::open_directory(
        pkg_proxy,
        &Path::new("lib"),
        fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_EXECUTABLE,
    )
    .map_err(|e| LaunchError::LibLoadError(e.to_string()))?;
    let (ll_client_chan, ll_service_chan) =
        zx::Channel::create().map_err(LaunchError::ChannelCreation)?;
    library_loader::start(lib_proxy, ll_service_chan);

    // Get the provided job to create the new process in, if one was provided, or else create a new
    // child job of this process's (this process that this code is running in) own 'default job'.
    let job = config_args
        .job
        .unwrap_or(job_default().create_child_job().map_err(LaunchError::JobCreation)?);

    // Build the command line args for the new process and send them to the launcher.
    let bin_arg = &[String::from(
        PKG_PATH
            .join(&config_args.bin_path)
            .to_str()
            .ok_or(LaunchError::InvalidBinaryPath(config_args.bin_path.to_string()))?,
    )];
    let args = config_args.args.unwrap_or(vec![]);
    let string_iters: Vec<_> = bin_arg.iter().chain(args.iter()).map(|s| s.as_bytes()).collect();
    config_args
        .launcher
        .add_args(&mut string_iters.into_iter())
        .map_err(|e| LaunchError::AddArgs(e.to_string()))?;

    // Get any initial handles to provide to the new process, if any were provided by the caller.
    // Add handles for the new process's default job (by convention, this is the same job that the
    // new process is launched in) and the fuchsia.ldsvc.Loader service created above, then send to
    // the launcher.
    let job_dup =
        job.duplicate_handle(zx::Rights::SAME_RIGHTS).map_err(LaunchError::DuplicateJob)?;
    let mut handle_infos = config_args.handle_infos.unwrap_or(vec![]);
    handle_infos.append(&mut vec![
        fproc::HandleInfo {
            handle: ll_client_chan.into_handle(),
            id: HandleInfo::new(HandleType::LdsvcLoader, 0).as_raw(),
        },
        fproc::HandleInfo {
            handle: job_dup.into_handle(),
            id: HandleInfo::new(HandleType::DefaultJob, 0).as_raw(),
        },
    ]);
    config_args
        .launcher
        .add_handles(&mut handle_infos.iter_mut())
        .map_err(|e| LaunchError::AddHandles(e.to_string()))?;

    // Send environment variables for the new process, if any, to the launcher.
    let environs: Vec<_> = config_args.environs.unwrap_or(vec![]);
    if environs.len() > 0 {
        let environs_iters: Vec<_> = environs.iter().map(|s| s.as_bytes()).collect();
        config_args
            .launcher
            .add_environs(&mut environs_iters.into_iter())
            .map_err(|e| LaunchError::AddEnvirons(e.to_string()))?;
    }

    // Combine any manually provided namespace entries with the provided ComponentNamespace, and
    // then send the new process's namespace to the launcher.
    let mut name_infos = config_args.name_infos.unwrap_or(vec![]);
    for (path, directory) in config_args.ns.items {
        let directory = ClientEnd::new(
            directory
                .into_channel()
                .map_err(|_| LaunchError::DirectoryToChannel)?
                .into_zx_channel(),
        );
        name_infos.push(fproc::NameInfo { path, directory });
    }
    config_args
        .launcher
        .add_names(&mut name_infos.iter_mut())
        .map_err(|e| LaunchError::AddNames(e.to_string()))?;

    Ok(fproc::LaunchInfo {
        executable: executable_vmo,
        job: job,
        name: config_args.name.to_owned(),
    })
}

#[cfg(test)]
mod tests {
    use {
        super::*, anyhow::Context, fidl::endpoints::create_endpoints,
        fidl::endpoints::create_proxy, fidl_fuchsia_component_runner as fcrunner,
        fuchsia_async as fasync,
    };

    struct FakeComponent<K>
    where
        K: FnOnce() + std::marker::Send,
    {
        pub onkill: Option<K>,

        pub onstop: Option<K>,
    }

    #[async_trait]
    impl<K: 'static> Controllable for FakeComponent<K>
    where
        K: FnOnce() + std::marker::Send,
    {
        async fn kill(mut self) {
            let func = self.onkill.take().unwrap();
            func();
        }

        fn stop<'a>(&mut self) -> BoxFuture<'a, ()> {
            let func = self.onstop.take().unwrap();
            async move { func() }.boxed()
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_kill_component() -> Result<(), Error> {
        let (sender, recv) = futures::channel::oneshot::channel::<()>();
        let fake_component = FakeComponent {
            onkill: Some(move || {
                sender.send(()).unwrap();
            }),
            onstop: None,
        };

        let (client_endpoint, server_endpoint) =
            create_endpoints::<fcrunner::ComponentControllerMarker>()
                .expect("could not create component controller endpoints");

        let controller_stream =
            server_endpoint.into_stream().context("failed to convert server end to controller")?;

        let controller = Controller::new(fake_component, controller_stream);

        client_endpoint
            .into_proxy()
            .expect("conversion to proxy failed.")
            .kill()
            .expect("FIDL error returned from kill request to controller");

        // this should return after kill call
        controller.serve().await.expect("should not fail");

        // this means kill was called
        recv.await?;

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_stop_component() -> Result<(), Error> {
        let (sender, recv) = futures::channel::oneshot::channel::<()>();
        let fake_component = FakeComponent {
            onstop: Some(move || {
                sender.send(()).unwrap();
            }),
            onkill: None,
        };

        let (client_endpoint, server_endpoint) =
            create_endpoints::<fcrunner::ComponentControllerMarker>()
                .expect("could not create component controller endpoints");

        let controller_stream =
            server_endpoint.into_stream().context("failed to convert server end to controller")?;

        let controller = Controller::new(fake_component, controller_stream);

        client_endpoint
            .into_proxy()
            .expect("conversion to proxy failed.")
            .stop()
            .expect("FIDL error returned from kill request to controller");

        // this should return after stop call for now.
        controller.serve().await.expect("should not fail");

        // this means stop was called
        recv.await?;

        Ok(())
    }

    mod launch_info {

        use {super::*, anyhow::format_err, futures::channel::oneshot, std::mem::drop};

        fn setup_empty_namespace() -> Result<ComponentNamespace, ComponentNamespaceError> {
            setup_namespace(false, vec![])
        }

        fn setup_namespace(
            include_pkg: bool,
            // All the handles created for this will have server end closed.
            // Clients cannot send messages on those handles in ns.
            extra_paths: Vec<&str>,
        ) -> Result<ComponentNamespace, ComponentNamespaceError> {
            let mut ns = Vec::<fcrunner::ComponentNamespaceEntry>::new();
            if include_pkg {
                let pkg_path = "/pkg".to_string();
                let pkg_chan =
                    io_util::open_directory_in_namespace("/pkg", fio::OPEN_RIGHT_READABLE)
                        .unwrap()
                        .into_channel()
                        .unwrap()
                        .into_zx_channel();
                let pkg_handle = ClientEnd::new(pkg_chan);

                ns.push(fcrunner::ComponentNamespaceEntry {
                    path: Some(pkg_path),
                    directory: Some(pkg_handle),
                });
            }

            for path in extra_paths {
                let (client, _server) = create_endpoints::<fio::DirectoryMarker>()
                    .expect("could not create component controller endpoints");
                ns.push(fcrunner::ComponentNamespaceEntry {
                    path: Some(path.to_string()),
                    directory: Some(client),
                });
            }
            ComponentNamespace::try_from(ns)
        }

        #[derive(Default)]
        struct FakeLauncherServiceResults {
            names: Vec<String>,
            handles: Vec<u32>,
            args: Vec<String>,
        }

        fn start_launcher(
        ) -> Result<(fproc::LauncherProxy, oneshot::Receiver<FakeLauncherServiceResults>), Error>
        {
            let (launcher_proxy, server_end) = create_proxy::<fproc::LauncherMarker>()?;
            let (sender, receiver) = oneshot::channel();
            fasync::spawn_local(async move {
                let stream = server_end.into_stream().expect("error making stream");
                run_launcher_service(stream, sender)
                    .await
                    .expect("error running fake launcher service");
            });
            Ok((launcher_proxy, receiver))
        }

        async fn run_launcher_service(
            mut stream: fproc::LauncherRequestStream,
            sender: oneshot::Sender<FakeLauncherServiceResults>,
        ) -> Result<(), Error> {
            let mut res = FakeLauncherServiceResults::default();
            while let Some(event) = stream.try_next().await? {
                match event {
                    fproc::LauncherRequest::AddArgs { args, .. } => {
                        res.args.extend(
                            args.into_iter()
                                .map(|a| {
                                    std::str::from_utf8(&a)
                                        .expect("cannot convert bytes to utf8 string")
                                        .to_owned()
                                })
                                .collect::<Vec<String>>(),
                        );
                    }
                    fproc::LauncherRequest::AddEnvirons { .. } => {}
                    fproc::LauncherRequest::AddNames { names, .. } => {
                        res.names
                            .extend(names.into_iter().map(|m| m.path).collect::<Vec<String>>());
                    }
                    fproc::LauncherRequest::AddHandles { handles, .. } => {
                        res.handles.extend(handles.into_iter().map(|m| m.id).collect::<Vec<u32>>());
                    }
                    fproc::LauncherRequest::CreateWithoutStarting { .. } => {}
                    fproc::LauncherRequest::Launch { .. } => {}
                }
            }
            sender.send(res).map_err(|_e| format_err!("can't send result"))?;
            Ok(())
        }

        #[fasync::run_singlethreaded(test)]
        async fn missing_pkg() -> Result<(), Error> {
            let (launcher_proxy, _server_end) = create_proxy::<fproc::LauncherMarker>()?;
            let ns = setup_empty_namespace()?;

            assert_eq!(
                configure_launcher(LauncherConfigArgs {
                    bin_path: "bin/path",
                    name: "name",
                    args: None,
                    ns: ns,
                    job: None,
                    handle_infos: None,
                    name_infos: None,
                    environs: None,
                    launcher: &launcher_proxy,
                })
                .await,
                Err(LaunchError::MissingPkg),
            );

            drop(_server_end);
            Ok(())
        }

        #[fasync::run_singlethreaded(test)]
        async fn invalid_executable() -> Result<(), Error> {
            let (launcher_proxy, _server_end) = create_proxy::<fproc::LauncherMarker>()?;
            let ns = setup_namespace(true, vec![])?;

            match configure_launcher(LauncherConfigArgs {
                bin_path: "test/path",
                name: "name",
                args: None,
                ns: ns,
                job: None,
                handle_infos: None,
                name_infos: None,
                environs: None,
                launcher: &launcher_proxy,
            })
            .await
            .expect_err("should error out")
            {
                LaunchError::LoadingExecutable(_) => {}
                e => panic!("Expected LoadingExecutable error, got {:?}", e),
            }
            Ok(())
        }

        #[fasync::run_singlethreaded(test)]
        async fn invalid_pkg() -> Result<(), Error> {
            let (launcher_proxy, _server_end) = create_proxy::<fproc::LauncherMarker>()?;
            let ns = setup_namespace(false, vec!["/pkg"])?;

            assert_eq!(
                configure_launcher(LauncherConfigArgs {
                    bin_path: "bin/path",
                    name: "name",
                    args: None,
                    ns: ns,
                    job: None,
                    handle_infos: None,
                    name_infos: None,
                    environs: None,
                    launcher: &launcher_proxy,
                })
                .await,
                Err(LaunchError::LoadingExecutable(
                    "A FIDL client encountered an IO error writing a \
                 request into a channel: PEER_CLOSED"
                        .to_owned()
                )),
            );
            Ok(())
        }

        #[fasync::run_singlethreaded(test)]
        async fn default_args() -> Result<(), Error> {
            let (launcher_proxy, recv) = start_launcher()?;

            let ns = setup_namespace(true, vec![])?;

            let _launch_info = configure_launcher(LauncherConfigArgs {
                bin_path: "test/runner_tests",
                name: "name",
                args: None,
                ns: ns,
                job: None,
                handle_infos: None,
                name_infos: None,
                environs: None,
                launcher: &launcher_proxy,
            })
            .await?;

            drop(launcher_proxy);

            let ls = recv.await?;

            assert_eq!(ls.args, vec!("/pkg/test/runner_tests".to_owned()));

            Ok(())
        }

        #[fasync::run_singlethreaded(test)]
        async fn extra_args() -> Result<(), Error> {
            let (launcher_proxy, recv) = start_launcher()?;

            let ns = setup_namespace(true, vec![])?;

            let args = vec!["args1".to_owned(), "arg2".to_owned()];

            let _launch_info = configure_launcher(LauncherConfigArgs {
                bin_path: "test/runner_tests",
                name: "name",
                args: Some(args.clone()),
                ns: ns,
                job: None,
                handle_infos: None,
                name_infos: None,
                environs: None,
                launcher: &launcher_proxy,
            })
            .await?;

            drop(launcher_proxy);

            let ls = recv.await?;

            let mut expected = vec!["/pkg/test/runner_tests".to_owned()];
            expected.extend(args);
            assert_eq!(ls.args, expected);

            Ok(())
        }

        #[fasync::run_singlethreaded(test)]
        async fn namespace_added() -> Result<(), Error> {
            let (launcher_proxy, recv) = start_launcher()?;

            let ns = setup_namespace(true, vec!["/some_path1", "/some_path2"])?;

            let _launch_info = configure_launcher(LauncherConfigArgs {
                bin_path: "test/runner_tests",
                name: "name",
                args: None,
                ns: ns,
                job: None,
                handle_infos: None,
                name_infos: None,
                environs: None,
                launcher: &launcher_proxy,
            })
            .await?;

            drop(launcher_proxy);

            let ls = recv.await?;

            assert_eq!(
                ls.names,
                vec!("/pkg", "/some_path1", "/some_path2")
                    .into_iter()
                    .map(|s| s.to_string())
                    .rev()
                    .collect::<Vec<String>>()
            );

            Ok(())
        }

        #[fasync::run_singlethreaded(test)]
        async fn extra_namespace_entries() -> Result<(), Error> {
            let (launcher_proxy, recv) = start_launcher()?;

            let ns = setup_namespace(true, vec!["/some_path1", "/some_path2"])?;

            let mut names = vec![];

            let extra_paths = vec!["/extra1", "/extra2"];

            for path in &extra_paths {
                let (client, _server) = create_endpoints::<fio::DirectoryMarker>()
                    .expect("could not create component controller endpoints");

                names.push(fproc::NameInfo { path: path.to_string(), directory: client });
            }

            let _launch_info = configure_launcher(LauncherConfigArgs {
                bin_path: "test/runner_tests",
                name: "name",
                args: None,
                ns: ns,
                job: None,
                handle_infos: None,
                name_infos: Some(names),
                environs: None,
                launcher: &launcher_proxy,
            })
            .await?;

            drop(launcher_proxy);

            let ls = recv.await?;

            let mut paths = vec!["/pkg", "/some_path1", "/some_path2"];
            paths.extend(extra_paths.into_iter().rev());

            assert_eq!(
                ls.names,
                paths.into_iter().map(|s| s.to_string()).rev().collect::<Vec<String>>()
            );

            Ok(())
        }

        #[fasync::run_singlethreaded(test)]
        async fn handles_added() -> Result<(), Error> {
            let (launcher_proxy, recv) = start_launcher()?;

            let ns = setup_namespace(true, vec![])?;

            let _launch_info = configure_launcher(LauncherConfigArgs {
                bin_path: "test/runner_tests",
                name: "name",
                args: None,
                ns: ns,
                job: None,
                handle_infos: None,
                name_infos: None,
                environs: None,
                launcher: &launcher_proxy,
            })
            .await?;

            drop(launcher_proxy);

            let ls = recv.await?;

            assert_eq!(
                ls.handles,
                vec!(
                    HandleInfo::new(HandleType::LdsvcLoader, 0).as_raw(),
                    HandleInfo::new(HandleType::DefaultJob, 0).as_raw()
                )
            );

            Ok(())
        }

        #[fasync::run_singlethreaded(test)]
        async fn extra_handles() -> Result<(), Error> {
            let (launcher_proxy, recv) = start_launcher()?;

            let ns = setup_namespace(true, vec![])?;

            let mut handle_infos = vec![];
            for fd in 0..3 {
                let (client, _server) = create_endpoints::<fio::DirectoryMarker>()
                    .expect("could not create component controller endpoints");
                handle_infos.push(fproc::HandleInfo {
                    handle: client.into_channel().into_handle(),
                    id: fd,
                });
            }

            let _launch_info = configure_launcher(LauncherConfigArgs {
                bin_path: "test/runner_tests",
                name: "name",
                args: None,
                ns: ns,
                job: None,
                handle_infos: Some(handle_infos),
                name_infos: None,
                environs: None,
                launcher: &launcher_proxy,
            })
            .await?;

            drop(launcher_proxy);

            let ls = recv.await?;

            assert_eq!(
                ls.handles,
                vec!(
                    0,
                    1,
                    2,
                    HandleInfo::new(HandleType::LdsvcLoader, 0).as_raw(),
                    HandleInfo::new(HandleType::DefaultJob, 0).as_raw(),
                )
            );

            Ok(())
        }
    }
}

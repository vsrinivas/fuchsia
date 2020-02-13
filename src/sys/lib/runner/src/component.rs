// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    async_trait::async_trait,
    fidl::endpoints::ClientEnd,
    fidl_fuchsia_io::{self as fio, CLONE_FLAG_SAME_RIGHTS, OPEN_RIGHT_READABLE},
    fidl_fuchsia_process as fproc, fidl_fuchsia_sys2 as fsys,
    fuchsia_runtime::{job_default, HandleInfo, HandleType},
    fuchsia_zircon::{self as zx, HandleBased},
    futures::prelude::*,
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
pub trait Killable {
    /// Should kill self and do cleanup.
    /// Should not return error or panic, should log error instead.
    async fn kill(self);
}

/// Holds information about the component that allows the controller to
/// interact with and control the component.
pub struct Controller<C: Killable> {
    /// stream via which the component manager will ask the controller to
    /// manipulate the component
    request_stream: fsys::ComponentControllerRequestStream,

    /// killable object which kills underlying component.
    /// This would be None once the object is killed.
    killable: Option<C>,
}

impl<C: Killable> Controller<C> {
    /// Creates new instance
    pub fn new(killable: C, requests: fsys::ComponentControllerRequestStream) -> Controller<C> {
        Controller { killable: Some(killable), request_stream: requests }
    }

    /// Serve the request stream held by this Controller.
    pub async fn serve(mut self) -> Result<(), Error> {
        while let Ok(Some(request)) = self.request_stream.try_next().await {
            match request {
                fsys::ComponentControllerRequest::Stop { control_handle: c } => {
                    // for now, treat a stop the same as a kill because this
                    // is not yet implementing proper behavior to first ask the
                    // remote process to exit
                    self.kill().await;
                    c.shutdown();
                    break;
                }
                fsys::ComponentControllerRequest::Kill { control_handle: c } => {
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
        if self.killable.is_some() {
            self.killable.take().unwrap().kill().await;
        }
    }
}

/// An error encountered trying convert fsys::ComponentNamespace
#[derive(Debug, Error)]
pub enum ComponentNamespaceError {
    #[error("cannot clone directory for cloning namespace: {:?}", _0)]
    DirectoryClone(anyhow::Error),

    #[error("cannot convert directory handle to proxy: {}.", _0)]
    IntoProxy(fidl::Error),
}

/// This represents Component namespace which is easier for other functions in this library to read
/// and operate on.
#[derive(Default)]
pub struct ComponentNamespace {
    /// Pair representing path and directory proxy.
    items: Vec<(String, fio::DirectoryProxy)>,
}

impl TryFrom<fsys::ComponentNamespace> for ComponentNamespace {
    type Error = ComponentNamespaceError;

    fn try_from(mut ns: fsys::ComponentNamespace) -> Result<Self, Self::Error> {
        let mut new_ns = Self { items: Vec::with_capacity(ns.directories.len()) };

        while let Some(path) = ns.paths.pop() {
            if let Some(dir) = ns.directories.pop() {
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
            let client_proxy = io_util::clone_directory(proxy, CLONE_FLAG_SAME_RIGHTS)
                .map_err(ComponentNamespaceError::DirectoryClone)?;
            ns.items.push((path.clone(), client_proxy));
        }
        Ok(ns)
    }
}

/// An error encountered trying convert fsys::ComponentNamespace
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
    let mut handle_infos = config_args.handle_infos.unwrap_or(vec![]);
    let mut name_infos = config_args.name_infos.unwrap_or(vec![]);
    let args = config_args.args.unwrap_or(vec![]);

    let bin_arg = &[String::from(
        PKG_PATH
            .join(&config_args.bin_path)
            .to_str()
            .ok_or(LaunchError::InvalidBinaryPath(config_args.bin_path.to_string()))?,
    )];

    // Start the library loader service
    let pkg_str = PKG_PATH.to_str().unwrap();
    let (ll_client_chan, ll_service_chan) =
        zx::Channel::create().map_err(LaunchError::ChannelCreation)?;
    let (_, pkg_proxy) = config_args
        .ns
        .items
        .iter()
        .find(|(p, _)| p.as_str() == pkg_str)
        .ok_or(LaunchError::MissingPkg)?;

    let lib_proxy = io_util::open_directory(pkg_proxy, &Path::new("lib"), OPEN_RIGHT_READABLE)
        .map_err(|e| LaunchError::LibLoadError(e.to_string()))?;

    // The loader service should only be able to load files from `/pkg/lib`. Giving it a larger
    // scope is potentially a security vulnerability, as it could make it trivial for parts of
    // applications to get handles to things the application author didn't intend.
    library_loader::start(lib_proxy, ll_service_chan);

    let executable_vmo = library_loader::load_vmo(pkg_proxy, &config_args.bin_path)
        .await
        .map_err(|e| LaunchError::LoadingExecutable(e.to_string()))?;

    let job = config_args
        .job
        .unwrap_or(job_default().create_child_job().map_err(LaunchError::JobCreation)?);

    let job_dup =
        job.duplicate_handle(zx::Rights::SAME_RIGHTS).map_err(LaunchError::DuplicateJob)?;

    let mut string_iters: Vec<_> = bin_arg.iter().chain(args.iter()).map(|s| s.bytes()).collect();
    config_args
        .launcher
        .add_args(
            &mut string_iters.iter_mut().map(|iter| iter as &mut dyn ExactSizeIterator<Item = u8>),
        )
        .map_err(|e| LaunchError::AddArgs(e.to_string()))?;

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

    let environs: Vec<_> = config_args.environs.unwrap_or(vec![]);

    if environs.len() > 0 {
        let mut environs_iters: Vec<_> = environs.iter().map(|s| s.bytes()).collect();
        config_args
            .launcher
            .add_environs(
                &mut environs_iters
                    .iter_mut()
                    .map(|iter| iter as &mut dyn ExactSizeIterator<Item = u8>),
            )
            .map_err(|e| LaunchError::AddEnvirons(e.to_string()))?;
    }

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
        fidl::endpoints::create_proxy, fuchsia_async as fasync,
    };

    struct FakeComponent<K>
    where
        K: FnOnce(),
    {
        pub onkill: Option<K>,
    }

    #[async_trait]
    impl<K> Killable for FakeComponent<K>
    where
        K: FnOnce() + std::marker::Send,
    {
        async fn kill(mut self) {
            let func = self.onkill.take().unwrap();
            func();
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_kill_component() -> Result<(), Error> {
        let (sender, recv) = futures::channel::oneshot::channel::<()>();
        let fake_component = FakeComponent {
            onkill: Some(move || {
                sender.send(()).unwrap();
            }),
        };

        let (client_endpoint, server_endpoint) =
            create_endpoints::<fsys::ComponentControllerMarker>()
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
            onkill: Some(move || {
                sender.send(()).unwrap();
            }),
        };

        let (client_endpoint, server_endpoint) =
            create_endpoints::<fsys::ComponentControllerMarker>()
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
            let mut ns = fsys::ComponentNamespace { paths: vec![], directories: vec![] };
            if include_pkg {
                let pkg_path = "/pkg".to_string();
                let pkg_chan = io_util::open_directory_in_namespace("/pkg", OPEN_RIGHT_READABLE)
                    .unwrap()
                    .into_channel()
                    .unwrap()
                    .into_zx_channel();
                let pkg_handle = ClientEnd::new(pkg_chan);

                ns = fsys::ComponentNamespace {
                    paths: vec![pkg_path],
                    directories: vec![pkg_handle],
                };
            }

            for path in extra_paths {
                let (client, _server) = create_endpoints::<fio::DirectoryMarker>()
                    .expect("could not create component controller endpoints");
                ns.paths.push(path.to_string());
                ns.directories.push(client);
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
                Err(LaunchError::LibLoadError(
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

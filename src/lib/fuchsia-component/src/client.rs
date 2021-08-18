// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Tools for starting or connecting to existing Fuchsia applications and services.

use {
    crate::DEFAULT_SERVICE_INSTANCE,
    anyhow::{format_err, Context as _, Error},
    fidl::endpoints::{
        DiscoverableProtocolMarker, MemberOpener, ProtocolMarker, Proxy, ServerEnd, ServiceMarker,
        ServiceProxy,
    },
    fidl_fuchsia_io::DirectoryProxy,
    fidl_fuchsia_sys::{
        ComponentControllerEvent, ComponentControllerEventStream, ComponentControllerProxy,
        FileDescriptor, FlatNamespace, LaunchInfo, LauncherMarker, LauncherProxy, ServiceList,
        TerminationReason,
    },
    fidl_fuchsia_sys2::{ChildRef, RealmMarker, RealmProxy},
    fuchsia_async as fasync,
    fuchsia_runtime::HandleType,
    fuchsia_zircon::{self as zx, Socket, SocketOpts},
    futures::{
        future::{self, FutureExt, TryFutureExt},
        stream::{StreamExt, TryStreamExt},
        Future,
    },
    log::*,
    std::{
        borrow::Borrow,
        fmt,
        fs::File,
        marker::PhantomData,
        path::{Path, PathBuf},
        sync::Arc,
    },
    thiserror::Error,
};

/// Path to the service directory in an application's root namespace.
const SVC_DIR: &'static str = "/svc";

/// A protocol connection request that allows checking if the protocol exists.
pub struct ProtocolConnector<D: Borrow<DirectoryProxy>, P: DiscoverableProtocolMarker> {
    svc_dir: D,
    _svc_marker: PhantomData<P>,
}

impl<D: Borrow<DirectoryProxy>, P: DiscoverableProtocolMarker> ProtocolConnector<D, P> {
    /// Returns a new `ProtocolConnector` to `P` in the specified service directory.
    fn new(svc_dir: D) -> ProtocolConnector<D, P> {
        ProtocolConnector { svc_dir, _svc_marker: PhantomData }
    }

    /// Returns `true` if the protocol exists in the service directory.
    ///
    /// This method requires a round trip to the service directory to check for
    /// existence.
    pub async fn exists(&self) -> Result<bool, Error> {
        match files_async::dir_contains(self.svc_dir.borrow(), P::NAME).await {
            Ok(v) => Ok(v),
            // If the service directory is unavailable, then mask the error as if
            // the protocol does not exist.
            Err(files_async::Error::Fidl(
                _,
                fidl::Error::ClientChannelClosed { status, protocol_name: _ },
            )) if status == zx::Status::PEER_CLOSED => Ok(false),
            Err(e) => Err(Error::new(e).context("error checking for service entry in directory")),
        }
    }

    /// Connect to the FIDL protocol using the provided server-end.
    ///
    /// Note, this method does not check if the protocol exists. It is up to the
    /// caller to call `exists` to check for existence.
    pub fn connect_with(self, server_end: zx::Channel) -> Result<(), Error> {
        self.svc_dir
            .borrow()
            .open(
                fidl_fuchsia_io::OPEN_RIGHT_READABLE,
                0, /* mode */
                P::NAME,
                fidl::endpoints::ServerEnd::new(server_end),
            )
            .context("error connecting to protocol")
    }

    /// Connect to the FIDL protocol.
    ///
    /// Note, this method does not check if the protocol exists. It is up to the
    /// caller to call `exists` to check for existence.
    pub fn connect(self) -> Result<P::Proxy, Error> {
        let (proxy, server) = zx::Channel::create().context("error creating zx channels")?;
        let () = self.connect_with(server).context("error connecting with server channel")?;
        let proxy =
            fasync::Channel::from_channel(proxy).context("error creating proxy from channel")?;
        Ok(P::Proxy::from_channel(proxy))
    }
}

/// Clone the handle to the service directory in the application's root namespace.
pub fn clone_namespace_svc() -> Result<fidl_fuchsia_io::DirectoryProxy, Error> {
    io_util::directory::open_in_namespace(SVC_DIR, fidl_fuchsia_io::OPEN_RIGHT_READABLE)
        .context("error opening svc directory")
}

/// Return a FIDL protocol connector at the default service directory in the
/// application's root namespace.
pub fn new_protocol_connector<P: DiscoverableProtocolMarker>(
) -> Result<ProtocolConnector<DirectoryProxy, P>, Error> {
    new_protocol_connector_at::<P>(SVC_DIR)
}

/// Return a FIDL protocol connector at the specified service directory in the
/// application's root namespace.
///
/// The service directory path must be an absolute path.
pub fn new_protocol_connector_at<P: DiscoverableProtocolMarker>(
    service_directory_path: &str,
) -> Result<ProtocolConnector<DirectoryProxy, P>, Error> {
    let dir = io_util::directory::open_in_namespace(
        service_directory_path,
        fidl_fuchsia_io::OPEN_RIGHT_READABLE,
    )
    .context("error opening service directory")?;

    Ok(ProtocolConnector::new(dir))
}

/// Return a FIDL protocol connector at the specified service directory.
pub fn new_protocol_connector_in_dir<P: DiscoverableProtocolMarker>(
    dir: &DirectoryProxy,
) -> ProtocolConnector<&DirectoryProxy, P> {
    ProtocolConnector::new(dir)
}

/// Connect to a FIDL protocol using the provided channel.
pub fn connect_channel_to_protocol<P: DiscoverableProtocolMarker>(
    server_end: zx::Channel,
) -> Result<(), Error> {
    connect_channel_to_protocol_at::<P>(server_end, SVC_DIR)
}

/// Connect to a FIDL protocol using the provided channel and namespace prefix.
pub fn connect_channel_to_protocol_at<P: DiscoverableProtocolMarker>(
    server_end: zx::Channel,
    service_directory_path: &str,
) -> Result<(), Error> {
    let protocol_path = format!("{}/{}", service_directory_path, P::PROTOCOL_NAME);
    connect_channel_to_protocol_at_path(server_end, &protocol_path)
}

/// Connect to a FIDL protocol using the provided channel and namespace path.
pub fn connect_channel_to_protocol_at_path(
    server_end: zx::Channel,
    protocol_path: &str,
) -> Result<(), Error> {
    fdio::service_connect(&protocol_path, server_end)
        .with_context(|| format!("Error connecting to protocol path: {}", protocol_path))
}

/// Connect to a FIDL protocol using the application root namespace.
pub fn connect_to_protocol<P: DiscoverableProtocolMarker>() -> Result<P::Proxy, Error> {
    connect_to_protocol_at::<P>(SVC_DIR)
}

/// Connect to a FIDL protocol using the provided namespace prefix.
pub fn connect_to_protocol_at<P: DiscoverableProtocolMarker>(
    service_prefix: &str,
) -> Result<P::Proxy, Error> {
    let (proxy, server) = zx::Channel::create()?;
    connect_channel_to_protocol_at::<P>(server, service_prefix)?;
    let proxy = fasync::Channel::from_channel(proxy)?;
    Ok(P::Proxy::from_channel(proxy))
}

/// Connect to a FIDL protocol using the provided path.
pub fn connect_to_protocol_at_path<P: ProtocolMarker>(
    protocol_path: &str,
) -> Result<P::Proxy, Error> {
    let (proxy, server) = zx::Channel::create()?;
    connect_channel_to_protocol_at_path(server, protocol_path)?;
    let proxy = fasync::Channel::from_channel(proxy)?;
    Ok(P::Proxy::from_channel(proxy))
}

/// Connect to an instance of a FIDL protocol hosted in `directory`.
pub fn connect_to_protocol_at_dir_root<P: DiscoverableProtocolMarker>(
    directory: &DirectoryProxy,
) -> Result<P::Proxy, Error> {
    connect_to_named_protocol_at_dir_root::<P>(directory, P::PROTOCOL_NAME)
}

/// Connect to an instance of a FIDL protocol hosted in `directory` using the given `filename`.
pub fn connect_to_named_protocol_at_dir_root<P: DiscoverableProtocolMarker>(
    directory: &DirectoryProxy,
    filename: &str,
) -> Result<P::Proxy, Error> {
    let proxy = io_util::open_node(
        directory,
        &PathBuf::from(filename),
        fidl_fuchsia_io::OPEN_RIGHT_READABLE | fidl_fuchsia_io::OPEN_RIGHT_WRITABLE,
        fidl_fuchsia_io::MODE_TYPE_SERVICE,
    )
    .context("Failed to open protocol in directory")?;
    let proxy = P::Proxy::from_channel(proxy.into_channel().unwrap());
    Ok(proxy)
}

/// Connect to an instance of a FIDL protocol hosted in `directory`, in the `svc/` subdir.
pub fn connect_to_protocol_at_dir_svc<P: DiscoverableProtocolMarker>(
    directory: &DirectoryProxy,
) -> Result<P::Proxy, Error> {
    let proxy = io_util::open_node(
        directory,
        &PathBuf::from(format!("svc/{}", P::PROTOCOL_NAME)),
        fidl_fuchsia_io::OPEN_RIGHT_READABLE | fidl_fuchsia_io::OPEN_RIGHT_WRITABLE,
        fidl_fuchsia_io::MODE_TYPE_SERVICE,
    )
    .context("Failed to open protocol in directory")?;
    let proxy = P::Proxy::from_channel(proxy.into_channel().unwrap());
    Ok(proxy)
}

struct DirectoryProtocolImpl(DirectoryProxy);

impl MemberOpener for DirectoryProtocolImpl {
    fn open_member(&self, member: &str, server_end: zx::Channel) -> Result<(), fidl::Error> {
        let flags = fidl_fuchsia_io::OPEN_RIGHT_READABLE | fidl_fuchsia_io::OPEN_RIGHT_WRITABLE;
        self.0.open(
            flags,
            fidl_fuchsia_io::MODE_TYPE_SERVICE,
            member,
            ServerEnd::new(server_end),
        )?;
        Ok(())
    }
}

/// Connect to the "default" instance of a FIDL service in the `/svc` directory of
/// the application's root namespace.
pub fn connect_to_service<S: ServiceMarker>() -> Result<S::Proxy, Error> {
    connect_to_service_instance_at::<S>(SVC_DIR, DEFAULT_SERVICE_INSTANCE)
}

/// Connect to an instance of a FIDL service in the `/svc` directory of
/// the application's root namespace.
/// `instance` is a path of one or more components.
// NOTE: We would like to use impl AsRef<T> to accept a wide variety of string-like
// inputs but Rust limits specifying explicit generic parameters when `impl-traits`
// are present.
pub fn connect_to_service_instance<S: ServiceMarker>(instance: &str) -> Result<S::Proxy, Error> {
    connect_to_service_instance_at::<S>(SVC_DIR, instance)
}

/// Connect to an instance of a FIDL service using the provided path prefix.
/// `path_prefix` should not contain any slashes.
/// `instance` is a path of one or more components.
// NOTE: We would like to use impl AsRef<T> to accept a wide variety of string-like
// inputs but Rust limits specifying explicit generic parameters when `impl-traits`
// are present.
pub fn connect_to_service_instance_at<S: ServiceMarker>(
    path_prefix: &str,
    instance: &str,
) -> Result<S::Proxy, Error> {
    let mut service_path = PathBuf::new();
    service_path.push(path_prefix);
    service_path.push(S::SERVICE_NAME);
    service_path.push(instance);
    let directory_proxy = io_util::open_directory_in_namespace(
        service_path.to_str().unwrap(),
        io_util::OPEN_RIGHT_READABLE | io_util::OPEN_RIGHT_WRITABLE,
    )?;
    Ok(S::Proxy::from_member_opener(Box::new(DirectoryProtocolImpl(directory_proxy))))
}

/// Connect to the "default" instance of a FIDL service hosted on the directory protocol
/// channel `directory`.
pub fn connect_to_service_at_dir<S: ServiceMarker>(
    directory: &zx::Channel,
) -> Result<S::Proxy, Error> {
    connect_to_service_instance_at_dir::<S>(directory, DEFAULT_SERVICE_INSTANCE)
}

/// Connect to an instance of a FIDL service hosted on the directory protocol channel `directory`.
/// `instance` is a path of one or more components.
// NOTE: We would like to use impl AsRef<T> to accept a wide variety of string-like
// inputs but Rust limits specifying explicit generic parameters when `impl-traits`
// are present.
pub fn connect_to_service_instance_at_dir<S: ServiceMarker>(
    directory: &zx::Channel,
    instance: &str,
) -> Result<S::Proxy, Error> {
    let service_path = Path::new(S::SERVICE_NAME).join(instance);
    let (directory_proxy, server_end) =
        fidl::endpoints::create_proxy::<fidl_fuchsia_io::DirectoryMarker>()?;
    let flags = fidl_fuchsia_io::OPEN_FLAG_DIRECTORY
        | fidl_fuchsia_io::OPEN_RIGHT_READABLE
        | fidl_fuchsia_io::OPEN_RIGHT_WRITABLE;
    fdio::open_at(directory, service_path.to_str().unwrap(), flags, server_end.into_channel())?;
    Ok(S::Proxy::from_member_opener(Box::new(DirectoryProtocolImpl(directory_proxy))))
}

/// Opens a FIDL service as a directory so that its instances can be listed.
pub fn open_service<S: ServiceMarker>() -> Result<DirectoryProxy, Error> {
    let service_path = Path::new(SVC_DIR).join(S::SERVICE_NAME);
    Ok(io_util::directory::open_in_namespace(
        service_path.to_str().unwrap(),
        io_util::OPEN_RIGHT_READABLE | io_util::OPEN_RIGHT_WRITABLE,
    )?)
}

/// Opens the exposed directory from a child. Only works in CFv2, and only works if this component
/// uses `fuchsia.sys2.Realm`.
pub async fn open_childs_exposed_directory(
    child_name: impl Into<String>,
    collection_name: Option<String>,
) -> Result<DirectoryProxy, Error> {
    let realm_proxy = connect_to_protocol::<RealmMarker>()?;
    let (directory_proxy, server_end) =
        fidl::endpoints::create_proxy::<fidl_fuchsia_io::DirectoryMarker>()?;
    let name: String = child_name.into();
    realm_proxy
        .open_exposed_dir(
            &mut ChildRef { name: name.clone(), collection: collection_name.clone() },
            server_end,
        )
        .await?
        .map_err(|e| {
            format_err!(
                "failed to bind to child {} in collection {:?}: {:?}",
                name,
                collection_name,
                e
            )
        })?;
    Ok(directory_proxy)
}

/// Connects to a FIDL protocol exposed by a child that's within the `/svc` directory. Only works in
/// CFv2, and only works if this component uses `fuchsia.sys2.Realm`.
pub async fn connect_to_childs_protocol<P: DiscoverableProtocolMarker>(
    child_name: String,
    collection_name: Option<String>,
) -> Result<P::Proxy, Error> {
    let child_exposed_directory =
        open_childs_exposed_directory(child_name, collection_name).await?;
    connect_to_protocol_at_dir_root::<P>(&child_exposed_directory)
}

/// Adds a new directory handle to the namespace for the new process.
pub fn add_handle_to_namespace(namespace: &mut FlatNamespace, path: String, dir: zx::Handle) {
    namespace.paths.push(path);
    namespace.directories.push(zx::Channel::from(dir));
}

/// Adds a new directory to the namespace for the new process.
pub fn add_dir_to_namespace(
    namespace: &mut FlatNamespace,
    path: String,
    dir: File,
) -> Result<(), Error> {
    let handle = fdio::transfer_fd(dir)?;
    Ok(add_handle_to_namespace(namespace, path, handle))
}

/// Returns a connection to the application launcher protocol. Components v1 only.
pub fn launcher() -> Result<LauncherProxy, Error> {
    connect_to_protocol::<LauncherMarker>()
}

/// Launch an application at the specified URL. Components v1 only.
pub fn launch(
    launcher: &LauncherProxy,
    url: String,
    arguments: Option<Vec<String>>,
) -> Result<App, Error> {
    launch_with_options(launcher, url, arguments, LaunchOptions::new())
}

/// Options for the launcher when starting an applications.
pub struct LaunchOptions {
    namespace: Option<Box<FlatNamespace>>,
    out: Option<Box<FileDescriptor>>,
    additional_services: Option<Box<ServiceList>>,
}

impl LaunchOptions {
    /// Creates default launch options.
    pub fn new() -> LaunchOptions {
        LaunchOptions { namespace: None, out: None, additional_services: None }
    }

    /// Adds a new directory handle to the namespace for the new process.
    pub fn add_handle_to_namespace(&mut self, path: String, dir: zx::Handle) -> &mut Self {
        let namespace = self
            .namespace
            .get_or_insert_with(|| Box::new(FlatNamespace { paths: vec![], directories: vec![] }));
        add_handle_to_namespace(namespace, path, dir);
        self
    }

    /// Adds a new directory to the namespace for the new process.
    pub fn add_dir_to_namespace(&mut self, path: String, dir: File) -> Result<&mut Self, Error> {
        let handle = fdio::transfer_fd(dir)?;
        Ok(self.add_handle_to_namespace(path, handle))
    }

    /// Sets the out handle.
    pub fn set_out(&mut self, f: FileDescriptor) -> &mut Self {
        self.out = Some(Box::new(f));
        self
    }

    /// Set additional services to add the new component's namespace under /svc, in addition to
    /// those coming from the environment. 'host_directory' should be a channel to the directory
    /// hosting the services in 'names'. Subsequent calls will override previous calls.
    pub fn set_additional_services(
        &mut self,
        names: Vec<String>,
        host_directory: zx::Channel,
    ) -> &mut Self {
        let list = ServiceList { names, host_directory: Some(host_directory), provider: None };
        self.additional_services = Some(Box::new(list));
        self
    }
}

/// Launch an application at the specified URL. Components v1 only.
pub fn launch_with_options(
    launcher: &LauncherProxy,
    url: String,
    arguments: Option<Vec<String>>,
    options: LaunchOptions,
) -> Result<App, Error> {
    let (controller, controller_server_end) = fidl::endpoints::create_proxy()?;
    let (directory_request, directory_server_chan) = zx::Channel::create()?;
    let directory_request = Arc::new(directory_request);
    let mut launch_info = LaunchInfo {
        url,
        arguments,
        out: options.out,
        err: None,
        directory_request: Some(directory_server_chan),
        flat_namespace: options.namespace,
        additional_services: options.additional_services,
    };
    launcher
        .create_component(&mut launch_info, Some(controller_server_end.into()))
        .context("Failed to start a new Fuchsia application.")?;
    Ok(App { directory_request, controller, stdout: None, stderr: None })
}

/// Returns a connection to the Realm protocol. Components v2 only.
pub fn realm() -> Result<RealmProxy, Error> {
    connect_to_protocol::<RealmMarker>()
}

/// `App` represents a launched application.
///
/// When `App` is dropped, launched application will be terminated.
#[must_use = "Dropping `App` will cause the application to be terminated."]
pub struct App {
    // directory_request is a directory protocol channel
    directory_request: Arc<zx::Channel>,

    // Keeps the component alive until `App` is dropped.
    controller: ComponentControllerProxy,

    //TODO pub accessors to take stdout/stderr in wrapper types that implement AsyncRead.
    stdout: Option<fasync::Socket>,
    stderr: Option<fasync::Socket>,
}

impl App {
    /// Returns a reference to the directory protocol channel of the application.
    #[inline]
    pub fn directory_channel(&self) -> &zx::Channel {
        &self.directory_request
    }

    /// Returns reference of directory request which can be passed to `ServiceFs::add_proxy_service_to`.
    #[inline]
    pub fn directory_request(&self) -> &Arc<zx::Channel> {
        &self.directory_request
    }

    /// Returns a reference to the component controller.
    #[inline]
    pub fn controller(&self) -> &ComponentControllerProxy {
        &self.controller
    }

    /// Connect to a protocol provided by the `App`.
    #[inline]
    pub fn connect_to_protocol<P: DiscoverableProtocolMarker>(&self) -> Result<P::Proxy, Error> {
        let (client_channel, server_channel) = zx::Channel::create()?;
        self.pass_to_protocol::<P>(server_channel)?;
        Ok(P::Proxy::from_channel(fasync::Channel::from_channel(client_channel)?))
    }

    /// Connect to a protocol provided by the `App`.
    #[inline]
    pub fn connect_to_named_protocol<P: DiscoverableProtocolMarker>(
        &self,
        protocol_name: &str,
    ) -> Result<P::Proxy, Error> {
        let (client_channel, server_channel) = zx::Channel::create()?;
        self.pass_to_named_protocol(protocol_name, server_channel)?;
        Ok(P::Proxy::from_channel(fasync::Channel::from_channel(client_channel)?))
    }

    /// Connect to a FIDL service provided by the `App`.
    #[inline]
    pub fn connect_to_service<S: ServiceMarker>(&self) -> Result<S::Proxy, Error> {
        connect_to_service_at_dir::<S>(&self.directory_request)
    }

    /// Connect to a protocol by passing a channel for the server.
    #[inline]
    pub fn pass_to_protocol<P: DiscoverableProtocolMarker>(
        &self,
        server_channel: zx::Channel,
    ) -> Result<(), Error> {
        self.pass_to_named_protocol(P::PROTOCOL_NAME, server_channel)
    }

    /// Connect to a protocol by name.
    #[inline]
    pub fn pass_to_named_protocol(
        &self,
        protocol_name: &str,
        server_channel: zx::Channel,
    ) -> Result<(), Error> {
        fdio::service_connect_at(&self.directory_request, protocol_name, server_channel)?;
        Ok(())
    }

    /// Forces the component to exit.
    pub fn kill(&mut self) -> Result<(), fidl::Error> {
        self.controller.kill()
    }

    /// Wait for the component to terminate and return its exit status.
    pub fn wait(&mut self) -> impl Future<Output = Result<ExitStatus, Error>> {
        ExitStatus::from_event_stream(self.controller.take_event_stream())
    }

    /// Wait for the component to terminate and return its exit status, stdout, and stderr.
    pub fn wait_with_output(mut self) -> impl Future<Output = Result<Output, Error>> {
        let drain = |pipe: Option<fasync::Socket>| match pipe {
            None => future::ready(Ok(vec![])).left_future(),
            Some(pipe) => pipe.into_datagram_stream().try_concat().err_into().right_future(),
        };

        future::try_join3(self.wait(), drain(self.stdout), drain(self.stderr))
            .map_ok(|(exit_status, stdout, stderr)| Output { exit_status, stdout, stderr })
    }
}

/// A component builder, providing a simpler interface to
/// [`fidl_fuchsia_sys::LauncherProxy::create_component`].
///
/// `AppBuilder`s interface matches
/// [`std:process:Command`](https://doc.rust-lang.org/std/process/struct.Command.html) as
/// closely as possible, except where the semantics of spawning a process differ from the
/// semantics of spawning a Fuchsia component:
///
/// * Fuchsia components do not support stdin, a current working directory, or environment
/// variables.
///
/// * `AppBuilder` will move certain handles into the spawned component (see
/// [`AppBuilder::add_dir_to_namespace`]), so a single instance of `AppBuilder` can only be
/// used to create a single component.
#[derive(Debug)]
pub struct AppBuilder {
    launch_info: LaunchInfo,
    directory_request: Option<Arc<zx::Channel>>,
    stdout: Option<Stdio>,
    stderr: Option<Stdio>,
}

impl AppBuilder {
    /// Creates a new `AppBuilder` for the component referenced by the given `url`.
    pub fn new(url: impl Into<String>) -> Self {
        Self {
            launch_info: LaunchInfo {
                url: url.into(),
                arguments: None,
                out: None,
                err: None,
                directory_request: None,
                flat_namespace: None,
                additional_services: None,
            },
            directory_request: None,
            stdout: None,
            stderr: None,
        }
    }

    /// Returns a reference to the local end of the component's directory_request channel,
    /// creating it if necessary.
    pub fn directory_request(&mut self) -> Result<&Arc<zx::Channel>, Error> {
        Ok(match self.directory_request {
            Some(ref channel) => channel,
            None => {
                let (directory_request, directory_server_chan) = zx::Channel::create()?;
                let directory_request = Arc::new(directory_request);
                self.launch_info.directory_request = Some(directory_server_chan);
                self.directory_request = Some(directory_request);
                self.directory_request.as_ref().unwrap()
            }
        })
    }

    /// Configures stdout for the new process.
    pub fn stdout(mut self, cfg: impl Into<Stdio>) -> Self {
        self.stdout = Some(cfg.into());
        self
    }

    /// Configures stderr for the new process.
    pub fn stderr(mut self, cfg: impl Into<Stdio>) -> Self {
        self.stderr = Some(cfg.into());
        self
    }

    /// Mounts a handle to a directory in the namespace of the component.
    pub fn add_handle_to_namespace(mut self, path: String, handle: zx::Handle) -> Self {
        let namespace = self
            .launch_info
            .flat_namespace
            .get_or_insert_with(|| Box::new(FlatNamespace { paths: vec![], directories: vec![] }));
        add_handle_to_namespace(namespace, path, handle);
        self
    }

    /// Mounts an opened directory in the namespace of the component.
    pub fn add_dir_to_namespace(self, path: String, dir: File) -> Result<Self, Error> {
        let handle = fdio::transfer_fd(dir)?;
        Ok(self.add_handle_to_namespace(path, handle))
    }

    /// Append the given `arg` to the sequence of arguments passed to the new process.
    pub fn arg(mut self, arg: impl Into<String>) -> Self {
        self.launch_info.arguments.get_or_insert_with(Vec::new).push(arg.into());
        self
    }

    /// Append all the given `args` to the sequence of arguments passed to the new process.
    pub fn args(mut self, args: impl IntoIterator<Item = impl Into<String>>) -> Self {
        self.launch_info
            .arguments
            .get_or_insert_with(Vec::new)
            .extend(args.into_iter().map(|arg| arg.into()));
        self
    }

    /// Launch the component using the provided `launcher` proxy, returning the launched
    /// application or the error encountered while launching it.
    ///
    /// By default:
    /// * when the returned [`App`] is dropped, the launched application will be terminated.
    /// * stdout and stderr will use the the default stdout and stderr for the environment.
    pub fn spawn(self, launcher: &LauncherProxy) -> Result<App, Error> {
        self.launch(launcher)
    }

    /// Launches the component using the provided `launcher` proxy, waits for it to finish, and
    /// collects all of its output.
    ///
    /// By default, stdout and stderr are captured (and used to provide the resulting output).
    pub fn output(
        mut self,
        launcher: &LauncherProxy,
    ) -> Result<impl Future<Output = Result<Output, Error>>, Error> {
        self.stdout = self.stdout.or(Some(Stdio::MakePipe));
        self.stderr = self.stderr.or(Some(Stdio::MakePipe));
        Ok(self.launch(launcher)?.wait_with_output())
    }

    /// Launches the component using the provided `launcher` proxy, waits for it to finish, and
    /// collects its exit status.
    ///
    /// By default, stdout and stderr will use the default stdout and stderr for the
    /// environment.
    pub fn status(
        self,
        launcher: &LauncherProxy,
    ) -> Result<impl Future<Output = Result<ExitStatus, Error>>, Error> {
        Ok(self.launch(launcher)?.wait())
    }

    fn launch(mut self, launcher: &LauncherProxy) -> Result<App, Error> {
        let (controller, controller_server_end) = fidl::endpoints::create_proxy()?;
        let directory_request = if let Some(directory_request) = self.directory_request.take() {
            directory_request
        } else {
            let (directory_request, directory_server_chan) = zx::Channel::create()?;
            self.launch_info.directory_request = Some(directory_server_chan);
            Arc::new(directory_request)
        };

        let (stdout, remote_stdout) =
            self.stdout.unwrap_or(Stdio::Inherit).to_local_and_remote()?;
        if let Some(fd) = remote_stdout {
            self.launch_info.out = Some(Box::new(fd));
        }

        let (stderr, remote_stderr) =
            self.stderr.unwrap_or(Stdio::Inherit).to_local_and_remote()?;
        if let Some(fd) = remote_stderr {
            self.launch_info.err = Some(Box::new(fd));
        }

        launcher
            .create_component(&mut self.launch_info, Some(controller_server_end.into()))
            .context("Failed to start a new Fuchsia application.")?;

        Ok(App { directory_request, controller, stdout, stderr })
    }
}

/// Describes what to do with a standard I/O stream for a child component.
#[derive(Debug)]
pub enum Stdio {
    /// Use the default output sink for the environment.
    Inherit,
    /// Capture the component's output in a new socket.
    MakePipe,
    /// Provide a handle (that is valid in a `fidl_fuchsia_sys::FileDescriptor`) to the component to
    /// write output to.
    Handle(zx::Handle),
}

impl Stdio {
    fn to_local_and_remote(
        self,
    ) -> Result<(Option<fasync::Socket>, Option<FileDescriptor>), Error> {
        match self {
            Stdio::Inherit => Ok((None, None)),
            Stdio::MakePipe => {
                let (local, remote) = Socket::create(SocketOpts::STREAM)?;
                // local end is read-only
                local.half_close()?;

                let local = fasync::Socket::from_socket(local)?;
                let remote = FileDescriptor {
                    type0: HandleType::FileDescriptor as i32,
                    type1: 0,
                    type2: 0,
                    handle0: Some(remote.into()),
                    handle1: None,
                    handle2: None,
                };

                Ok((Some(local), Some(remote)))
            }
            Stdio::Handle(handle) => Ok((
                None,
                Some(FileDescriptor {
                    type0: HandleType::FileDescriptor as i32,
                    type1: 0,
                    type2: 0,
                    handle0: Some(handle),
                    handle1: None,
                    handle2: None,
                }),
            )),
        }
    }
}

impl<T: Into<zx::Handle>> From<T> for Stdio {
    fn from(t: T) -> Self {
        Self::Handle(t.into())
    }
}

/// Describes the result of a component after it has terminated.
#[derive(Debug, Clone, Error)]
pub struct ExitStatus {
    return_code: i64,
    termination_reason: TerminationReason,
}

impl ExitStatus {
    /// Consumes an `event_stream`, returning the `ExitStatus` of the component when it exits, or
    /// the error encountered while waiting for the component to terminate.
    pub fn from_event_stream(
        event_stream: ComponentControllerEventStream,
    ) -> impl Future<Output = Result<Self, Error>> {
        event_stream
            .try_filter_map(|event| {
                future::ready(match event {
                    ComponentControllerEvent::OnTerminated { return_code, termination_reason } => {
                        Ok(Some(ExitStatus { return_code, termination_reason }))
                    }
                    _ => Ok(None),
                })
            })
            .into_future()
            .map(|(next, _rest)| match next {
                Some(result) => result.map_err(|err| err.into()),
                _ => Ok(ExitStatus {
                    return_code: -1,
                    termination_reason: TerminationReason::Unknown,
                }),
            })
    }

    /// Did the component exit without an error?  Success is defined as a reason of exited and
    /// a code of 0.
    #[inline]
    pub fn success(&self) -> bool {
        self.exited() && self.return_code == 0
    }

    /// Returns true if the component exited, as opposed to not starting at all due to some
    /// error or terminating with any reason other than `EXITED`.
    #[inline]
    pub fn exited(&self) -> bool {
        self.termination_reason == TerminationReason::Exited
    }

    /// The reason the component was terminated.
    #[inline]
    pub fn reason(&self) -> TerminationReason {
        self.termination_reason
    }

    /// The return code from the component. Guaranteed to be non-zero if termination reason is
    /// not `EXITED`.
    #[inline]
    pub fn code(&self) -> i64 {
        self.return_code
    }

    /// Converts the `ExitStatus` to a `Result<(), ExitStatus>`, mapping to `Ok(())` if the
    /// component exited with status code 0, or to `Err(ExitStatus)` otherwise.
    pub fn ok(&self) -> Result<(), Self> {
        if self.success() {
            Ok(())
        } else {
            Err(self.clone())
        }
    }
}

impl fmt::Display for ExitStatus {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        if self.exited() {
            write!(f, "Exited with {}", self.code())
        } else {
            write!(f, "Terminated with reason {:?}", self.reason())
        }
    }
}

/// The output of a finished component.
pub struct Output {
    /// The exit status of the component.
    pub exit_status: ExitStatus,
    /// The data that the component wrote to stdout.
    pub stdout: Vec<u8>,
    /// The data that the component wrote to stderr.
    pub stderr: Vec<u8>,
}

/// The output of a component that terminated with a failure.
#[derive(Clone, Error)]
#[error("{}", exit_status)]
pub struct OutputError {
    #[source]
    exit_status: ExitStatus,
    stdout: String,
    stderr: String,
}

impl Output {
    /// Converts the `Output` to a `Result<(), OutputError>`, mapping to `Ok(())` if the component
    /// exited with status code 0, or to `Err(OutputError)` otherwise.
    pub fn ok(&self) -> Result<(), OutputError> {
        if self.exit_status.success() {
            Ok(())
        } else {
            let stdout = String::from_utf8_lossy(&self.stdout).into_owned();
            let stderr = String::from_utf8_lossy(&self.stderr).into_owned();
            Err(OutputError { exit_status: self.exit_status.clone(), stdout, stderr })
        }
    }
}

impl fmt::Debug for OutputError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        struct RawMultilineString<'a>(&'a str);

        impl<'a> fmt::Debug for RawMultilineString<'a> {
            fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
                if self.0.is_empty() {
                    f.write_str(r#""""#)
                } else {
                    f.write_str("r#\"")?;
                    f.write_str(self.0)?;
                    f.write_str("\"#")
                }
            }
        }

        f.debug_struct("OutputError")
            .field("exit_status", &self.exit_status)
            .field("stdout", &RawMultilineString(&self.stdout))
            .field("stderr", &RawMultilineString(&self.stderr))
            .finish()
    }
}

#[cfg(test)]
mod tests {
    use fidl::endpoints::DiscoverableProtocolMarker as _;
    use fidl_fuchsia_component_client_test::{
        ServiceAMarker, ServiceAProxy, ServiceBMarker, ServiceBProxy,
    };

    use vfs::{
        directory::entry::DirectoryEntry, execution_scope::ExecutionScope,
        file::vmo::read_only_static, pseudo_directory,
    };

    use super::*;

    #[fasync::run_singlethreaded(test)]
    async fn test_svc_connector_svc_does_not_exist() -> Result<(), Error> {
        let req = new_protocol_connector::<ServiceAMarker>().context("error probing service")?;
        matches::assert_matches!(req.exists().await.context("error checking service"), Ok(false));
        let _: ServiceAProxy = req.connect().context("error connecting to service")?;

        let req = new_protocol_connector_at::<ServiceAMarker>(SVC_DIR)
            .context("error probing service at svc dir")?;
        matches::assert_matches!(
            req.exists().await.context("error checking service at svc dir"),
            Ok(false)
        );
        let _: ServiceAProxy = req.connect().context("error connecting to service at svc dir")?;

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_svc_connector_connect_with_dir() -> Result<(), Error> {
        let dir = pseudo_directory! {
            ServiceBMarker::PROTOCOL_NAME => read_only_static("read_only"),
        };
        let (dir_proxy, dir_server) =
            fidl::endpoints::create_proxy::<fidl_fuchsia_io::DirectoryMarker>()?;
        let scope = ExecutionScope::new();
        dir.open(
            scope,
            fidl_fuchsia_io::OPEN_RIGHT_READABLE,
            fidl_fuchsia_io::MODE_TYPE_DIRECTORY,
            vfs::path::Path::dot(),
            ServerEnd::new(dir_server.into_channel()),
        );

        let req = new_protocol_connector_in_dir::<ServiceAMarker>(&dir_proxy);
        matches::assert_matches!(
            req.exists().await.context("error probing invalid service"),
            Ok(false)
        );
        let _: ServiceAProxy = req.connect().context("error connecting to invalid service")?;

        let req = new_protocol_connector_in_dir::<ServiceBMarker>(&dir_proxy);
        matches::assert_matches!(req.exists().await.context("error probing service"), Ok(true));
        let _: ServiceBProxy = req.connect().context("error connecting to service")?;

        Ok(())
    }
}

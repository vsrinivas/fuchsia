// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Connect to or provide Fuchsia services.

#![deny(missing_docs)]

#[allow(unused)] // Remove pending fix to rust-lang/rust#53682
use {
    failure::{Error, ResultExt, Fail, format_err},
    fdio::fdio_sys,
    fuchsia_async as fasync,
    futures::{
        Future, Poll,
        stream::{FuturesUnordered, StreamExt, StreamFuture},
        task::Context,
    },
    fidl::endpoints::{RequestStream, ServiceMarker, Proxy},
    fidl_fuchsia_io::{
        DirectoryRequestStream,
        DirectoryRequest,
        DirectoryObject,
        NodeAttributes,
        NodeInfo,
    },
    fidl_fuchsia_sys::{
        ComponentControllerProxy,
        EnvironmentControllerProxy,
        EnvironmentMarker,
        EnvironmentOptions,
        FlatNamespace,
        LauncherMarker,
        LauncherProxy,
        LaunchInfo,
        LoaderMarker,
        ServiceList,
    },
    fuchsia_zircon::{self as zx, Peered, Signals},
    std::{
        fs::File,
        marker::{PhantomData, Unpin},
        os::unix::io::IntoRawFd,
        pin::Pin,
    },
};

/// Creates an `&'static str` containing the URL of a Fuchsia package
/// from a string literal containng the name of a fuchsia component
/// containing only a single package.
///
/// e.g. `fuchsia_single_component_package_url!("my_server")` would
/// create `fuchsia-pkg://fuchsia.com/my_server#meta/my_server.cmx`.
#[macro_export]
macro_rules! fuchsia_single_component_package_url {
    ($component_name:expr) => {
        concat!(
            "fuchsia-pkg://fuchsia.com/",
            $component_name,
            "#meta/",
            $component_name,
            ".cmx",
            )
    }
}

/// Tools for starting or connecting to existing Fuchsia applications and services.
pub mod client {
    use super::*;

    /// Connect to a FIDL service using the provided channel and namespace prefix.
    pub fn connect_channel_to_service_at<S: ServiceMarker>(
        server_end: zx::Channel,
        service_prefix: &str,
    ) -> Result<(), Error> {
        let service_path = format!("{}/{}", service_prefix, S::NAME);
        fdio::service_connect(&service_path, server_end)
            .with_context(|_| format!("Error connecting to service path: {}", service_path))?;
        Ok(())
    }

    /// Connect to a FIDL service using the provided channel.
    pub fn connect_channel_to_service<S: ServiceMarker>(server_end: zx::Channel)
        -> Result<(), Error>
    {
        connect_channel_to_service_at::<S>(server_end, "/svc")
    }

    /// Connect to a FIDL service using the provided namespace prefix.
    pub fn connect_to_service_at<S: ServiceMarker>(service_prefix: &str)
        -> Result<S::Proxy, Error>
    {
        let (proxy, server) = zx::Channel::create()?;
        connect_channel_to_service_at::<S>(server, service_prefix)?;
        let proxy = fasync::Channel::from_channel(proxy)?;
        Ok(S::Proxy::from_channel(proxy))
    }

    /// Connect to a FIDL service using the application root namespace.
    pub fn connect_to_service<S: ServiceMarker>()
        -> Result<S::Proxy, Error>
    {
        connect_to_service_at::<S>("/svc")
    }

    /// Options for the launcher when starting an applications.
    pub struct LaunchOptions {
        namespace: Option<Box<FlatNamespace>>
    }

    impl LaunchOptions {
        /// Creates default launch options.
        pub fn new() -> LaunchOptions {
            LaunchOptions {
                namespace: None
            }
        }

        /// Adds a new directory to the namespace for the new process.
        pub fn add_dir_to_namespace(&mut self, path: String, dir: File) -> Result<&mut Self, Error> {
            let handle = fdio::transfer_fd(dir)?;
            let namespace = self.namespace.get_or_insert_with(||
                  Box::new(FlatNamespace {paths: vec![], directories: vec![]}));
            namespace.paths.push(path);
            namespace.directories.push(zx::Channel::from(handle));

            Ok(self)
        }
    }

    /// Launcher launches Fuchsia applications.
    #[derive(Clone)]
    pub struct Launcher {
        launcher: LauncherProxy,
    }

    impl From<LauncherProxy> for Launcher {
        fn from(other: LauncherProxy) -> Self {
            Launcher { launcher: other }
        }
    }

    impl Into<LauncherProxy> for Launcher {
        fn into(self) -> LauncherProxy {
            self.launcher
        }
    }

    impl Launcher {
        #[inline]
        /// Create a new application launcher.
        pub fn new() -> Result<Self, Error> {
            let launcher = connect_to_service::<LauncherMarker>()?;
            Ok(Launcher { launcher })
        }

        /// Launch an application at the specified URL.
        pub fn launch(
            &self,
            url: String,
            arguments: Option<Vec<String>>,
        ) -> Result<App, Error> {
            self.launch_with_options(url, arguments, LaunchOptions::new())
        }

        /// Launch an application at the specified URL.
        pub fn launch_with_options(
            &self,
            url: String,
            arguments: Option<Vec<String>>,
            options: LaunchOptions
        ) -> Result<App, Error> {
            let (controller, controller_server_end) = zx::Channel::create()?;
            let (directory_request, directory_server_chan) = zx::Channel::create()?;

            let mut launch_info = LaunchInfo {
                url,
                arguments,
                out: None,
                err: None,
                directory_request: Some(directory_server_chan),
                flat_namespace: options.namespace,
                additional_services: None,
            };

            self.launcher
                .create_component(&mut launch_info, Some(controller_server_end.into()))
                .context("Failed to start a new Fuchsia application.")?;

            let controller = fasync::Channel::from_channel(controller)?;
            let controller = ComponentControllerProxy::new(controller);

            Ok(App { directory_request, controller })
        }
    }

    /// `App` represents a launched application.
    ///
    /// When `App` is dropped, launched application will be terminated.
    #[must_use = "Dropping `App` will cause the application to be terminated."]
    pub struct App {
        // directory_request is a directory protocol channel
        directory_request: zx::Channel,

        // Keeps the component alive until `App` is dropped.
        #[allow(dead_code)]
        controller: ComponentControllerProxy,
    }

    impl App {
        /// Returns a reference to the directory protocol channel of the application.
        #[inline]
        pub fn directory_channel(&self) -> &zx::Channel {
            &self.directory_request
        }

        /// Returns a reference to the component controller.
        #[inline]
        pub fn controller(&self) -> &ComponentControllerProxy {
            &self.controller
        }

        #[inline]
        /// Connect to a service provided by the `App`.
        pub fn connect_to_service<S: ServiceMarker>(&self)
            -> Result<S::Proxy, Error>
        {
            let (client_channel, server_channel) = zx::Channel::create()?;
            self.pass_to_service::<S>(server_channel)?;
            Ok(S::Proxy::from_channel(fasync::Channel::from_channel(client_channel)?))
        }

        /// Connect to a service by passing a channel for the server.
        pub fn pass_to_service<S: ServiceMarker>(&self, server_channel: zx::Channel)
            -> Result<(), Error>
        {
            self.pass_to_named_service(S::NAME, server_channel)
        }

        /// Connect to a service by name.
        pub fn pass_to_named_service(&self, service_name: &str, server_channel: zx::Channel)
            -> Result<(), Error>
        {
            fdio::service_connect_at(&self.directory_request, service_name, server_channel)?;
            Ok(())
        }
    }
}

/// Tools for providing Fuchsia services.
pub mod server {
    use super::*;

    /// An error indicating the startup handle on which the FIDL server
    /// attempted to start was missing.
    #[derive(Debug, Fail)]
    #[fail(display = "The startup handle on which the FIDL server attempted to start was missing.")]
    pub struct MissingStartupHandle;

    /// `ServiceFactory` lazily creates instances of services.
    ///
    /// Note that this trait is implemented by `FnMut` closures like `|| MyService { ... }`.
    pub trait ServiceFactory: Send + 'static {
        /// The path name of a service.
        ///
        /// Used by the `FdioServer` to know which service to connect incoming requests to.
        fn service_name(&self) -> &str;

        /// Create a new instance of the service on the provided `fasync::Channel`.
        fn spawn_service(&mut self, channel: fasync::Channel);

        /// Create a new instance of the service on the provided `zx::Channel`.
        fn spawn_service_zx_channel(&mut self, channel: zx::Channel) {
            match fasync::Channel::from_channel(channel) {
                Ok(chan) => self.spawn_service(chan),
                Err(e) => eprintln!("Unable to convert channel to async: {:?}", e),
            }
        }
    }

    impl<F> ServiceFactory for (&'static str, F)
        where F: FnMut(fasync::Channel) + Send + 'static,
    {
        fn service_name(&self) -> &str {
            self.0
        }

        fn spawn_service(&mut self, channel: fasync::Channel) {
            (self.1)(channel)
        }
    }

    /// `ServicesServer` is a server which manufactures service instances of
    /// varying types on demand.
    /// To run a `ServicesServer`, use `Server::new`.
    pub struct ServicesServer {
        services: Vec<Box<ServiceFactory>>,
    }

    impl ServicesServer {
        /// Create a new `ServicesServer` which doesn't provide any services.
        pub fn new() -> Self {
            ServicesServer { services: vec![] }
        }

        /// Add a service to the `ServicesServer`.
        pub fn add_service<S: ServiceFactory>(mut self, service_factory: S) -> Self {
            self.services.push(Box::new(service_factory));
            self
        }

        /// Add a service that proxies requests to the current environment.
        pub fn add_proxy_service<S: ServiceMarker>(self) -> Self {
            struct Proxy<S>(PhantomData<S>);
            impl<S: ServiceMarker> ServiceFactory for Proxy<S> {
                fn service_name(&self) -> &str {
                    S::NAME
                }
                fn spawn_service(&mut self, channel: fasync::Channel) {
                    self.spawn_service_zx_channel(channel.into())
                }
                fn spawn_service_zx_channel(&mut self, channel: zx::Channel) {
                    if let Err(e) = crate::client::connect_channel_to_service::<S>(channel) {
                        eprintln!("failed to proxy request to {}: {:?}", S::NAME, e);
                    }
                }
            }

            self.add_service(Proxy::<S>(PhantomData))
        }

        /// Add a service to the `ServicesServer` that will launch a component
        /// upon request, proxying requests to the launched component.
        pub fn add_component_proxy_service(
            self,
            service_name: &'static str,
            component_url: String,
            arguments: Option<Vec<String>>,
        ) -> Self {
            // Note: the launched app will live for the remaining lifetime of the
            // closure, which will be exactly as long as the lifetime of the resulting
            // `FdioServer`. When the `FdioServer` is dropped, the launched component
            // will be terminated.
            let mut launched_app = None;
            let mut args = Some((component_url, arguments));
            self.add_service((service_name, move |channel: fasync::Channel| {
                let res = (|| {
                    if let Some((component_url, arguments)) = args.take() {
                        launched_app = Some(
                            crate::client::Launcher::new()?.launch(component_url, arguments)?);
                    }
                    if let Some(app) = launched_app.as_ref() {
                        app.pass_to_named_service(service_name, channel.into())?;
                    }
                    Ok::<(), Error>(())
                })();
                if let Err(e) = res {
                    eprintln!("ServicesServer failed to launch component: {:?}", e);
                }
            }))
        }

        /// Start serving directory protocol service requests on a new channel.
        pub fn start_on_channel(self, channel: zx::Channel) -> Result<FdioServer, Error> {
            let channel = fasync::Channel::from_channel(channel)?;
            channel
                .as_ref()
                .signal_peer(Signals::NONE, Signals::USER_0)
                .unwrap_or_else(|e| {
                    eprintln!("ServicesServer::start_on_channel signal_peer failed with {}", e);
                });

            let mut server = FdioServer {
                connections: FuturesUnordered::new(),
                factories: self.services,
            };

            server.serve_connection(channel);
            Ok(server)
        }

        /// Start serving directory protocol service requests on the process
        /// PA_DIRECTORY_REQUEST handle
        pub fn start(self) -> Result<FdioServer, Error> {
            let fdio_handle =
                fuchsia_runtime::take_startup_handle(
                    fuchsia_runtime::HandleType::DirectoryRequest.into()
                ).ok_or(MissingStartupHandle)?;

            self.start_on_channel(fdio_handle.into())
        }

        /// Start serving directory protocol service requests via a `ServiceList`.
        /// The resulting `ServiceList` can be attached to a new environment in
        /// order to provide child components with access to these services.
        pub fn start_services_list(self) -> Result<(FdioServer, ServiceList), Error> {
            let (chan1, chan2) = zx::Channel::create()?;

            self.start_on_channel(chan1).map(|fdio_server| {
                let names = fdio_server.factories
                                .iter()
                                .map(|x| x.service_name().to_owned())
                                .collect();
                (
                    fdio_server,
                    ServiceList {
                        names,
                        provider: None,
                        host_directory: Some(chan2),
                    },
                )
            })
        }

        /// Starts a new component inside an environment that only has access to
        /// the services provided through this `ServicesServer`.
        ///
        /// Note that the resulting `App` and `EnvironmentControllerProxy` must be kept
        /// alive for the component to continue running. Once they are dropped, the
        /// component will be destroyed.
        pub fn launch_component_in_nested_environment(
            self,
            url: String,
            arguments: Option<Vec<String>>,
            environment_label: &str,
        ) -> Result<(FdioServer, EnvironmentControllerProxy, crate::client::App), Error> {
            let (fdio_server, new_env_controller, app) = self
                .launch_component_in_nested_environment_with_options(
                    url,
                    arguments,
                    client::LaunchOptions::new(),
                    environment_label,
                )?;
            Ok((fdio_server, new_env_controller, app))
        }

        /// Starts a new component inside an isolated environment with custom launch
        /// options, see the comment for |launch_component_in_nested_environment()|
        /// above.
        pub fn launch_component_in_nested_environment_with_options(
            self,
            url: String,
            arguments: Option<Vec<String>>,
            options: client::LaunchOptions,
            environment_label: &str,
        ) -> Result<(FdioServer, EnvironmentControllerProxy, crate::client::App), Error> {
            let env = crate::client::connect_to_service::<EnvironmentMarker>()
                .context("connecting to current environment")?;
            let services_with_loader = self.add_proxy_service::<LoaderMarker>();
            let (fdio_server, mut service_list) = services_with_loader.start_services_list()?;

            let (new_env, new_env_server_end) = fidl::endpoints::create_proxy()?;
            let (new_env_controller, new_env_controller_server_end) =
                fidl::endpoints::create_proxy()?;
            env.create_nested_environment(
                new_env_server_end,
                new_env_controller_server_end,
                environment_label,
                Some(fidl::encoding::OutOfLine(&mut service_list)),
                &mut EnvironmentOptions {
                    inherit_parent_services: false,
                    allow_parent_runners: false,
                    kill_on_oom: false,
                    delete_storage_on_death: false,
                },
            )
            .context("creating isolated environment")?;

            let (launcher_proxy, launcher_server_end) = fidl::endpoints::create_proxy()?;
            new_env
                .get_launcher(launcher_server_end)
                .context("getting nested environment launcher")?;

            let launcher = crate::client::Launcher::from(launcher_proxy);
            let app = launcher.launch_with_options(url, arguments, options)?;

            Ok((fdio_server, new_env_controller, app))
        }
    }

    /// `FdioServer` is a very basic vfs directory server that only responds to
    /// OPEN and CLONE messages. OPEN always connects the client channel to a
    /// newly spawned fidl service produced by the factory F.
    #[must_use = "futures must be polled"]
    pub struct FdioServer {
        // The open connections to this FdioServer. These connections
        // represent external clients who may attempt to open services.
        connections: FuturesUnordered<StreamFuture<DirectoryRequestStream>>,
        // The collection of services, exported from the current process,
        // which may be opened from external clients.
        factories: Vec<Box<ServiceFactory>>,
    }

    impl Unpin for FdioServer {}

    impl FdioServer {
        /// Add an additional connection to the `FdioServer` to provide services to.
        pub fn serve_connection(&mut self, chan: fasync::Channel) {
            self.connections.push(DirectoryRequestStream::from_channel(chan).into_future())
        }

        fn handle_request(&mut self, req: DirectoryRequest) -> Result<(), Error> {
            match req {
                DirectoryRequest::Clone { flags: _, object, control_handle: _ } => {
                    let service_channel = fasync::Channel::from_channel(object.into_channel())?;
                    self.serve_connection(service_channel);
                    Ok(())
                }
                DirectoryRequest::Close { responder, } => {
                    responder.send(zx::sys::ZX_OK).map_err(|e| e.into())
                }
                DirectoryRequest::Open { flags: _, mode: _, path, object, control_handle: _, } => {
                    // This mechanism to open "public" redirects the service
                    // request to the FDIO Server itself for historical reasons.
                    //
                    // This has the unfortunate implication that "public" can be
                    // continually opened from the directory, resulting in paths
                    // like "public/public/public".
                    //
                    // TODO(smklein): Implement a more realistic pseudo-directory,
                    // capable of distinguishing between different characteristics
                    // of imported / exported directories.
                    if path == "public" {
                        let service_channel = fasync::Channel::from_channel(object.into_channel())?;
                        self.serve_connection(service_channel);
                        return Ok(());
                    }

                    match self.factories.iter_mut().find(|factory| factory.service_name() == path) {
                        Some(factory) => factory.spawn_service_zx_channel(object.into_channel()),
                        None => eprintln!("No service found for path {}", path),
                    }
                    Ok(())
                }
                DirectoryRequest::Describe { responder } => {
                    let mut info = NodeInfo::Directory(DirectoryObject);
                    responder.send(&mut info).map_err(|e| e.into())
                }
                // Unsupported / Ignored methods.
                DirectoryRequest::GetAttr { responder, } => {
                    let mut attrs = NodeAttributes {
                        mode: 0,
                        id: 0,
                        content_size: 0,
                        storage_size: 0,
                        link_count: 0,
                        creation_time: 0,
                        modification_time: 0,
                    };
                    responder.send(zx::sys::ZX_ERR_NOT_SUPPORTED, &mut attrs).map_err(|e| e.into())
                }
                DirectoryRequest::SetAttr { flags: _, attributes:_, responder, } => {
                    responder.send(zx::sys::ZX_ERR_NOT_SUPPORTED).map_err(|e| e.into())
                }
                DirectoryRequest::Ioctl { opcode: _, max_out: _, handles: _, in_: _, responder, } => {
                    responder.send(zx::sys::ZX_ERR_NOT_SUPPORTED,
                                   &mut std::iter::empty(),
                                   &mut std::iter::empty()).map_err(|e| e.into())
                }
                DirectoryRequest::Sync { responder, } => {
                    responder.send(zx::sys::ZX_ERR_NOT_SUPPORTED).map_err(|e| e.into())
                }
                DirectoryRequest::Unlink { path: _, responder, } => {
                    responder.send(zx::sys::ZX_ERR_NOT_SUPPORTED).map_err(|e| e.into())
                }
                DirectoryRequest::ReadDirents { max_bytes: _, responder, } => {
                    responder.send(zx::sys::ZX_ERR_NOT_SUPPORTED,
                                   &mut std::iter::empty()).map_err(|e| e.into())
                }
                DirectoryRequest::Rewind { responder, } => {
                    responder.send(zx::sys::ZX_ERR_NOT_SUPPORTED).map_err(|e| e.into())
                }
                DirectoryRequest::GetToken { responder, } => {
                    responder.send(zx::sys::ZX_ERR_NOT_SUPPORTED, None).map_err(|e| e.into())
                }
                DirectoryRequest::Rename { src: _, dst_parent_token: _, dst: _, responder, } => {
                    responder.send(zx::sys::ZX_ERR_NOT_SUPPORTED).map_err(|e| e.into())
                }
                DirectoryRequest::Link { src: _, dst_parent_token: _, dst: _, responder, } => {
                    responder.send(zx::sys::ZX_ERR_NOT_SUPPORTED).map_err(|e| e.into())
                }
                DirectoryRequest::Watch { mask: _, options: _, watcher: _, responder, } => {
                    responder.send(zx::sys::ZX_ERR_NOT_SUPPORTED).map_err(|e| e.into())
                }
            }
        }
    }

    impl Future for FdioServer {
        type Output = Result<(), Error>;

        fn poll(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
            loop {
                match self.connections.poll_next_unpin(cx) {
                    Poll::Ready(Some((maybe_request, stream))) => {
                        if let Some(Ok(request)) = maybe_request {
                            match self.handle_request(request) {
                                Ok(()) => self.connections.push(stream.into_future()),
                                _ => (),
                            }
                        }
                    },
                    Poll::Ready(None) | Poll::Pending => return Poll::Pending,
                }
            }
        }
    }
}

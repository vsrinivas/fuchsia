// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Tools for providing Fuchsia services.

use {
    byteorder::{LittleEndian, WriteBytesExt as _},
    failure::{bail, Error, Fail, ResultExt},
    fidl::{
        encoding::OutOfLine,
        endpoints::{Proxy as _, RequestStream, ServerEnd, ServiceMarker},
    },
    fidl_fuchsia_io::{
        DirectoryObject, DirectoryRequest, DirectoryRequestStream, NodeAttributes, NodeInfo,
        NodeMarker, OPEN_FLAG_DESCRIBE, OPEN_FLAG_DIRECTORY, OPEN_FLAG_NODE_REFERENCE,
        OPEN_FLAG_POSIX, OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE,
    },
    fidl_fuchsia_sys::{
        EnvironmentControllerProxy, EnvironmentMarker, EnvironmentOptions, LauncherProxy,
        LoaderMarker, ServiceList,
    },
    fuchsia_async as fasync,
    fuchsia_zircon::{self as zx, Peered, Signals},
    futures::{
        ready,
        stream::{FuturesUnordered, StreamExt},
        task::Context,
        Future, Poll, Stream,
    },
    std::{
        collections::hash_map::{Entry, HashMap},
        io::Write,
        marker::{PhantomData, Unpin},
        pin::Pin,
        sync::Arc,
    },
};

mod service;
pub use service::{FidlService, Service, ServiceObj, ServiceObjLocal, ServiceObjTrait};

enum ServiceFsNode<ServiceObjTy: ServiceObjTrait> {
    Directory {
        /// A map from filename to index in the parent `ServiceFs`.
        children: HashMap<String, usize>,
    },
    Service(ServiceObjTy),
}

impl<ServiceObjTy: ServiceObjTrait> ServiceFsNode<ServiceObjTy> {
    fn expect_dir(&mut self) -> &mut HashMap<String, usize> {
        match self {
            ServiceFsNode::Directory { children } => children,
            ServiceFsNode::Service(_) => panic!("ServiceFs expected directory"),
        }
    }

    fn to_dirent_type(&self) -> u8 {
        match self {
            ServiceFsNode::Directory { .. } => fidl_fuchsia_io::DIRENT_TYPE_DIRECTORY,
            ServiceFsNode::Service(_) => fidl_fuchsia_io::DIRENT_TYPE_SERVICE,
        }
    }
}

/// A filesystem which connects clients to services.
///
/// This type implements the `Stream` trait and will yield the values
/// returned from calling `Service::connect` on the services it hosts.
///
/// This can be used to, for example, yield streams of channels, request
/// streams, futures to run, or any other value that should be processed
/// as the result of a request.
#[must_use]
pub struct ServiceFs<ServiceObjTy: ServiceObjTrait> {
    /// The open connections to this `ServiceFs`. These connections
    /// represent external clients who may attempt to open services.
    client_connections: FuturesUnordered<ClientConnection>,

    /// The tree of `ServiceFsNode`s.
    /// The root is always a directory at index 0.
    ///
    //
    // FIXME(cramertj) move to a generational index and support
    // removal of nodes.
    nodes: Vec<ServiceFsNode<ServiceObjTy>>,
}

const ROOT_NODE: usize = 0;
const NO_FLAGS: u32 = 0;
const CLONE_REQ_SUPPORTED_FLAGS: u32 =
    OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE | OPEN_FLAG_DESCRIBE;
const OPEN_REQ_SUPPORTED_FLAGS: u32 = OPEN_RIGHT_READABLE
    | OPEN_RIGHT_WRITABLE
    | OPEN_FLAG_DESCRIBE
    | OPEN_FLAG_POSIX
    | OPEN_FLAG_DIRECTORY
    | OPEN_FLAG_NODE_REFERENCE;

impl<'a, Output: 'a> ServiceFs<ServiceObjLocal<'a, Output>> {
    /// Create a new `ServiceFs` that is singlethreaded-only and does not
    /// require services to implement `Send`.
    pub fn new_local() -> Self {
        Self {
            client_connections: FuturesUnordered::new(),
            nodes: vec![ServiceFsNode::Directory { children: HashMap::new() }],
        }
    }
}

impl<'a, Output: 'a> ServiceFs<ServiceObj<'a, Output>> {
    /// Create a new `ServiceFs` that is multithreaded-capable and requires
    /// services to implement `Send`.
    pub fn new() -> Self {
        Self {
            client_connections: FuturesUnordered::new(),
            nodes: vec![ServiceFsNode::Directory { children: HashMap::new() }],
        }
    }
}

/// A directory within a `ServiceFs`.
///
/// Services and subdirectories can be added to it.
pub struct ServiceFsDir<'a, ServiceObjTy: ServiceObjTrait> {
    position: usize,
    fs: &'a mut ServiceFs<ServiceObjTy>,
}

fn dir<'a, ServiceObjTy: ServiceObjTrait>(
    fs: &'a mut ServiceFs<ServiceObjTy>,
    position: usize,
    path: String,
) -> ServiceFsDir<'a, ServiceObjTy> {
    let new_node_position = fs.nodes.len();
    let self_dir = fs.nodes[position].expect_dir();
    let &mut position = self_dir.entry(path.clone()).or_insert(new_node_position);
    if position == new_node_position {
        fs.nodes.push(ServiceFsNode::Directory { children: HashMap::new() });
    } else {
        if let ServiceFsNode::Service(_) = &fs.nodes[position] {
            panic!("Error adding dir to ServiceFs: existing service at \"{}\"", path)
        }
    }
    ServiceFsDir { position, fs }
}

fn add_service<'a, ServiceObjTy: ServiceObjTrait>(
    fs: &'a mut ServiceFs<ServiceObjTy>,
    position: usize,
    path: String,
    service: ServiceObjTy,
) {
    let new_node_position = fs.nodes.len();
    let self_dir = fs.nodes[position].expect_dir();
    let entry = self_dir.entry(path);
    match entry {
        Entry::Occupied(prev) => {
            panic!("Duplicate ServiceFs service added at path \"{}\"", prev.key())
        }
        Entry::Vacant(slot) => {
            slot.insert(new_node_position);
            fs.nodes.push(ServiceFsNode::Service(service));
        }
    }
}

/// A `Service` implementation that proxies requests
/// to the outside environment.
///
/// Not intended for direct use. Use the `add_proxy_service`
/// function instead.
#[doc(hidden)]
pub struct Proxy<S, O>(PhantomData<(S, fn() -> O)>);

impl<S: ServiceMarker, O> Service for Proxy<S, O> {
    type Output = O;
    fn connect(&mut self, channel: zx::Channel) -> Option<O> {
        if let Err(e) = crate::client::connect_channel_to_service::<S>(channel) {
            eprintln!("failed to proxy request to {}: {:?}", S::NAME, e);
        }
        None
    }
}

/// A `Service` implementation that proxies requests to the given component.
///
/// Not intended for direct use. Use the `add_proxy_service_to` function instead.
#[doc(hidden)]
pub struct ProxyTo<S, O> {
    directory_request: Arc<zx::Channel>,
    _phantom: PhantomData<(S, fn() -> O)>,
}

impl<S: ServiceMarker, O> Service for ProxyTo<S, O> {
    type Output = O;
    fn connect(&mut self, channel: zx::Channel) -> Option<O> {
        if let Err(e) = fdio::service_connect_at(&self.directory_request, S::NAME, channel) {
            eprintln!("failed to proxy request to {}: {:?}", S::NAME, e);
        }
        None
    }
}

struct LaunchData {
    component_url: String,
    arguments: Option<Vec<String>>,
}

/// A `Service` implementation that proxies requests
/// to a launched component.
///
/// Not intended for direct use. Use the `add_component_proxy_service`
/// function instead.
#[doc(hidden)]
pub struct ComponentProxy<O> {
    launch_data: Option<LaunchData>,
    launched_app: Option<crate::client::App>,
    service_name: &'static str,
    _marker: PhantomData<O>,
}

impl<O> Service for ComponentProxy<O> {
    type Output = O;
    fn connect(&mut self, channel: zx::Channel) -> Option<O> {
        let res = (|| {
            if let Some(LaunchData { component_url, arguments }) = self.launch_data.take() {
                self.launched_app = Some(crate::client::launch(
                    &crate::client::launcher()?,
                    component_url,
                    arguments,
                )?);
            }
            if let Some(app) = self.launched_app.as_ref() {
                app.pass_to_named_service(self.service_name, channel.into())?;
            }
            Ok::<(), Error>(())
        })();
        if let Err(e) = res {
            eprintln!("ServiceFs failed to launch component: {:?}", e);
        }
        None
    }
}

// Not part of a trait so that clients won't have to import a trait
// in order to call these functions.
macro_rules! add_service_functions {
    () => {
        /// Adds a FIDL service to the directory.
        ///
        /// The FIDL service will be hosted at the name provided by the
        /// `[Discoverable]` annotation in the FIDL source.
        pub fn add_fidl_service<F, RS>(
            &mut self,
            service: F,
        ) -> &mut Self
        where
            F: FnMut(RS) -> ServiceObjTy::Output,
            RS: RequestStream,
            FidlService<F, RS, ServiceObjTy::Output>: Into<ServiceObjTy>,
        {
            self.add_fidl_service_at(
                RS::Service::NAME,
                service,
            )
        }

        /// Adds a FIDL service to the directory at the given path.
        ///
        /// The path must be a single component containing no `/` characters.
        pub fn add_fidl_service_at<F, RS>(
            &mut self,
            path: impl Into<String>,
            service: F,
        ) -> &mut Self
        where
            F: FnMut(RS) -> ServiceObjTy::Output,
            RS: RequestStream,
            FidlService<F, RS, ServiceObjTy::Output>: Into<ServiceObjTy>,
        {
            self.add_service_at(
                path,
                FidlService::from(service),
            )
        }

        /// Adds a service that proxies requests to the current environment.
        // NOTE: we'd like to be able to remove the type parameter `O` here,
        //  but unfortunately the bound `ServiceObjTy: From<Proxy<S, ServiceObjTy::Output>>`
        //  makes type checking angry.
        pub fn add_proxy_service<S: ServiceMarker, O>(&mut self) -> &mut Self
        where
            ServiceObjTy: From<Proxy<S, O>>,
            ServiceObjTy: ServiceObjTrait<Output = O>,
        {
            self.add_service_at(
                S::NAME,
                Proxy::<S, ServiceObjTy::Output>(PhantomData),
            )
        }

        /// Adds a service that proxies requests to the given component.
        // NOTE: we'd like to be able to remove the type parameter `O` here,
        //  but unfortunately the bound `ServiceObjTy: From<Proxy<S, ServiceObjTy::Output>>`
        //  makes type checking angry.
        pub fn add_proxy_service_to<S: ServiceMarker, O>(&mut self, directory_request: Arc<zx::Channel>) -> &mut Self
        where
            ServiceObjTy: From<ProxyTo<S, O>>,
            ServiceObjTy: ServiceObjTrait<Output = O>,
        {
            self.add_service_at(
                S::NAME,
                ProxyTo::<S, ServiceObjTy::Output>{
                    directory_request, _phantom: PhantomData}
            )
        }

        /// Add a service to the `ServicesServer` that will launch a component
        /// upon request, proxying requests to the launched component.
        pub fn add_component_proxy_service<O>(
            &mut self,
            service_name: &'static str,
            component_url: String,
            arguments: Option<Vec<String>>,
        ) -> &mut Self
        where
            ServiceObjTy: From<ComponentProxy<O>>,
            ServiceObjTy: ServiceObjTrait<Output = O>,
        {
            self.add_service_at(
                service_name,
                ComponentProxy {
                    launch_data: Some(LaunchData { component_url, arguments }),
                    launched_app: None,
                    service_name,
                    _marker: PhantomData,
                }
            )
        }
    };
}

impl<'a, ServiceObjTy: ServiceObjTrait> ServiceFsDir<'a, ServiceObjTy> {
    /// Returns a reference to the subdirectory at the given path,
    /// creating one if none exists.
    ///
    /// The path must be a single component containing no `/` characters.
    ///
    /// Panics if a service has already been added at the given path.
    pub fn dir<'b>(&'b mut self, path: impl Into<String>) -> ServiceFsDir<'b, ServiceObjTy> {
        dir(self.fs, self.position, path.into())
    }

    add_service_functions!();

    /// Adds a service to the directory at the given path.
    ///
    /// The path must be a single component containing no `/` characters.
    ///
    /// Panics if any node has already been added at the given path.
    pub fn add_service_at(
        &mut self,
        path: impl Into<String>,
        service: impl Into<ServiceObjTy>,
    ) -> &mut Self {
        add_service(self.fs, self.position, path.into(), service.into());
        self
    }
}

impl<ServiceObjTy: ServiceObjTrait> ServiceFs<ServiceObjTy> {
    /// Returns a reference to the subdirectory at the given path,
    /// creating one if none exists.
    ///
    /// The path must be a single component containing no `/` characters.
    ///
    /// Panics if a service has already been added at the given path.
    pub fn dir<'a>(&'a mut self, path: impl Into<String>) -> ServiceFsDir<'a, ServiceObjTy> {
        dir(self, ROOT_NODE, path.into())
    }

    add_service_functions!();

    /// Adds a service to the directory at the given path.
    ///
    /// The path must be a single component containing no `/` characters.
    ///
    /// Panics if any node has already been added at the given path.
    pub fn add_service_at(
        &mut self,
        path: impl Into<String>,
        service: impl Into<ServiceObjTy>,
    ) -> &mut Self {
        add_service(self, ROOT_NODE, path.into(), service.into());
        self
    }

    /// Start serving directory protocol service requests via a `ServiceList`.
    /// The resulting `ServiceList` can be attached to a new environment in
    /// order to provide child components with access to these services.
    pub fn host_services_list(&mut self) -> Result<ServiceList, Error> {
        let names = self.nodes[ROOT_NODE].expect_dir().keys().cloned().collect();

        let (chan1, chan2) = zx::Channel::create()?;
        self.serve_connection(chan1)?;

        Ok(ServiceList { names, provider: None, host_directory: Some(chan2) })
    }

    /// Creates a new environment that only has access to the services provided through this
    /// `ServiceFs` and the enclosing environment's `Loader` service, appending a few random
    /// bytes to the given `environment_label_prefix` to ensure this environment has a unique
    /// name.
    ///
    /// Note that the resulting `NestedEnvironment` must be kept alive for the environment to
    /// continue to exist. Once dropped, the environment and all components launched within it
    /// will be destroyed.
    pub fn create_salted_nested_environment<O>(
        &mut self,
        environment_label_prefix: &str,
    ) -> Result<NestedEnvironment, Error>
    where
        ServiceObjTy: From<Proxy<LoaderMarker, O>>,
        ServiceObjTy: ServiceObjTrait<Output = O>,
    {
        let mut salt = [0; 4];
        fuchsia_zircon::cprng_draw(&mut salt[..]).expect("zx_cprng_draw does not fail");
        let environment_label = format!("{}_{}", environment_label_prefix, hex::encode(&salt));
        self.create_nested_environment(&environment_label)
    }

    /// Creates a new environment that only has access to the services provided through this
    /// `ServiceFs` and the enclosing environment's `Loader` service.
    ///
    /// Note that the resulting `NestedEnvironment` must be kept alive for the environment to
    /// continue to exist. Once dropped, the environment and all components launched within it
    /// will be destroyed.
    pub fn create_nested_environment<O>(
        &mut self,
        environment_label: &str,
    ) -> Result<NestedEnvironment, Error>
    where
        ServiceObjTy: From<Proxy<LoaderMarker, O>>,
        ServiceObjTy: ServiceObjTrait<Output = O>,
    {
        let env = crate::client::connect_to_service::<EnvironmentMarker>()
            .context("connecting to current environment")?;
        let services_with_loader = self.add_proxy_service::<LoaderMarker, _>();
        let mut service_list = services_with_loader.host_services_list()?;

        let (new_env, new_env_server_end) = fidl::endpoints::create_proxy()?;
        let (controller, controller_server_end) = fidl::endpoints::create_proxy()?;
        let (launcher, launcher_server_end) = fidl::endpoints::create_proxy()?;
        let (directory_request, directory_server_end) = zx::Channel::create()?;

        env.create_nested_environment(
            new_env_server_end,
            controller_server_end,
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

        new_env.get_launcher(launcher_server_end).context("getting nested environment launcher")?;
        self.serve_connection(directory_server_end)?;

        Ok(NestedEnvironment { controller, launcher, directory_request })
    }

    /// Starts a new component inside an environment that only has access to
    /// the services provided through this `ServicesServer`.
    ///
    /// Note that the resulting `App` and `EnvironmentControllerProxy` must be kept
    /// alive for the component to continue running. Once they are dropped, the
    /// component will be destroyed.
    pub fn launch_component_in_nested_environment<O>(
        &mut self,
        url: String,
        arguments: Option<Vec<String>>,
        environment_label: &str,
    ) -> Result<(EnvironmentControllerProxy, crate::client::App), Error>
    where
        ServiceObjTy: From<Proxy<LoaderMarker, O>>,
        ServiceObjTy: ServiceObjTrait<Output = O>,
    {
        let NestedEnvironment { controller, launcher, directory_request: _ } =
            self.create_nested_environment(environment_label)?;

        let app = crate::client::launch(&launcher, url, arguments)?;
        Ok((controller, app))
    }
}

/// `NestedEnvironment` represents an environment nested within another.
///
/// When `NestedEnvironment` is dropped, the environment and all components started within it
/// will be terminated.
#[must_use = "Dropping `NestedEnvironment` will cause the environment to be terminated."]
pub struct NestedEnvironment {
    controller: EnvironmentControllerProxy,
    launcher: LauncherProxy,
    directory_request: zx::Channel,
}

impl NestedEnvironment {
    /// Returns a reference to the environment's controller.
    #[inline]
    pub fn controller(&self) -> &EnvironmentControllerProxy {
        &self.controller
    }

    /// Returns a reference to the environment's launcher.
    #[inline]
    pub fn launcher(&self) -> &LauncherProxy {
        &self.launcher
    }

    /// Connect to a service provided by this environment.
    #[inline]
    pub fn connect_to_service<S: ServiceMarker>(&self) -> Result<S::Proxy, Error> {
        let (client_channel, server_channel) = zx::Channel::create()?;
        self.pass_to_service::<S>(server_channel)?;
        Ok(S::Proxy::from_channel(fasync::Channel::from_channel(client_channel)?))
    }

    /// Connect to a service by passing a channel for the server.
    #[inline]
    pub fn pass_to_service<S: ServiceMarker>(
        &self,
        server_channel: zx::Channel,
    ) -> Result<(), Error> {
        self.pass_to_named_service(S::NAME, server_channel)
    }

    /// Connect to a service by name.
    #[inline]
    pub fn pass_to_named_service(
        &self,
        service_name: &str,
        server_channel: zx::Channel,
    ) -> Result<(), Error> {
        fdio::service_connect_at(&self.directory_request, service_name, server_channel)?;
        Ok(())
    }
}

enum ConnectionState {
    Open,
    Closed,
}

/// A client connection to `ServiceFs`.
///
/// This type also implements the `Future` trait and resolves to itself
/// when the channel becomes readable.
struct ClientConnection {
    /// The stream of incoming requests. This is always `Some` unless
    /// this `ClientConnection` was used as a `Future` and completed, in
    /// which case it will have given away the stream to the output of
    /// the `Future`.
    stream: Option<DirectoryRequestStream>,

    /// The current node of the `ClientConnection` in the `ServiceFs`
    /// filesystem.
    position: usize,

    /// Buffer and position of current DirectoryRequest::ReadDirents
    dirents_buf: Option<(Vec<u8>, usize)>,
}

impl ClientConnection {
    fn stream(&mut self) -> &mut DirectoryRequestStream {
        self.stream.as_mut().expect("ClientConnection used after `Future` completed")
    }
}

impl Future for ClientConnection {
    type Output = Option<(Result<DirectoryRequest, fidl::Error>, ClientConnection)>;

    fn poll(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        let res_opt = ready!(self.stream().poll_next_unpin(cx));
        Poll::Ready(res_opt.map(|res| {
            (
                res,
                ClientConnection {
                    stream: self.stream.take(),
                    position: self.position,
                    dirents_buf: self.dirents_buf.take(),
                },
            )
        }))
    }
}

/// An error indicating the startup handle on which the FIDL server
/// attempted to start was missing.
#[derive(Debug, Fail)]
#[fail(display = "The startup handle on which the FIDL server attempted to start was missing.")]
pub struct MissingStartupHandle;

fn send_failed_on_open(
    object: ServerEnd<NodeMarker>,
    status: zx::sys::zx_status_t,
) -> Result<(), Error> {
    let (_stream, control_handle) = object
        .into_stream_and_control_handle()
        .context("fail to convert to stream and control handle")?;
    control_handle.send_on_open_(status, None).context("fail sending OnOpenEvent")?;
    Ok(())
}

fn maybe_send_error(
    object: ServerEnd<NodeMarker>,
    flags: u32,
    error: zx::sys::zx_status_t,
) -> Result<(), Error> {
    if (flags & OPEN_FLAG_DESCRIBE) != 0 {
        send_failed_on_open(object, error)?;
    }
    Ok(())
}

fn handle_potentially_unsupported_flags(
    object: ServerEnd<NodeMarker>,
    flags: u32,
    supported_flags_bitmask: u32,
) -> Result<ServerEnd<NodeMarker>, Error> {
    let unsupported_flags = flags & !supported_flags_bitmask;
    if unsupported_flags != 0 {
        maybe_send_error(object, flags, zx::sys::ZX_ERR_NOT_SUPPORTED)?;
        bail!("unsupported flags: {:b}", unsupported_flags);
    } else {
        Ok(object)
    }
}

impl<ServiceObjTy: ServiceObjTrait> ServiceFs<ServiceObjTy> {
    /// Removes the `DirectoryRequest` startup handle for the current
    /// component and adds connects it to this `ServiceFs` as a client.
    ///
    /// Multiple calls to this function from the same component will
    /// result in `Err(MissingStartupHandle)`.
    pub fn take_and_serve_directory_handle(&mut self) -> Result<&mut Self, Error> {
        let startup_handle =
            fuchsia_runtime::take_startup_handle(fuchsia_runtime::HandleType::DirectoryRequest)
                .ok_or(MissingStartupHandle)?;

        self.serve_connection(zx::Channel::from(startup_handle))
    }

    /// Add an additional connection to the `FdioServer` to provide services to.
    pub fn serve_connection(&mut self, chan: zx::Channel) -> Result<&mut Self, Error> {
        self.serve_connection_at(chan, ROOT_NODE, NO_FLAGS)?;
        Ok(self)
    }

    fn serve_connection_at(
        &mut self,
        chan: zx::Channel,
        position: usize,
        flags: u32,
    ) -> Result<(), Error> {
        // It is not an error if the other end of the channel is already
        // closed: the client may call Directory::Open, send a channel chan,
        // then write a request on their local end of chan. If the request
        // does not expect a response (eg. Directory::Open in a subdirectory),
        // the client may close chan immediately afterwards. We should keep
        // our end of the channel until we have processed all incoming requests.
        chan.signal_peer(Signals::NONE, Signals::USER_0)
            .or_else(|e| match e {
                zx::Status::PEER_CLOSED => Ok(()),
                e => Err(e),
            })
            .context("ServiceFs signal_peer failed")?;

        let chan =
            fasync::Channel::from_channel(chan).context("failure to convert to async channel")?;

        let stream = DirectoryRequestStream::from_channel(chan);
        if (flags & OPEN_FLAG_DESCRIBE) != 0 {
            let mut info =
                self.describe_node(position).expect("error serving connection for missing node");
            stream
                .control_handle()
                .send_on_open_(zx::sys::ZX_OK, Some(OutOfLine(&mut info)))
                .context("fail sending OnOpen event")?;
        }

        self.client_connections.push(ClientConnection {
            stream: Some(stream),
            position,
            dirents_buf: None,
        });
        Ok(())
    }

    fn handle_request(
        &mut self,
        request: DirectoryRequest,
        connection: &mut ClientConnection,
    ) -> Result<(Option<ServiceObjTy::Output>, ConnectionState), Error> {
        assert!(self.nodes.len() > connection.position);

        macro_rules! unsupported {
            ($responder:ident $($args:tt)*) => {
                $responder.send(zx::sys::ZX_ERR_NOT_SUPPORTED $($args)*)
            }
        }

        match request {
            DirectoryRequest::Clone { flags, object, control_handle: _ } => {
                let object =
                    handle_potentially_unsupported_flags(object, flags, CLONE_REQ_SUPPORTED_FLAGS)?;

                if let Err(e) =
                    self.serve_connection_at(object.into_channel(), connection.position, flags)
                {
                    eprintln!("ServiceFs failed to clone: {:?}", e);
                }
            }
            DirectoryRequest::Close { responder } => {
                responder.send(zx::sys::ZX_OK)?;
                return Ok((None, ConnectionState::Closed));
            }
            DirectoryRequest::Open { flags, mode: _, path, object, control_handle: _ } => {
                let object =
                    handle_potentially_unsupported_flags(object, flags, OPEN_REQ_SUPPORTED_FLAGS)?;

                if path == "." {
                    if let Err(e) =
                        self.serve_connection_at(object.into_channel(), connection.position, flags)
                    {
                        eprintln!("ServiceFS failed to open '.': {:?}", e);
                    }
                    return Ok((None, ConnectionState::Open));
                } else if path == "" {
                    maybe_send_error(object, flags, zx::sys::ZX_ERR_BAD_PATH)?;
                    return Ok((None, ConnectionState::Open));
                }

                let mut segments = path.rsplitn(2, "/");
                let end_segment = segments.next().unwrap();
                let directory_segment = segments.next();
                let children = match self.descend(connection.position, directory_segment) {
                    Ok(children) => children,
                    Err(e) => {
                        eprintln!("invalid path {} - {}", path, e);
                        maybe_send_error(object, flags, zx::sys::ZX_ERR_BAD_PATH)?;
                        return Ok((None, ConnectionState::Open));
                    }
                };

                if let Some(&next_node_pos) = children.get(end_segment) {
                    match self.nodes.get_mut(next_node_pos).expect("Missing child node") {
                        ServiceFsNode::Directory { .. } => {
                            if let Err(e) = self.serve_connection_at(
                                object.into_channel(),
                                next_node_pos,
                                flags,
                            ) {
                                eprintln!("ServiceFs failed to open directory: {:?}", e);
                            }
                        }
                        ServiceFsNode::Service(service) => {
                            if flags & OPEN_FLAG_DIRECTORY != 0 {
                                send_failed_on_open(object, zx::sys::ZX_ERR_NOT_DIR)?;
                                return Ok((None, ConnectionState::Open));
                            }
                            // Case 1: client opens node to get more metadata on it (by calling
                            //         `Describe` or `GetAttr` later), so there's no need to
                            //         connect to service. This is done by `find` command.
                            if flags & OPEN_FLAG_NODE_REFERENCE != 0
                                || flags & OPEN_FLAG_DESCRIBE != 0
                            {
                                if let Err(e) = self.serve_connection_at(
                                    object.into_channel(),
                                    next_node_pos,
                                    flags,
                                ) {
                                    eprintln!("ServiceFs failed to open service node: {:?}", e);
                                }
                                return Ok((None, ConnectionState::Open));
                            }
                            // Case 2: client opens node to connect to service
                            return Ok((
                                service.service().connect(object.into_channel()),
                                ConnectionState::Open,
                            ));
                        }
                    }
                } else {
                    maybe_send_error(object, flags, zx::sys::ZX_ERR_NOT_FOUND)?;
                    return Ok((None, ConnectionState::Open));
                }
            }
            DirectoryRequest::Describe { responder } => {
                let mut info =
                    self.describe_node(connection.position).expect("node missing for Describe req");
                responder.send(&mut info)?;
            }
            DirectoryRequest::GetAttr { responder } => {
                let node =
                    self.nodes.get(connection.position).expect("node missing for GetAttr req");
                let mode_type = match node {
                    ServiceFsNode::Directory { .. } => fidl_fuchsia_io::MODE_TYPE_DIRECTORY,
                    ServiceFsNode::Service(..) => fidl_fuchsia_io::MODE_TYPE_SERVICE,
                };

                let mut attrs = NodeAttributes {
                    mode: mode_type | 0o400, /* mode R_USR */
                    id: fidl_fuchsia_io::INO_UNKNOWN,
                    content_size: 0,
                    storage_size: 0,
                    link_count: 1,
                    creation_time: 0,
                    modification_time: 0,
                };
                responder.send(zx::sys::ZX_OK, &mut attrs)?
            }
            DirectoryRequest::SetAttr { responder, .. } => unsupported!(responder)?,
            DirectoryRequest::Ioctl { responder, .. } => {
                unsupported!(responder, &mut std::iter::empty(), &mut std::iter::empty())?
            }
            DirectoryRequest::Sync { responder } => unsupported!(responder)?,
            DirectoryRequest::Unlink { responder, .. } => unsupported!(responder)?,
            DirectoryRequest::ReadDirents { max_bytes, responder } => {
                let children = self.open_dir(connection.position)?;

                let dirents_buf = connection
                    .dirents_buf
                    .get_or_insert_with(|| (self.to_dirent_bytes(&children), 0));
                let (dirents_buf, offset) = (&mut dirents_buf.0, &mut dirents_buf.1);
                if *offset >= dirents_buf.len() {
                    responder.send(zx::sys::ZX_OK, &mut std::iter::empty())?;
                } else {
                    let new_offset = std::cmp::min(dirents_buf.len(), *offset + max_bytes as usize);
                    responder.send(
                        zx::sys::ZX_OK,
                        &mut dirents_buf[*offset..new_offset].iter().cloned(),
                    )?;
                    *offset = new_offset;
                }
            }
            DirectoryRequest::Rewind { responder } => {
                connection.dirents_buf = None;
                responder.send(zx::sys::ZX_OK)?;
            }
            DirectoryRequest::GetToken { responder } => unsupported!(responder, None)?,
            DirectoryRequest::Rename { responder, .. } => unsupported!(responder)?,
            DirectoryRequest::Link { responder, .. } => unsupported!(responder)?,
            DirectoryRequest::Watch { responder, .. } => unsupported!(responder)?,
        }
        Ok((None, ConnectionState::Open))
    }

    fn describe_node(&self, pos: usize) -> Option<NodeInfo> {
        self.nodes.get(pos).map(|node| match node {
            ServiceFsNode::Directory { .. } => NodeInfo::Directory(DirectoryObject),
            ServiceFsNode::Service(..) => NodeInfo::Service(fidl_fuchsia_io::Service),
        })
    }

    /// Retrieve directory listing at |path| starting from node |start_pos|. If |path| is None,
    /// simply return directory listing of node |start_pos|.
    fn descend(
        &self,
        start_pos: usize,
        path: Option<&str>,
    ) -> Result<&HashMap<String, usize>, Error> {
        let mut pos = start_pos;
        let mut children = self.open_dir(pos)?;

        if let Some(path) = path {
            for segment in path.split("/") {
                match children.get(segment) {
                    Some(next_pos) => pos = *next_pos,
                    _ => bail!("segment not found: {}", segment),
                }
                children =
                    self.open_dir(pos).context(format!("cannot open segment {}", segment))?;
            }
        }
        Ok(children)
    }

    /// Retrieve directory listing of node |pos|. Return an error if |pos| is not a directory
    /// node
    fn open_dir(&self, pos: usize) -> Result<&HashMap<String, usize>, Error> {
        let node = self.nodes.get(pos).expect(&format!("missing node {}", pos));
        match node {
            ServiceFsNode::Directory { children } => Ok(children),
            _ => bail!("node not a directory: {}", pos),
        }
    }

    fn to_dirent_bytes(&self, nodes: &HashMap<String, usize>) -> Vec<u8> {
        let mut buf = vec![];
        for (name, node) in nodes.iter() {
            let typ = self.nodes.get(*node).expect("missing child").to_dirent_type();
            if let Err(e) = write_dirent_bytes(&mut buf, *node as u64, typ, name) {
                eprintln!("failed encoding dirent for node {}: {}", *node, e);
            }
        }
        buf
    }
}

fn write_dirent_bytes(buf: &mut Vec<u8>, ino: u64, typ: u8, name: &str) -> Result<(), Error> {
    // Safe to unwrap since `Write::write` on a `Vec` should never fail.
    buf.write_u64::<LittleEndian>(ino).unwrap();
    buf.write_u8(name.len() as u8).unwrap();
    buf.write_u8(typ as u8).unwrap();
    buf.write(name.as_ref()).unwrap();
    Ok(())
}

impl<ServiceObjTy: ServiceObjTrait> Unpin for ServiceFs<ServiceObjTy> {}

impl<ServiceObjTy: ServiceObjTrait> Stream for ServiceFs<ServiceObjTy> {
    type Item = ServiceObjTy::Output;

    fn poll_next(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        loop {
            let (request, mut client_connection) =
                match ready!(self.client_connections.poll_next_unpin(cx)) {
                    // a client request
                    Some(Some(x)) => x,
                    // this client_connection has terminated
                    Some(None) => continue,
                    // all client connections have terminated
                    None => return Poll::Ready(None),
                };
            let request = match request {
                Ok(request) => request,
                Err(e) => {
                    eprintln!("ServiceFs failed to parse an incoming request: {:?}", e);
                    continue;
                }
            };
            match self.handle_request(request, &mut client_connection) {
                Ok((value, connection_state)) => {
                    if let ConnectionState::Open = connection_state {
                        // Requeue the client to receive new requests
                        self.client_connections.push(client_connection);
                    }
                    if let Some(value) = value {
                        return Poll::Ready(Some(value));
                    }
                }
                Err(e) => eprintln!("ServiceFs failed to handle an incoming request: {:?}", e),
            }
        }
    }
}

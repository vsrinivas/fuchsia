// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Tools for providing Fuchsia services.

use {
    crate::DEFAULT_SERVICE_INSTANCE,
    anyhow::{format_err, Context as _, Error},
    byteorder::{LittleEndian, WriteBytesExt as _},
    fidl::endpoints::{
        DiscoverableService, Proxy as _, RequestStream, ServerEnd, UnifiedServiceMarker,
        UnifiedServiceRequest,
    },
    fidl_fuchsia_io::{
        DirectoryObject, DirectoryProxy, DirectoryRequest, DirectoryRequestStream, FileRequest,
        FileRequestStream, NodeAttributes, NodeInfo, NodeMarker, NodeRequest, NodeRequestStream,
        SeekOrigin, CLONE_FLAG_SAME_RIGHTS, OPEN_FLAG_DESCRIBE, OPEN_FLAG_DIRECTORY,
        OPEN_FLAG_NODE_REFERENCE, OPEN_FLAG_NOT_DIRECTORY, OPEN_FLAG_POSIX, OPEN_RIGHT_READABLE,
        OPEN_RIGHT_WRITABLE,
    },
    fidl_fuchsia_sys::{
        EnvironmentControllerProxy, EnvironmentMarker, EnvironmentOptions, LauncherProxy,
        LoaderMarker, ServiceList,
    },
    fuchsia_async as fasync,
    fuchsia_zircon::{self as zx, HandleBased as _, Peered, Signals},
    futures::{
        stream::{FuturesUnordered, StreamExt},
        Stream,
    },
    std::{
        cmp::min,
        collections::hash_map::{Entry, HashMap},
        io::Write,
        marker::{PhantomData, Unpin},
        pin::Pin,
        sync::Arc,
        task::{Context, Poll},
    },
    thiserror::Error,
};

mod service;
pub use service::{
    FidlService, FidlServiceMember, Service, ServiceObj, ServiceObjLocal, ServiceObjTrait,
};

mod stream_helpers;
use stream_helpers::NextWith;

enum Directory {
    Local { children: HashMap<String, usize> },
    Remote(DirectoryProxy),
}

enum ServiceFsNode<ServiceObjTy: ServiceObjTrait> {
    Directory(Directory),
    Service(ServiceObjTy),
    VmoFile { vmo: zx::Vmo, offset: u64, length: u64 },
}

const NOT_A_DIR: &str = "ServiceFs expected directory";

impl<ServiceObjTy: ServiceObjTrait> ServiceFsNode<ServiceObjTy> {
    fn is_service(&self) -> bool {
        match self {
            ServiceFsNode::Service(_) => true,
            _ => false,
        }
    }
    fn is_directory(&self) -> bool {
        match self {
            ServiceFsNode::Directory(_) => true,
            _ => false,
        }
    }

    fn expect_dir(&self) -> &HashMap<String, usize> {
        if let ServiceFsNode::Directory(Directory::Local { children }) = self {
            children
        } else {
            panic!(NOT_A_DIR)
        }
    }

    fn expect_dir_mut(&mut self) -> &mut HashMap<String, usize> {
        if let ServiceFsNode::Directory(Directory::Local { children }) = self {
            children
        } else {
            panic!(NOT_A_DIR)
        }
    }

    fn to_dirent_type(&self) -> u8 {
        match self {
            ServiceFsNode::Directory(_) => fidl_fuchsia_io::DIRENT_TYPE_DIRECTORY,
            ServiceFsNode::Service(_) => fidl_fuchsia_io::DIRENT_TYPE_SERVICE,
            ServiceFsNode::VmoFile { .. } => fidl_fuchsia_io::DIRENT_TYPE_FILE,
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
    /// Open connections to `ServiceFs` directories.
    dir_connections: FuturesUnordered<DirConnection>,

    /// Open connections to `ServiceFs` files.
    file_connections: FuturesUnordered<FileConnection>,

    /// Open connections to `ServiceFs` nodes (directories, files, or service).
    node_connections: FuturesUnordered<NodeConnection>,

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
const CLONE_REQ_SUPPORTED_FLAGS: u32 = OPEN_RIGHT_READABLE
    | OPEN_RIGHT_WRITABLE
    | OPEN_FLAG_DESCRIBE
    | CLONE_FLAG_SAME_RIGHTS
    | OPEN_FLAG_DIRECTORY;
const OPEN_REQ_SUPPORTED_FLAGS: u32 = OPEN_RIGHT_READABLE
    | OPEN_RIGHT_WRITABLE
    | OPEN_FLAG_DESCRIBE
    | OPEN_FLAG_POSIX
    | OPEN_FLAG_DIRECTORY
    | OPEN_FLAG_NOT_DIRECTORY
    | OPEN_FLAG_NODE_REFERENCE;

impl<'a, Output: 'a> ServiceFs<ServiceObjLocal<'a, Output>> {
    /// Create a new `ServiceFs` that is singlethreaded-only and does not
    /// require services to implement `Send`.
    pub fn new_local() -> Self {
        Self {
            dir_connections: FuturesUnordered::new(),
            file_connections: FuturesUnordered::new(),
            node_connections: FuturesUnordered::new(),
            nodes: vec![ServiceFsNode::Directory(Directory::Local { children: HashMap::new() })],
        }
    }
}

impl<'a, Output: 'a> ServiceFs<ServiceObj<'a, Output>> {
    /// Create a new `ServiceFs` that is multithreaded-capable and requires
    /// services to implement `Send`.
    pub fn new() -> Self {
        Self {
            dir_connections: FuturesUnordered::new(),
            file_connections: FuturesUnordered::new(),
            node_connections: FuturesUnordered::new(),
            nodes: vec![ServiceFsNode::Directory(Directory::Local { children: HashMap::new() })],
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

/// Finds the position at which a new directory should be added.
fn find_dir_insert_position<ServiceObjTy: ServiceObjTrait>(
    fs: &mut ServiceFs<ServiceObjTy>,
    position: usize,
    path: String,
) -> usize {
    let new_node_position = fs.nodes.len();
    let self_dir = fs.nodes[position].expect_dir_mut();
    let &mut position = self_dir.entry(path.clone()).or_insert(new_node_position);
    if position != new_node_position {
        if let ServiceFsNode::Service(_) = &fs.nodes[position] {
            panic!("Error adding dir to ServiceFs: existing service at \"{}\"", path)
        }
    }
    position
}

fn dir<'a, ServiceObjTy: ServiceObjTrait>(
    fs: &'a mut ServiceFs<ServiceObjTy>,
    position: usize,
    path: String,
) -> ServiceFsDir<'a, ServiceObjTy> {
    let new_node_position = fs.nodes.len();
    let position = find_dir_insert_position(fs, position, path);
    if position == new_node_position {
        fs.nodes.push(ServiceFsNode::Directory(Directory::Local { children: HashMap::new() }));
    }
    ServiceFsDir { position, fs }
}

fn remote<'a, ServiceObjTy: ServiceObjTrait>(
    fs: &'a mut ServiceFs<ServiceObjTy>,
    position: usize,
    name: String,
    proxy: DirectoryProxy,
) {
    let new_node_position = fs.nodes.len();
    let position = find_dir_insert_position(fs, position, name);
    if position == new_node_position {
        fs.nodes.push(ServiceFsNode::Directory(Directory::Remote(proxy)));
    }
}

fn add_entry<ServiceObjTy: ServiceObjTrait>(
    fs: &mut ServiceFs<ServiceObjTy>,
    position: usize,
    path: String,
    entry: ServiceFsNode<ServiceObjTy>,
) {
    let new_node_position = fs.nodes.len();
    let self_dir = fs.nodes[position].expect_dir_mut();
    let map_entry = self_dir.entry(path);
    match map_entry {
        Entry::Occupied(prev) => {
            panic!("Duplicate ServiceFs entry added at path \"{}\"", prev.key())
        }
        Entry::Vacant(slot) => {
            slot.insert(new_node_position);
            fs.nodes.push(entry);
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

impl<S: DiscoverableService, O> Service for Proxy<S, O> {
    type Output = O;
    fn connect(&mut self, channel: zx::Channel) -> Option<O> {
        if let Err(e) = crate::client::connect_channel_to_service::<S>(channel) {
            eprintln!("failed to proxy request to {}: {:?}", S::SERVICE_NAME, e);
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

impl<S: DiscoverableService, O> Service for ProxyTo<S, O> {
    type Output = O;
    fn connect(&mut self, channel: zx::Channel) -> Option<O> {
        if let Err(e) = fdio::service_connect_at(&self.directory_request, S::SERVICE_NAME, channel)
        {
            eprintln!("failed to proxy request to {}: {:?}", S::SERVICE_NAME, e);
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
pub struct ComponentProxy<S: DiscoverableService, O> {
    launch_data: Option<LaunchData>,
    launched_app: Option<crate::client::App>,
    _marker: PhantomData<(S, O)>,
}

impl<S: DiscoverableService, O> Service for ComponentProxy<S, O> {
    type Output = O;
    fn connect(&mut self, channel: zx::Channel) -> Option<Self::Output> {
        let res = (|| {
            if let Some(LaunchData { component_url, arguments }) = self.launch_data.take() {
                self.launched_app = Some(crate::client::launch(
                    &crate::client::launcher()?,
                    component_url,
                    arguments,
                )?);
            }
            if let Some(app) = self.launched_app.as_ref() {
                app.pass_to_named_service(S::SERVICE_NAME, channel.into())?;
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
macro_rules! add_functions {
    () => {
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
            self.add_entry_at(path.into(), ServiceFsNode::Service(service.into()))
        }

        /// Adds a FIDL service to the directory.
        ///
        /// `service` is a closure that accepts a `RequestStream`.
        /// Each service being served must return an instance of the same type
        /// (`ServiceObjTy::Output`). This is necessary in order to multiplex
        /// multiple services over the same dispatcher code. The typical way
        /// to do this is to create an `enum` with variants for each service
        /// you want to serve.
        ///
        /// ```ignore
        /// enum MyServices {
        ///     EchoServer(EchoRequestStream),
        ///     CustomServer(CustomRequestStream),
        ///     // ...
        /// }
        /// ```
        ///
        /// The constructor for a variant of the `MyServices` enum can be passed
        /// as the `service` parameter.
        ///
        /// ```ignore
        /// let mut fs = ServiceFs::new_local();
        /// fs
        ///     .add_fidl_service(MyServices::EchoServer)
        ///     .add_fidl_service(MyServices::CustomServer)
        ///     .take_and_serve_directory_handle()?;
        /// ```
        ///
        /// `ServiceFs` can now be treated as a `Stream` of type `MyServices`.
        ///
        /// ```ignore
        /// const MAX_CONCURRENT: usize = 10_000;
        /// fs.for_each_concurrent(MAX_CONCURRENT, |request: MyServices| {
        ///     match request {
        ///         MyServices::EchoServer(request) => handle_echo(request),
        ///         MyServices::CustomServer(request) => handle_custom(request),
        ///     }
        /// }).await;
        /// ```
        ///
        /// The FIDL service will be hosted at the name provided by the
        /// `[Discoverable]` annotation in the FIDL source.
        pub fn add_fidl_service<F, RS>(&mut self, service: F) -> &mut Self
        where
            F: FnMut(RS) -> ServiceObjTy::Output,
            RS: RequestStream,
            RS::Service: DiscoverableService,
            FidlService<F, RS, ServiceObjTy::Output>: Into<ServiceObjTy>,
        {
            self.add_fidl_service_at(RS::Service::SERVICE_NAME, service)
        }

        /// Adds a FIDL service to the directory at the given path.
        ///
        /// The path must be a single component containing no `/` characters.
        ///
        /// See [`add_fidl_service`](#method.add_fidl_service) for details.
        pub fn add_fidl_service_at<F, RS>(
            &mut self,
            path: impl Into<String>,
            service: F,
        ) -> &mut Self
        where
            F: FnMut(RS) -> ServiceObjTy::Output,
            RS: RequestStream,
            RS::Service: DiscoverableService,
            FidlService<F, RS, ServiceObjTy::Output>: Into<ServiceObjTy>,
        {
            self.add_service_at(path, FidlService::from(service))
        }

        /// Adds a FIDL Unified Service to the directory as the default instance.
        ///
        /// The name of the default instance is
        /// [`DEFAULT_SERVICE_INSTANCE`](../constant.DEFAULT_SERVICE_INSTANCE.html).
        ///
        /// The FIDL service will be hosted at `[SERVICE_NAME]/[default]/` where `SERVICE_NAME` is
        /// constructed from the FIDL library path and the name of the FIDL service.
        ///
        /// # Example
        ///
        /// For the following FIDL definition,
        /// ```fidl
        /// library lib.foo;
        ///
        /// service Bar {
        ///   ...
        /// }
        /// ```
        ///
        /// The `SERVICE_NAME` of FIDL Service `Bar` would be `lib.foo.Bar`.
        pub fn add_unified_service<F, USR>(&mut self, service: F) -> &mut Self
        where
            F: Fn(USR) -> ServiceObjTy::Output,
            F: Clone,
            USR: UnifiedServiceRequest,
            FidlServiceMember<F, USR, ServiceObjTy::Output>: Into<ServiceObjTy>,
        {
            self.add_unified_service_at(USR::Service::SERVICE_NAME, service)
        }

        /// Adds a FIDL Unified Service to the directory as the default instance at the given path.
        ///
        /// The path must be a single component containing no `/` characters.
        /// The name of the default instance is
        /// [`DEFAULT_SERVICE_INSTANCE`](../constant.DEFAULT_SERVICE_INSTANCE.html).
        ///
        /// The FIDL service will be hosted at `[path]/default/`.
        pub fn add_unified_service_at<F, USR>(
            &mut self,
            path: impl Into<String>,
            service: F,
        ) -> &mut Self
        where
            F: Fn(USR) -> ServiceObjTy::Output,
            F: Clone,
            USR: UnifiedServiceRequest,
            FidlServiceMember<F, USR, ServiceObjTy::Output>: Into<ServiceObjTy>,
        {
            self.add_unified_service_instance_at(path, DEFAULT_SERVICE_INSTANCE, service)
        }

        /// Adds a named instance of a FIDL Unified Service to the directory.
        ///
        /// The FIDL service will be hosted at `[SERVICE_NAME]/[instance]/` where `SERVICE_NAME` is
        /// constructed from the FIDL library path and the name of the FIDL service.
        ///
        /// The `instance` must be a single component containing no `/` characters.
        ///
        /// # Example
        ///
        /// For the following FIDL definition,
        /// ```fidl
        /// library lib.foo;
        ///
        /// service Bar {
        ///   ...
        /// }
        /// ```
        ///
        /// The `SERVICE_NAME` of FIDL Service `Bar` would be `lib.foo.Bar`.
        pub fn add_unified_service_instance<F, USR>(
            &mut self,
            instance: impl Into<String>,
            service: F,
        ) -> &mut Self
        where
            F: Fn(USR) -> ServiceObjTy::Output,
            F: Clone,
            USR: UnifiedServiceRequest,
            FidlServiceMember<F, USR, ServiceObjTy::Output>: Into<ServiceObjTy>,
        {
            self.add_unified_service_instance_at(USR::Service::SERVICE_NAME, instance, service)
        }

        /// Adds a named instance of a FIDL Unified Service to the directory at the given path.
        ///
        /// The FIDL service will be hosted at `[path]/[instance]/`.
        ///
        /// The `path` and `instance` must be single components containing no `/` characters.
        pub fn add_unified_service_instance_at<F, USR>(
            &mut self,
            path: impl Into<String>,
            instance: impl Into<String>,
            service: F,
        ) -> &mut Self
        where
            F: Fn(USR) -> ServiceObjTy::Output,
            F: Clone,
            USR: UnifiedServiceRequest,
            FidlServiceMember<F, USR, ServiceObjTy::Output>: Into<ServiceObjTy>,
        {
            // Create the service directory, with an instance subdirectory.
            let mut dir = self.dir(path);
            let mut dir = dir.dir(instance);

            // Attach member protocols under the instance directory.
            for member in USR::member_names() {
                dir.add_service_at(*member, FidlServiceMember::from(service.clone()));
            }
            self
        }

        /// Adds a service that proxies requests to the current environment.
        // NOTE: we'd like to be able to remove the type parameter `O` here,
        //  but unfortunately the bound `ServiceObjTy: From<Proxy<S, ServiceObjTy::Output>>`
        //  makes type checking angry.
        pub fn add_proxy_service<S: DiscoverableService, O>(&mut self) -> &mut Self
        where
            ServiceObjTy: From<Proxy<S, O>>,
            ServiceObjTy: ServiceObjTrait<Output = O>,
        {
            self.add_service_at(S::SERVICE_NAME, Proxy::<S, ServiceObjTy::Output>(PhantomData))
        }

        /// Adds a service that proxies requests to the given component.
        // NOTE: we'd like to be able to remove the type parameter `O` here,
        //  but unfortunately the bound `ServiceObjTy: From<Proxy<S, ServiceObjTy::Output>>`
        //  makes type checking angry.
        pub fn add_proxy_service_to<S: DiscoverableService, O>(
            &mut self,
            directory_request: Arc<zx::Channel>,
        ) -> &mut Self
        where
            ServiceObjTy: From<ProxyTo<S, O>>,
            ServiceObjTy: ServiceObjTrait<Output = O>,
        {
            self.add_service_at(
                S::SERVICE_NAME,
                ProxyTo::<S, ServiceObjTy::Output> { directory_request, _phantom: PhantomData },
            )
        }

        /// Add a service to the `ServicesServer` that will launch a component
        /// upon request, proxying requests to the launched component.
        pub fn add_component_proxy_service<S: DiscoverableService, O>(
            &mut self,
            component_url: String,
            arguments: Option<Vec<String>>,
        ) -> &mut Self
        where
            ServiceObjTy: From<ComponentProxy<S, O>>,
            ServiceObjTy: ServiceObjTrait<Output = O>,
        {
            self.add_service_at(
                S::SERVICE_NAME,
                ComponentProxy {
                    launch_data: Some(LaunchData { component_url, arguments }),
                    launched_app: None,
                    _marker: PhantomData,
                },
            )
        }

        /// Adds a VMO file to the directory at the given path.
        ///
        /// The path must be a single component containing no `/` characters.
        ///
        /// Panics if any node has already been added at the given path.
        pub fn add_vmo_file_at(
            &mut self,
            path: impl Into<String>,
            vmo: zx::Vmo,
            offset: u64,
            length: u64,
        ) -> &mut Self {
            self.add_entry_at(path.into(), ServiceFsNode::VmoFile { vmo, offset, length })
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

    /// Adds a new remote directory served over the given DirectoryProxy.
    ///
    /// The name must not contain any '/' characters.
    pub fn add_remote(&mut self, name: impl Into<String>, proxy: DirectoryProxy) {
        remote(self.fs, self.position, name.into(), proxy)
    }

    fn add_entry_at(&mut self, path: String, entry: ServiceFsNode<ServiceObjTy>) -> &mut Self {
        add_entry(self.fs, self.position, path, entry);
        self
    }

    add_functions!();
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

    /// Get a reference to the root directory as a `ServiceFsDir`.
    ///
    /// This can be useful when writing code which hosts some set of services on
    /// a directory and wants to be agnostic to whether that directory
    /// is the root `ServiceFs` or a subdirectory.
    ///
    /// Such a function can take an `&mut ServiceFsDir<...>` as an argument,
    /// allowing callers to provide either a subdirectory or `fs.root_dir()`.
    pub fn root_dir<'a>(&'a mut self) -> ServiceFsDir<'a, ServiceObjTy> {
        ServiceFsDir { position: ROOT_NODE, fs: self }
    }

    /// Adds a new remote directory served over the given DirectoryProxy.
    ///
    /// The name must not contain any '/' characters.
    pub fn add_remote(&mut self, name: impl Into<String>, proxy: DirectoryProxy) {
        remote(self, ROOT_NODE, name.into(), proxy)
    }

    fn add_entry_at(&mut self, path: String, entry: ServiceFsNode<ServiceObjTy>) -> &mut Self {
        add_entry(self, ROOT_NODE, path, entry);
        self
    }

    add_functions!();

    /// Start serving directory protocol service requests via a `ServiceList`.
    /// The resulting `ServiceList` can be attached to a new environment in
    /// order to provide child components with access to these services.
    pub fn host_services_list(&mut self) -> Result<ServiceList, Error> {
        let names = self.nodes[ROOT_NODE]
            .expect_dir()
            .iter()
            .filter(|(_, v)| self.nodes[**v].is_service())
            .map(|(k, _)| k)
            .cloned()
            .collect();

        let (chan1, chan2) = zx::Channel::create()?;
        self.serve_connection(chan1)?;

        Ok(ServiceList { names, provider: None, host_directory: Some(chan2) })
    }

    /// Returns the list of all directories in the root directory
    fn root_directories_list(&self) -> Vec<String> {
        self.nodes[ROOT_NODE]
            .expect_dir()
            .iter()
            .filter(|(_, v)| self.nodes[**v].is_directory())
            .map(|(k, _)| k)
            .cloned()
            .collect()
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
        self.create_nested_environment_with_options(
            environment_label,
            EnvironmentOptions {
                inherit_parent_services: false,
                use_parent_runners: false,
                kill_on_oom: false,
                delete_storage_on_death: false,
            },
        )
    }

    /// Creates a new environment, with custom options, that only has access to the services
    /// provided through this `ServiceFs` and the enclosing environment's `Loader` service.
    ///
    /// Note that the resulting `NestedEnvironment` must be kept alive for the environment to
    /// continue to exist. Once dropped, the environment and all components launched within it
    /// will be destroyed.
    pub fn create_nested_environment_with_options<O>(
        &mut self,
        environment_label: &str,
        mut options: EnvironmentOptions,
    ) -> Result<NestedEnvironment, Error>
    where
        ServiceObjTy: From<Proxy<LoaderMarker, O>>,
        ServiceObjTy: ServiceObjTrait<Output = O>,
    {
        let env = crate::client::connect_to_service::<EnvironmentMarker>()
            .context("connecting to current environment")?;
        let services_with_loader = self.add_proxy_service::<LoaderMarker, _>();

        // Services added in any subdirectories won't be provided to the nested environment, which
        // is an important detail that developers are likely to overlook. If there are any
        // subdirectories in this ServiceFs, return an error, because what we're about to do
        // probably doesn't line up with what the developer expects.
        if !services_with_loader.root_directories_list().is_empty() {
            return Err(format_err!(
                "services in sub-directories will not be added to nested environment"
            ));
        }

        let mut service_list = services_with_loader.host_services_list()?;

        let (new_env, new_env_server_end) = fidl::endpoints::create_proxy()?;
        let (controller, controller_server_end) = fidl::endpoints::create_proxy()?;
        let (launcher, launcher_server_end) = fidl::endpoints::create_proxy()?;
        let (directory_request, directory_server_end) = zx::Channel::create()?;

        env.create_nested_environment(
            new_env_server_end,
            controller_server_end,
            environment_label,
            Some(&mut service_list),
            &mut options,
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
        let (new_env_controller, app) = self.launch_component_in_nested_environment_with_options(
            url,
            arguments,
            crate::client::LaunchOptions::new(),
            environment_label,
        )?;
        Ok((new_env_controller, app))
    }

    /// Starts a new component inside an isolated environment with custom launch
    /// options, see the comment for |launch_component_in_nested_environment()|
    /// above.
    pub fn launch_component_in_nested_environment_with_options<O>(
        &mut self,
        url: String,
        arguments: Option<Vec<String>>,
        options: crate::client::LaunchOptions,
        environment_label: &str,
    ) -> Result<(EnvironmentControllerProxy, crate::client::App), Error>
    where
        ServiceObjTy: From<Proxy<LoaderMarker, O>>,
        ServiceObjTy: ServiceObjTrait<Output = O>,
    {
        let NestedEnvironment { controller, launcher, directory_request: _ } =
            self.create_nested_environment(environment_label)?;

        let app = crate::client::launch_with_options(&launcher, url, arguments, options)?;
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
    pub fn connect_to_service<S: DiscoverableService>(&self) -> Result<S::Proxy, Error> {
        let (client_channel, server_channel) = zx::Channel::create()?;
        self.pass_to_service::<S>(server_channel)?;
        Ok(S::Proxy::from_channel(fasync::Channel::from_channel(client_channel)?))
    }

    /// Connect to a service by passing a channel for the server.
    #[inline]
    pub fn pass_to_service<S: DiscoverableService>(
        &self,
        server_channel: zx::Channel,
    ) -> Result<(), Error> {
        self.pass_to_named_service(S::SERVICE_NAME, server_channel)
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

/// A client connection to a directory of `ServiceFs`.
type DirConnection = NextWith<DirectoryRequestStream, DirConnectionData>;

struct DirConnectionData {
    /// The current node of the `DirConnection` in the `ServiceFs`
    /// filesystem.
    position: usize,

    /// Buffer and position of current DirectoryRequest::ReadDirents
    dirents_buf: Option<(Vec<u8>, usize)>,
}

/// A client connection to a file in `ServiceFs`.
type FileConnection = NextWith<FileRequestStream, FileConnectionData>;

struct FileConnectionData {
    position: usize,
    seek_offset: u64,
}

/// A client connection to any node in `ServiceFs`.
type NodeConnection = NextWith<NodeRequestStream, NodeConnectionData>;

struct NodeConnectionData {
    position: usize,
}

/// An error indicating the startup handle on which the FIDL server
/// attempted to start was missing.
#[derive(Debug, Error)]
#[error("The startup handle on which the FIDL server attempted to start was missing.")]
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
        return Err(format_err!("unsupported flags: {:b}", unsupported_flags));
    } else {
        Ok(object)
    }
}

macro_rules! unsupported {
    ($responder:ident $($args:tt)*) => {
        $responder.send(zx::sys::ZX_ERR_NOT_SUPPORTED $($args)*)
    }
}

// Can't be a single function because DirectoryRequestStream,
// FileRequestStream, and NodeRequestStream don't have matching types, even though their
// function signatures are identical.
macro_rules! send_info_fn {
    ($(($name:ident, $stream:ty),)*) => { $(
        fn $name(stream: &$stream, info: Option<NodeInfo>) -> Result<(), Error> {
            if let Some(mut info) = info {
                stream
                    .control_handle()
                    .send_on_open_(zx::sys::ZX_OK, Some(&mut info))
                    .context("fail sending OnOpen event")?;
            }
            Ok(())
        }
    )* }
}

#[rustfmt::skip]
send_info_fn![
    (send_info_dir, DirectoryRequestStream),
    (send_info_file, FileRequestStream),
    (send_info_node, NodeRequestStream),
];

fn into_async(chan: zx::Channel) -> Result<fasync::Channel, Error> {
    Ok(fasync::Channel::from_channel(chan).context("failure to convert to async channel")?)
}

#[derive(Debug)]
enum DescendResult<'a> {
    LocalChildren(&'a HashMap<String, usize>),
    RemoteDir((&'a DirectoryProxy, String)),
}

impl<ServiceObjTy: ServiceObjTrait> ServiceFs<ServiceObjTy> {
    /// Removes the `DirectoryRequest` startup handle for the current
    /// component and adds connects it to this `ServiceFs` as a client.
    ///
    /// Multiple calls to this function from the same component will
    /// result in `Err(MissingStartupHandle)`.
    pub fn take_and_serve_directory_handle(&mut self) -> Result<&mut Self, Error> {
        let startup_handle = fuchsia_runtime::take_startup_handle(
            fuchsia_runtime::HandleType::DirectoryRequest.into(),
        )
        .ok_or(MissingStartupHandle)?;

        self.serve_connection(zx::Channel::from(startup_handle))
    }

    /// Add an additional connection to the `ServiceFs` to provide services to.
    pub fn serve_connection(&mut self, chan: zx::Channel) -> Result<&mut Self, Error> {
        match self.serve_connection_at(chan.into(), ROOT_NODE, None, NO_FLAGS)? {
            Some(_) => panic!("root directory connection should not return output"),
            None => {}
        }
        Ok(self)
    }

    /// Serve a connection at a specific node.
    fn serve_connection_at(
        &mut self,
        object: ServerEnd<NodeMarker>,
        position: usize,
        name: Option<&str>,
        flags: u32,
    ) -> Result<Option<ServiceObjTy::Output>, Error> {
        let node = &self.nodes[position];

        // Forward requests for a remote directory to that directory.
        match node {
            ServiceFsNode::Directory(Directory::Remote(proxy)) => {
                proxy.clone(flags, object)?;
                return Ok(None);
            }
            _ => {}
        }

        // It is not an error if the other end of the channel is already
        // closed: the client may call Directory::Open, send a channel chan,
        // then write a request on their local end of chan. If the request
        // does not expect a response (eg. Directory::Open in a subdirectory),
        // the client may close chan immediately afterwards. We should keep
        // our end of the channel until we have processed all incoming requests.
        object
            .channel()
            .signal_peer(Signals::NONE, Signals::USER_0)
            .or_else(|e| match e {
                zx::Status::PEER_CLOSED => Ok(()),
                e => Err(e),
            })
            .context("ServiceFs signal_peer failed")?;

        let info = if (flags & OPEN_FLAG_DESCRIBE) != 0 {
            Some(self.describe_node(position)?)
        } else {
            None
        };

        let is_directory = if let ServiceFsNode::Directory { .. } = node { true } else { false };
        if (flags & OPEN_FLAG_DIRECTORY != 0) && !is_directory {
            send_failed_on_open(object, zx::sys::ZX_ERR_NOT_DIR)?;
            return Ok(None);
        }
        if (flags & OPEN_FLAG_NOT_DIRECTORY != 0) && is_directory {
            send_failed_on_open(object, zx::sys::ZX_ERR_NOT_FILE)?;
            return Ok(None);
        }

        if flags & OPEN_FLAG_NODE_REFERENCE != 0 {
            let chan = into_async(object.into_channel())?;
            let stream = NodeRequestStream::from_channel(chan);
            send_info_node(&stream, info)?;
            self.node_connections
                .push(NodeConnection::new(stream, NodeConnectionData { position }));
            return Ok(None);
        }

        let chan = object.into_channel();

        match &mut self.nodes[position] {
            ServiceFsNode::Directory { .. } => {
                let chan = into_async(chan)?;
                let stream = DirectoryRequestStream::from_channel(chan);
                send_info_dir(&stream, info)?;
                self.dir_connections.push(DirConnection::new(
                    stream,
                    DirConnectionData { position, dirents_buf: None },
                ));
                Ok(None)
            }
            ServiceFsNode::VmoFile { .. } => {
                let chan = into_async(chan)?;
                let stream = FileRequestStream::from_channel(chan);
                send_info_file(&stream, info)?;
                self.file_connections.push(FileConnection::new(
                    stream,
                    FileConnectionData { position, seek_offset: 0 },
                ));
                Ok(None)
            }
            ServiceFsNode::Service(service) => {
                if let Some(name) = name {
                    Ok(service.service().connect_at(name, chan))
                } else {
                    Ok(service.service().connect(chan))
                }
            }
        }
    }

    fn handle_clone(
        &mut self,
        flags: u32,
        object: ServerEnd<NodeMarker>,
        position: usize,
    ) -> Option<ServiceObjTy::Output> {
        match (|| {
            let object =
                handle_potentially_unsupported_flags(object, flags, CLONE_REQ_SUPPORTED_FLAGS)?;
            self.serve_connection_at(object, position, None, flags)
        })() {
            Ok(output) => output,
            Err(e) => {
                eprintln!("ServiceFs failed to clone: {:?}", e);
                None
            }
        }
    }

    // TODO(fxbug.dev/37419): Remove default handling after methods landed.
    #[allow(unreachable_patterns)]
    fn handle_dir_request(
        &mut self,
        request: DirectoryRequest,
        connection: &mut DirConnectionData,
    ) -> Result<(Option<ServiceObjTy::Output>, ConnectionState), Error> {
        assert!(self.nodes.len() > connection.position);

        match request {
            DirectoryRequest::Clone { flags, object, control_handle: _ } => {
                match self.handle_clone(flags, object, connection.position) {
                    Some(_) => panic!("cloning directory connection should not return output"),
                    None => {}
                }
            }
            DirectoryRequest::Close { responder } => {
                responder.send(zx::sys::ZX_OK)?;
                return Ok((None, ConnectionState::Closed));
            }
            DirectoryRequest::Open { flags, mode, path, object, control_handle: _ } => {
                if path == "." {
                    let object = handle_potentially_unsupported_flags(
                        object,
                        flags,
                        OPEN_REQ_SUPPORTED_FLAGS,
                    )?;
                    match self.serve_connection_at(object, connection.position, None, flags) {
                        Ok(Some(_)) => panic!("serving directory '.' should not return output"),
                        Ok(None) => {}
                        Err(e) => eprintln!("ServiceFs failed to serve '.': {:?}", e),
                    }
                    return Ok((None, ConnectionState::Open));
                } else if path == "" {
                    maybe_send_error(object, flags, zx::sys::ZX_ERR_BAD_PATH)?;
                    return Ok((None, ConnectionState::Open));
                }

                let mut segments = path.rsplitn(2, "/");
                let end_segment = segments.next().unwrap();
                let directory_segment = segments.next();
                let descend_result = match self.descend(connection.position, directory_segment) {
                    Ok(r) => r,
                    Err(_) => {
                        maybe_send_error(object, flags, zx::sys::ZX_ERR_BAD_PATH)?;
                        return Ok((None, ConnectionState::Open));
                    }
                };

                match descend_result {
                    DescendResult::LocalChildren(children) => {
                        // Some flags are unsupported when dealing with local children.
                        // When the descend ends up at a remote dir, do not check the flags.
                        let object = handle_potentially_unsupported_flags(
                            object,
                            flags,
                            OPEN_REQ_SUPPORTED_FLAGS,
                        )?;
                        if let Some(&next_node_pos) = children.get(end_segment) {
                            let output = self.serve_connection_at(
                                object,
                                next_node_pos,
                                Some(end_segment),
                                flags,
                            )?;
                            return Ok((output, ConnectionState::Open));
                        } else {
                            maybe_send_error(object, flags, zx::sys::ZX_ERR_NOT_FOUND)?;
                            return Ok((None, ConnectionState::Open));
                        }
                    }
                    DescendResult::RemoteDir((proxy, remaining_path)) => {
                        let remaining_path = vec![remaining_path, end_segment.to_owned()]
                            .into_iter()
                            .filter(|x| x.len() > 0)
                            .collect::<Vec<_>>()
                            .join("/");
                        proxy.open(flags, mode, &remaining_path, object)?;
                        return Ok((None, ConnectionState::Open));
                    }
                }
            }
            DirectoryRequest::Describe { responder } => {
                let mut info = self.describe_node(connection.position)?;
                responder.send(&mut info)?;
            }
            DirectoryRequest::GetAttr { responder } => {
                let mut attrs = self.node_attrs(connection.position);
                responder.send(zx::sys::ZX_OK, &mut attrs)?
            }
            DirectoryRequest::SetAttr { responder, .. } => unsupported!(responder)?,
            DirectoryRequest::Sync { responder } => unsupported!(responder)?,
            DirectoryRequest::Unlink { responder, .. } => unsupported!(responder)?,
            DirectoryRequest::ReadDirents { max_bytes, responder } => {
                let children = self.children_for_dir(connection.position)?;

                let dirents_buf = connection
                    .dirents_buf
                    .get_or_insert_with(|| (self.to_dirent_bytes(&children), 0));
                let (dirents_buf, offset) = (&mut dirents_buf.0, &mut dirents_buf.1);
                if *offset >= dirents_buf.len() {
                    responder.send(zx::sys::ZX_OK, &[])?;
                } else {
                    let new_offset = std::cmp::min(dirents_buf.len(), *offset + max_bytes as usize);
                    responder.send(zx::sys::ZX_OK, &dirents_buf[*offset..new_offset])?;
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
            _ => {}
        }
        Ok((None, ConnectionState::Open))
    }

    // TODO(fxbug.dev/37419): Remove default handling after methods landed.
    #[allow(unreachable_patterns)]
    fn handle_file_request(
        &mut self,
        request: FileRequest,
        connection: &mut FileConnectionData,
    ) -> Result<ConnectionState, Error> {
        match request {
            FileRequest::Clone { flags, object, control_handle: _ } => {
                match self.handle_clone(flags, object, connection.position) {
                    Some(_) => panic!("file clone should not return output"),
                    None => {}
                }
            }
            FileRequest::Close { responder } => {
                responder.send(zx::sys::ZX_OK)?;
                return Ok(ConnectionState::Closed);
            }
            FileRequest::Describe { responder } => {
                let mut info = self.describe_node(connection.position)?;
                responder.send(&mut info)?;
            }
            FileRequest::Sync { responder } => unsupported!(responder)?,
            FileRequest::GetAttr { responder } => {
                let mut attrs = self.node_attrs(connection.position);
                responder.send(zx::sys::ZX_OK, &mut attrs)?
            }
            FileRequest::SetAttr { responder, .. } => unsupported!(responder)?,
            // FIXME(cramertj) enforce READ rights
            FileRequest::Read { count, responder } => match &self.nodes[connection.position] {
                ServiceFsNode::Directory { .. } | ServiceFsNode::Service(_) => {
                    panic!("read on non-file node")
                }
                ServiceFsNode::VmoFile { vmo, length, offset } => {
                    let actual_count = min(count, length.saturating_sub(connection.seek_offset));
                    let mut data = vec![0; actual_count as usize];
                    let status = vmo.read(&mut data, offset.saturating_add(connection.seek_offset));
                    match status {
                        Ok(()) => {
                            responder.send(zx::sys::ZX_OK, &data)?;
                            connection.seek_offset += actual_count;
                        }
                        Err(s) => responder.send(s.into_raw(), &[])?,
                    }
                }
            },
            FileRequest::ReadAt { count, offset: read_offset, responder } => {
                match &self.nodes[connection.position] {
                    ServiceFsNode::Directory { .. } | ServiceFsNode::Service(_) => {
                        panic!("read-at on non-file node")
                    }
                    ServiceFsNode::VmoFile { vmo, length, offset } => {
                        let length = *length;
                        let offset = *offset;
                        let actual_offset = min(offset.saturating_add(read_offset), length);
                        let actual_count = min(count, length.saturating_sub(actual_offset));
                        let mut data = vec![0; actual_count as usize];
                        let status = vmo.read(&mut data, actual_offset);
                        match status {
                            Ok(()) => responder.send(zx::sys::ZX_OK, &data)?,
                            Err(s) => responder.send(s.into_raw(), &[])?,
                        }
                    }
                }
            }
            FileRequest::Write { responder, .. } => unsupported!(responder, 0)?,
            FileRequest::WriteAt { responder, .. } => unsupported!(responder, 0)?,
            FileRequest::Seek { offset, start, responder } => {
                let start = match start {
                    SeekOrigin::Start => 0,
                    SeekOrigin::Current => connection.seek_offset,
                    SeekOrigin::End => match &self.nodes[connection.position] {
                        ServiceFsNode::Directory { .. } | ServiceFsNode::Service(_) => {
                            panic!("seek on non-file node")
                        }
                        ServiceFsNode::VmoFile { length, .. } => *length,
                    },
                };
                let new_offset: u64 = if offset.is_positive() {
                    start.saturating_add(offset as u64)
                } else if offset == i64::min_value() {
                    0
                } else {
                    start.saturating_sub(offset.abs() as u64)
                };
                connection.seek_offset = new_offset;
                responder.send(zx::sys::ZX_OK, new_offset)?;
            }
            FileRequest::Truncate { responder, .. } => unsupported!(responder)?,
            FileRequest::GetFlags { responder, .. } => unsupported!(responder, 0)?,
            FileRequest::SetFlags { responder, .. } => unsupported!(responder)?,
            FileRequest::GetBuffer { responder, .. } => unsupported!(responder, None)?,
            _ => {}
        }
        Ok(ConnectionState::Open)
    }

    // TODO(fxbug.dev/37419): Remove default handling after methods landed.
    #[allow(unreachable_patterns)]
    fn handle_node_request(
        &mut self,
        request: NodeRequest,
        connection: &mut NodeConnectionData,
    ) -> Result<ConnectionState, Error> {
        match request {
            NodeRequest::Clone { flags, object, control_handle: _ } => {
                if flags & OPEN_FLAG_NODE_REFERENCE == 0 {
                    // we cannot connect the object-- it is requesting more than a
                    // node reference, which is not allowed from within a node reference.
                    return Ok(ConnectionState::Open);
                }
                match self.handle_clone(flags, object, connection.position) {
                    Some(_) => panic!("cloning node connection should not return output"),
                    None => {}
                }
            }
            NodeRequest::Close { responder } => {
                responder.send(zx::sys::ZX_OK)?;
                return Ok(ConnectionState::Closed);
            }
            NodeRequest::Describe { responder } => {
                let mut info = self.describe_node(connection.position)?;
                responder.send(&mut info)?;
            }
            NodeRequest::Sync { responder } => unsupported!(responder)?,
            NodeRequest::GetAttr { responder } => {
                let mut attrs = self.node_attrs(connection.position);
                responder.send(zx::sys::ZX_OK, &mut attrs)?
            }
            NodeRequest::SetAttr { responder, .. } => unsupported!(responder)?,
            _ => {}
        }
        Ok(ConnectionState::Open)
    }

    fn describe_node(&self, pos: usize) -> Result<NodeInfo, Error> {
        Ok(match self.nodes.get(pos).expect("describe on missing node") {
            ServiceFsNode::Directory { .. } => NodeInfo::Directory(DirectoryObject),
            ServiceFsNode::Service(..) => NodeInfo::Service(fidl_fuchsia_io::Service),
            ServiceFsNode::VmoFile { vmo, offset, length } => {
                let vmo = vmo
                    .duplicate_handle(zx::Rights::SAME_RIGHTS)
                    .context("error duplicating VmoFile handle in describe_node")?;
                let (offset, length) = (*offset, *length);
                NodeInfo::Vmofile(fidl_fuchsia_io::Vmofile { vmo, offset, length })
            }
        })
    }

    fn node_attrs(&self, pos: usize) -> NodeAttributes {
        let mut attrs = NodeAttributes {
            mode: libc::S_IRUSR,
            id: fidl_fuchsia_io::INO_UNKNOWN,
            content_size: 0,
            storage_size: 0,
            link_count: 1,
            creation_time: 0,
            modification_time: 0,
        };
        match self.nodes.get(pos).expect("attrs on missing node") {
            ServiceFsNode::Directory { .. } => {
                attrs.mode |= fidl_fuchsia_io::MODE_TYPE_DIRECTORY;
            }
            ServiceFsNode::VmoFile { vmo: _, offset: _, length } => {
                attrs.mode |= fidl_fuchsia_io::MODE_TYPE_FILE;
                attrs.content_size = *length;
                attrs.storage_size = *length;
            }
            ServiceFsNode::Service(_) => {
                attrs.mode |= fidl_fuchsia_io::MODE_TYPE_SERVICE;
            }
        }
        attrs
    }

    /// Traverse directory listings at |path| starting from node |start_pos|, returning either
    /// the index of the local directory given by the path or a reference to a proxy for a remote
    /// directory, along with the remaining parts of the path.
    fn descend(&self, start_pos: usize, path: Option<&str>) -> Result<DescendResult<'_>, Error> {
        let mut pos = start_pos;

        if let Some(path) = path {
            for (index, segment) in path.split("/").enumerate() {
                let children = self.children_for_dir(pos)?;
                match children.get(segment) {
                    Some(next_pos) => pos = *next_pos,
                    _ => return Err(format_err!("segment not found: {}", segment)),
                }
                match self.nodes.get(pos).expect(&format!("missing node {}", pos)) {
                    ServiceFsNode::Directory(Directory::Remote(proxy)) => {
                        return Ok(DescendResult::RemoteDir((
                            &proxy,
                            path.split("/").skip(index + 1).collect::<Vec<&str>>().join("/"),
                        )));
                    }
                    _ => {}
                }
            }
        }

        Ok(DescendResult::LocalChildren(self.children_for_dir(pos)?))
    }

    /// Retrieve directory listing of node |pos|. Return an error if |pos| is not a directory
    /// node
    fn children_for_dir(&self, pos: usize) -> Result<&HashMap<String, usize>, Error> {
        let node = self.nodes.get(pos).expect(&format!("missing node {}", pos));
        match node {
            ServiceFsNode::Directory(Directory::Local { children }) => Ok(children),
            _ => return Err(format_err!("node not a directory: {}", pos)),
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
    buf.write_all(name.as_ref()).unwrap();
    Ok(())
}

impl<ServiceObjTy: ServiceObjTrait> Unpin for ServiceFs<ServiceObjTy> {}

struct PollState {
    // `true` if *any* items so far have made progress.
    made_progress: bool,
    // Only `true` if *all* items so far are complete.
    is_complete: bool,
}

impl Default for PollState {
    fn default() -> Self {
        Self { made_progress: false, is_complete: true }
    }
}

impl PollState {
    const NO_PROGRESS: PollState = PollState { made_progress: false, is_complete: false };
    const SOME_PROGRESS: PollState = PollState { made_progress: true, is_complete: false };
    const COMPLETE: PollState = PollState { made_progress: false, is_complete: true };

    fn merge(&mut self, other: PollState) {
        self.made_progress |= other.made_progress;
        self.is_complete &= other.is_complete;
    }
}

// FIXME(cramertj) it'd be nice to abstract away the common
// bits of these two functions.
impl<ServiceObjTy: ServiceObjTrait> ServiceFs<ServiceObjTy> {
    fn poll_serve_dir_connection(
        &mut self,
        cx: &mut Context<'_>,
    ) -> (Option<ServiceObjTy::Output>, PollState) {
        let (request, dir_stream, mut dir_connection_data) =
            match self.dir_connections.poll_next_unpin(cx) {
                // a client request
                Poll::Ready(Some(Some(x))) => x,
                // this client_connection has terminated
                Poll::Ready(Some(None)) => return (None, PollState::SOME_PROGRESS),
                // all client connections have terminated
                Poll::Ready(None) => return (None, PollState::COMPLETE),
                Poll::Pending => return (None, PollState::NO_PROGRESS),
            };
        let request = match request {
            Ok(request) => request,
            Err(e) => {
                eprintln!("ServiceFs failed to parse an incoming directory request: {:?}", e);
                return (None, PollState::SOME_PROGRESS);
            }
        };
        match self.handle_dir_request(request, &mut dir_connection_data) {
            Ok((value, connection_state)) => {
                if let ConnectionState::Open = connection_state {
                    // Requeue the client to receive new requests
                    self.dir_connections.push(DirConnection::new(dir_stream, dir_connection_data));
                }
                (value, PollState::SOME_PROGRESS)
            }
            Err(e) => {
                eprintln!("ServiceFs failed to handle an incoming directory request: {:?}", e);
                (None, PollState::SOME_PROGRESS)
            }
        }
    }

    fn poll_serve_file_connection(&mut self, cx: &mut Context<'_>) -> PollState {
        let (request, file_stream, mut file_connection_data) =
            match self.file_connections.poll_next_unpin(cx) {
                // a client request
                Poll::Ready(Some(Some(x))) => x,
                // This client connection has terminated
                Poll::Ready(Some(None)) => return PollState::SOME_PROGRESS,
                // all client connections have terminated
                Poll::Ready(None) => return PollState::COMPLETE,
                Poll::Pending => return PollState::NO_PROGRESS,
            };
        let request = match request {
            Ok(request) => request,
            Err(e) => {
                eprintln!("ServiceFs failed to parse an incoming file request: {:?}", e);
                return PollState::SOME_PROGRESS;
            }
        };
        match self.handle_file_request(request, &mut file_connection_data) {
            Ok(ConnectionState::Open) => {
                // Requeue the client to receive new requests
                self.file_connections.push(FileConnection::new(file_stream, file_connection_data));
            }
            Ok(ConnectionState::Closed) => {}
            Err(e) => {
                eprintln!("ServiceFs failed to handle an incoming file request: {:?}", e);
            }
        }
        PollState::SOME_PROGRESS
    }

    fn poll_serve_node_connection(&mut self, cx: &mut Context<'_>) -> PollState {
        let (request, node_stream, mut node_connection_data) =
            match self.node_connections.poll_next_unpin(cx) {
                // a client request
                Poll::Ready(Some(Some(x))) => x,
                // This client connection has terminated
                Poll::Ready(Some(None)) => return PollState::SOME_PROGRESS,
                // all client connections have terminated
                Poll::Ready(None) => return PollState::COMPLETE,
                Poll::Pending => return PollState::NO_PROGRESS,
            };
        let request = match request {
            Ok(request) => request,
            Err(e) => {
                eprintln!("ServiceFs failed to parse an incoming node request: {:?}", e);
                return PollState::SOME_PROGRESS;
            }
        };
        match self.handle_node_request(request, &mut node_connection_data) {
            Ok(ConnectionState::Open) => {
                // Requeue the client to receive new requests
                self.node_connections.push(NodeConnection::new(node_stream, node_connection_data));
            }
            Ok(ConnectionState::Closed) => {}
            Err(e) => {
                eprintln!("ServiceFs failed to handle an incoming node request: {:?}", e);
            }
        }
        PollState::SOME_PROGRESS
    }
}

impl<ServiceObjTy: ServiceObjTrait> Stream for ServiceFs<ServiceObjTy> {
    type Item = ServiceObjTy::Output;

    fn poll_next(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        loop {
            let mut iter_state = PollState::default();

            let (output, state) = self.poll_serve_dir_connection(cx);
            if let Some(output) = output {
                return Poll::Ready(Some(output));
            }
            iter_state.merge(state);

            let state = self.poll_serve_file_connection(cx);
            iter_state.merge(state);

            let state = self.poll_serve_node_connection(cx);
            iter_state.merge(state);

            // Return `None` to end the stream if all connections are done being served.
            if iter_state.is_complete {
                return Poll::Ready(None);
            }
            // Otherwise, return `Pending` if no new requests were available to serve.
            if !iter_state.made_progress {
                return Poll::Pending;
            }
        }
    }
}

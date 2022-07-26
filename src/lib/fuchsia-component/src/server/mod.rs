// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Tools for providing Fuchsia services.

use {
    crate::DEFAULT_SERVICE_INSTANCE,
    anyhow::{format_err, Context as _, Error},
    fidl::endpoints::{
        DiscoverableProtocolMarker, Proxy as _, RequestStream, ServerEnd, ServiceMarker,
        ServiceRequest,
    },
    fidl_fuchsia_io as fio,
    fidl_fuchsia_sys::{
        EnvironmentControllerProxy, EnvironmentMarker, EnvironmentOptions, LauncherProxy,
        LoaderMarker, ServiceList,
    },
    fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::{channel::mpsc, future::BoxFuture, FutureExt, Stream, StreamExt},
    pin_project::pin_project,
    std::{
        any::TypeId,
        marker::PhantomData,
        pin::Pin,
        sync::Arc,
        task::{Context, Poll},
    },
    thiserror::Error,
    vfs::{
        directory::{
            entry::DirectoryEntry,
            helper::DirectlyMutable,
            immutable::{connection::io1::ImmutableConnection, simple::simple},
            simple::Simple,
        },
        execution_scope::ExecutionScope,
        file::vmo::ReadOnlyVmoFile,
        path::Path,
        remote::{remote_dir, remote_node},
        service::endpoint,
    },
};

mod service;
pub use service::{
    FidlService, FidlServiceMember, FidlServiceServerConnector, Service, ServiceObj,
    ServiceObjLocal, ServiceObjTrait,
};

/// A filesystem which connects clients to services.
///
/// This type implements the `Stream` trait and will yield the values
/// returned from calling `Service::connect` on the services it hosts.
///
/// This can be used to, for example, yield streams of channels, request
/// streams, futures to run, or any other value that should be processed
/// as the result of a request.
#[must_use]
#[pin_project]
pub struct ServiceFs<ServiceObjTy: ServiceObjTrait> {
    // The execution scope for the backing VFS.
    scope: ExecutionScope,

    // The root directory.
    dir: Arc<Simple<ImmutableConnection>>,

    // New connections are sent via an mpsc. The tuple is (index, channel) where index is the index
    // into the `services` member.
    new_connection_sender: mpsc::UnboundedSender<(usize, zx::Channel)>,
    new_connection_receiver: mpsc::UnboundedReceiver<(usize, zx::Channel)>,

    // A collection of objects that are able to handle new connections and convert them into a
    // stream of ServiceObjTy::Output requests.  There will be one for each service in the
    // filesystem (irrespective of its place in the hierarchy).
    services: Vec<ServiceObjTy>,

    // A future that completes when the VFS no longer has any connections.  These connections are
    // distinct from connections that might be to services or remotes within this filesystem.
    shutdown: BoxFuture<'static, ()>,

    // The filesystem does not start servicing any requests until ServiceFs is first polled.  This
    // preserves behaviour of ServiceFs from when it didn't use the Rust VFS, and is relied upon in
    // some cases.  The queue is used until first polled.  After that, `channel_queue` will be None
    // and requests to service channels will be actioned immediately (potentially on different
    // threads depending on the executor).
    channel_queue: Option<Vec<zx::Channel>>,
}

impl<'a, Output: 'a> ServiceFs<ServiceObjLocal<'a, Output>> {
    /// Create a new `ServiceFs` that is singlethreaded-only and does not
    /// require services to implement `Send`.
    pub fn new_local() -> Self {
        Self::new_impl()
    }
}

impl<'a, Output: 'a> ServiceFs<ServiceObj<'a, Output>> {
    /// Create a new `ServiceFs` that is multithreaded-capable and requires
    /// services to implement `Send`.
    pub fn new() -> Self {
        Self::new_impl()
    }
}

/// A directory within a `ServiceFs`.
///
/// Services and subdirectories can be added to it.
pub struct ServiceFsDir<'a, ServiceObjTy: ServiceObjTrait> {
    fs: &'a mut ServiceFs<ServiceObjTy>,
    dir: Arc<Simple<ImmutableConnection>>,
}

/// A `Service` implementation that proxies requests
/// to the outside environment.
///
/// Not intended for direct use. Use the `add_proxy_service`
/// function instead.
#[doc(hidden)]
pub struct Proxy<P, O>(PhantomData<(P, fn() -> O)>);

impl<P: DiscoverableProtocolMarker, O> Service for Proxy<P, O> {
    type Output = O;
    fn connect(&mut self, channel: zx::Channel) -> Option<O> {
        if let Err(e) = crate::client::connect_channel_to_protocol::<P>(channel) {
            eprintln!("failed to proxy request to {}: {:?}", P::PROTOCOL_NAME, e);
        }
        None
    }
}

/// A `Service` implementation that proxies requests to the given component.
///
/// Not intended for direct use. Use the `add_proxy_service_to` function instead.
#[doc(hidden)]
pub struct ProxyTo<P, O> {
    directory_request: Arc<zx::Channel>,
    _phantom: PhantomData<(P, fn() -> O)>,
}

impl<P: DiscoverableProtocolMarker, O> Service for ProxyTo<P, O> {
    type Output = O;
    fn connect(&mut self, channel: zx::Channel) -> Option<O> {
        if let Err(e) = fdio::service_connect_at(&self.directory_request, P::PROTOCOL_NAME, channel)
        {
            eprintln!("failed to proxy request to {}: {:?}", P::PROTOCOL_NAME, e);
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
pub struct ComponentProxy<P: DiscoverableProtocolMarker, O> {
    launch_data: Option<LaunchData>,
    launched_app: Option<crate::client::App>,
    _marker: PhantomData<(P, O)>,
}

impl<P: DiscoverableProtocolMarker, O> Service for ComponentProxy<P, O> {
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
                app.pass_to_named_protocol(P::PROTOCOL_NAME, channel.into())?;
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
        /// Adds a service connector to the directory.
        ///
        /// ```rust
        /// let mut fs = ServiceFs::new_local();
        /// fs
        ///     .add_service_connector(|server_end: ServerEnd<EchoMarker>| {
        ///         connect_channel_to_protocol::<EchoMarker>(
        ///             server_end.into_channel(),
        ///         )
        ///     })
        ///     .add_service_connector(|server_end: ServerEnd<CustomMarker>| {
        ///         connect_channel_to_protocol::<CustomMarker>(
        ///             server_end.into_channel(),
        ///         )
        ///     })
        ///     .take_and_serve_directory_handle()?;
        /// ```
        ///
        /// The FIDL service will be hosted at the name provided by the
        /// `[Discoverable]` annotation in the FIDL source.
        pub fn add_service_connector<F, P>(&mut self, service: F) -> &mut Self
        where
            F: FnMut(ServerEnd<P>) -> ServiceObjTy::Output,
            P: DiscoverableProtocolMarker,
            FidlServiceServerConnector<F, P, ServiceObjTy::Output>: Into<ServiceObjTy>,
        {
            self.add_service_at(P::PROTOCOL_NAME, FidlServiceServerConnector::from(service))
        }

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
            let index = self.fs().services.len();
            self.fs().services.push(service.into());
            let sender = self.fs().new_connection_sender.clone();
            self.add_entry_at(
                path.into(),
                endpoint(move |_, channel| {
                    // It's possible for this send to fail in the case where ServiceFs has been
                    // dropped.  When that happens, ServiceFs will drop ExecutionScope which
                    // contains the RemoteHandle for this task which will then cause this task to be
                    // dropped but not necessarily immediately.  This will only occur when ServiceFs
                    // has been dropped, so it's safe to ignore the error here.
                    let _ = sender.unbounded_send((index, channel.into()));
                }),
            )
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
        /// ```rust
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
        /// ```rust
        /// let mut fs = ServiceFs::new_local();
        /// fs
        ///     .add_fidl_service(MyServices::EchoServer)
        ///     .add_fidl_service(MyServices::CustomServer)
        ///     .take_and_serve_directory_handle()?;
        /// ```
        ///
        /// `ServiceFs` can now be treated as a `Stream` of type `MyServices`.
        ///
        /// ```rust
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
            RS::Protocol: DiscoverableProtocolMarker,
            FidlService<F, RS, ServiceObjTy::Output>: Into<ServiceObjTy>,
        {
            self.add_fidl_service_at(RS::Protocol::PROTOCOL_NAME, service)
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
            RS::Protocol: DiscoverableProtocolMarker,
            FidlService<F, RS, ServiceObjTy::Output>: Into<ServiceObjTy>,
        {
            self.add_service_at(path, FidlService::from(service))
        }

        /// Adds a FIDL service to the directory as the default instance.
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
        pub fn add_unified_service<F, SR>(&mut self, service: F) -> &mut Self
        where
            F: Fn(SR) -> ServiceObjTy::Output,
            F: Clone,
            SR: ServiceRequest,
            FidlServiceMember<F, SR, ServiceObjTy::Output>: Into<ServiceObjTy>,
        {
            self.add_unified_service_at(SR::Service::SERVICE_NAME, service)
        }

        /// Adds a FIDL service to the directory as the default instance at the given path.
        ///
        /// The path must be a single component containing no `/` characters.
        /// The name of the default instance is
        /// [`DEFAULT_SERVICE_INSTANCE`](../constant.DEFAULT_SERVICE_INSTANCE.html).
        ///
        /// The FIDL service will be hosted at `[path]/default/`.
        pub fn add_unified_service_at<F, SR>(
            &mut self,
            path: impl Into<String>,
            service: F,
        ) -> &mut Self
        where
            F: Fn(SR) -> ServiceObjTy::Output,
            F: Clone,
            SR: ServiceRequest,
            FidlServiceMember<F, SR, ServiceObjTy::Output>: Into<ServiceObjTy>,
        {
            self.add_unified_service_instance_at(path, DEFAULT_SERVICE_INSTANCE, service)
        }

        /// Adds a named instance of a FIDL service to the directory.
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
        pub fn add_unified_service_instance<F, SR>(
            &mut self,
            instance: impl Into<String>,
            service: F,
        ) -> &mut Self
        where
            F: Fn(SR) -> ServiceObjTy::Output,
            F: Clone,
            SR: ServiceRequest,
            FidlServiceMember<F, SR, ServiceObjTy::Output>: Into<ServiceObjTy>,
        {
            self.add_unified_service_instance_at(SR::Service::SERVICE_NAME, instance, service)
        }

        /// Adds a named instance of a FIDL service to the directory at the given path.
        ///
        /// The FIDL service will be hosted at `[path]/[instance]/`.
        ///
        /// The `path` and `instance` must be single components containing no `/` characters.
        pub fn add_unified_service_instance_at<F, SR>(
            &mut self,
            path: impl Into<String>,
            instance: impl Into<String>,
            service: F,
        ) -> &mut Self
        where
            F: Fn(SR) -> ServiceObjTy::Output,
            F: Clone,
            SR: ServiceRequest,
            FidlServiceMember<F, SR, ServiceObjTy::Output>: Into<ServiceObjTy>,
        {
            // Create the service directory, with an instance subdirectory.
            let mut dir = self.dir(path);
            let mut dir = dir.dir(instance);

            // Attach member protocols under the instance directory.
            for member in SR::member_names() {
                dir.add_service_at(*member, FidlServiceMember::new(service.clone(), member));
            }
            self
        }

        /// Adds a service that proxies requests to the current environment.
        // NOTE: we'd like to be able to remove the type parameter `O` here,
        //  but unfortunately the bound `ServiceObjTy: From<Proxy<P, ServiceObjTy::Output>>`
        //  makes type checking angry.
        pub fn add_proxy_service<P: DiscoverableProtocolMarker, O>(&mut self) -> &mut Self
        where
            ServiceObjTy: From<Proxy<P, O>>,
            ServiceObjTy: ServiceObjTrait<Output = O>,
        {
            self.add_service_at(P::PROTOCOL_NAME, Proxy::<P, ServiceObjTy::Output>(PhantomData))
        }

        /// Adds a service that proxies requests to the given component.
        // NOTE: we'd like to be able to remove the type parameter `O` here,
        //  but unfortunately the bound `ServiceObjTy: From<Proxy<P, ServiceObjTy::Output>>`
        //  makes type checking angry.
        pub fn add_proxy_service_to<P: DiscoverableProtocolMarker, O>(
            &mut self,
            directory_request: Arc<zx::Channel>,
        ) -> &mut Self
        where
            ServiceObjTy: From<ProxyTo<P, O>>,
            ServiceObjTy: ServiceObjTrait<Output = O>,
        {
            self.add_service_at(
                P::PROTOCOL_NAME,
                ProxyTo::<P, ServiceObjTy::Output> { directory_request, _phantom: PhantomData },
            )
        }

        /// Add a service to the `ServicesServer` that will launch a component
        /// upon request, proxying requests to the launched component.
        pub fn add_component_proxy_service<P: DiscoverableProtocolMarker, O>(
            &mut self,
            component_url: String,
            arguments: Option<Vec<String>>,
        ) -> &mut Self
        where
            ServiceObjTy: From<ComponentProxy<P, O>>,
            ServiceObjTy: ServiceObjTrait<Output = O>,
        {
            self.add_service_at(
                P::PROTOCOL_NAME,
                ComponentProxy {
                    launch_data: Some(LaunchData { component_url, arguments }),
                    launched_app: None,
                    _marker: PhantomData,
                },
            )
        }

        /// Adds a VMO file to the directory at the given path.
        ///
        /// The path must be a single component containing no `/` characters. The vmo should have
        /// content size set as required.
        ///
        /// Panics if any node has already been added at the given path.
        pub fn add_vmo_file_at(&mut self, path: impl Into<String>, vmo: zx::Vmo) -> &mut Self {
            self.add_entry_at(path.into(), Arc::new(ReadOnlyVmoFile::new(vmo)))
        }

        fn add_entry_at(&mut self, path: String, entry: Arc<dyn DirectoryEntry>) -> &mut Self {
            // This will fail if the name has '/' characters or already exists.
            self.dir.add_entry_impl(path, entry, false).expect("Unable to add entry");
            self
        }

        /// Returns a reference to the subdirectory at the given path,
        /// creating one if none exists.
        ///
        /// The path must be a single component containing no `/` characters.
        ///
        /// Panics if a service has already been added at the given path.
        pub fn dir(&mut self, path: impl Into<String>) -> ServiceFsDir<'_, ServiceObjTy> {
            let dir = Arc::downcast(self.dir.get_or_insert(path.into(), simple).into_any())
                .unwrap_or_else(|_| panic!("Not a directory"));
            ServiceFsDir { fs: self.fs(), dir }
        }

        /// Adds a new remote directory served over the given DirectoryProxy.
        ///
        /// The name must not contain any '/' characters.
        pub fn add_remote(&mut self, name: impl Into<String>, proxy: fio::DirectoryProxy) {
            self.dir
                .add_entry_impl(name.into(), remote_dir(proxy), false)
                .expect("Unable to add entry");
        }

        /// Adds a new remote served over the given NodeProxy.  If the remote is a directory,
        /// add_remote should be used instead.
        ///
        /// The name must not contain any '/' characters.
        pub fn add_remote_node(&mut self, name: impl Into<String>, proxy: fio::NodeProxy) {
            self.dir
                .add_entry_impl(name.into(), remote_node(proxy), false)
                .expect("Unable to add entry");
        }
    };
}

impl<ServiceObjTy: ServiceObjTrait> ServiceFsDir<'_, ServiceObjTy> {
    fn fs(&mut self) -> &mut ServiceFs<ServiceObjTy> {
        self.fs
    }

    add_functions!();
}

impl<ServiceObjTy: ServiceObjTrait> ServiceFs<ServiceObjTy> {
    fn new_impl() -> Self {
        let (new_connection_sender, new_connection_receiver) = mpsc::unbounded();
        let scope = ExecutionScope::new();
        Self {
            scope: scope.clone(),
            dir: simple(),
            new_connection_sender,
            new_connection_receiver,
            services: Vec::new(),
            shutdown: async move { scope.wait().await }.boxed(),
            channel_queue: Some(Vec::new()),
        }
    }

    fn fs(&mut self) -> &mut ServiceFs<ServiceObjTy> {
        self
    }

    /// Get a reference to the root directory as a `ServiceFsDir`.
    ///
    /// This can be useful when writing code which hosts some set of services on
    /// a directory and wants to be agnostic to whether that directory
    /// is the root `ServiceFs` or a subdirectory.
    ///
    /// Such a function can take an `&mut ServiceFsDir<...>` as an argument,
    /// allowing callers to provide either a subdirectory or `fs.root_dir()`.
    pub fn root_dir(&mut self) -> ServiceFsDir<'_, ServiceObjTy> {
        let dir = self.dir.clone();
        ServiceFsDir { fs: self, dir }
    }

    add_functions!();

    /// Start serving directory protocol service requests via a `ServiceList`.
    /// The resulting `ServiceList` can be attached to a new environment in
    /// order to provide child components with access to these services.
    pub fn host_services_list(&mut self) -> Result<ServiceList, Error> {
        let names = self.dir.filter_map(|name, entry| {
            if entry.as_ref().type_id() == TypeId::of::<vfs::service::Service>() {
                Some(name.into())
            } else {
                None
            }
        });

        let (chan1, chan2) = zx::Channel::create()?;
        self.serve_connection(chan1)?;

        Ok(ServiceList { names, provider: None, host_directory: Some(chan2) })
    }

    /// Returns true if the root contains any sub-directories.
    fn has_sub_dirs(&self) -> bool {
        self.dir
            .any(|_, entry| entry.as_ref().type_id() == TypeId::of::<Simple<ImmutableConnection>>())
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
        zx::cprng_draw(&mut salt[..]);
        let environment_label = format!("{}_{}", environment_label_prefix, hex::encode(&salt));
        self.create_nested_environment(&environment_label)
    }

    /// Creates a new environment that only has access to the services provided through this
    /// `ServiceFs` and the enclosing environment's `Loader` service, appending a few random
    /// bytes to the given `environment_label_prefix` to ensure this environment has a unique
    /// name. Uses the provided environment `options`.
    ///
    /// Note that the resulting `NestedEnvironment` must be kept alive for the environment to
    /// continue to exist. Once dropped, the environment and all components launched within it
    /// will be destroyed.
    pub fn create_salted_nested_environment_with_options<O>(
        &mut self,
        environment_label_prefix: &str,
        options: EnvironmentOptions,
    ) -> Result<NestedEnvironment, Error>
    where
        ServiceObjTy: From<Proxy<LoaderMarker, O>>,
        ServiceObjTy: ServiceObjTrait<Output = O>,
    {
        let mut salt = [0; 4];
        zx::cprng_draw(&mut salt[..]);
        let environment_label = format!("{}_{}", environment_label_prefix, hex::encode(&salt));
        self.create_nested_environment_with_options(&environment_label, options)
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
        let env = crate::client::connect_to_protocol::<EnvironmentMarker>()
            .context("connecting to current environment")?;
        let services_with_loader = self.add_proxy_service::<LoaderMarker, _>();

        // Services added in any subdirectories won't be provided to the nested environment, which
        // is an important detail that developers are likely to overlook. If there are any
        // subdirectories in this ServiceFs, return an error, because what we're about to do
        // probably doesn't line up with what the developer expects.
        if services_with_loader.has_sub_dirs() {
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

    fn serve_connection_impl(&self, chan: zx::Channel) {
        self.dir.clone().open(
            self.scope.clone(),
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
            0,
            Path::dot(),
            chan.into(),
        );
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

    /// Connect to a protocol provided by this environment.
    #[inline]
    pub fn connect_to_service<P: DiscoverableProtocolMarker>(&self) -> Result<P::Proxy, Error> {
        self.connect_to_protocol::<P>()
    }

    /// Connect to a protocol provided by this environment.
    #[inline]
    pub fn connect_to_protocol<P: DiscoverableProtocolMarker>(&self) -> Result<P::Proxy, Error> {
        let (client_channel, server_channel) = zx::Channel::create()?;
        self.pass_to_protocol::<P>(server_channel)?;
        Ok(P::Proxy::from_channel(fasync::Channel::from_channel(client_channel)?))
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
}

/// An error indicating the startup handle on which the FIDL server
/// attempted to start was missing.
#[derive(Debug, Error)]
#[error("The startup handle on which the FIDL server attempted to start was missing.")]
pub struct MissingStartupHandle;

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

    /// Add a channel to serve this `ServiceFs` filesystem on. The `ServiceFs`
    /// will continue to be provided over previously added channels, including
    /// the one added if `take_and_serve_directory_handle` was called.
    pub fn serve_connection(&mut self, chan: zx::Channel) -> Result<&mut Self, Error> {
        if let Some(channels) = &mut self.channel_queue {
            channels.push(chan);
        } else {
            self.serve_connection_impl(chan);
        }
        Ok(self)
    }
}

impl<ServiceObjTy: ServiceObjTrait> Stream for ServiceFs<ServiceObjTy> {
    type Item = ServiceObjTy::Output;

    fn poll_next(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        if let Some(channels) = self.channel_queue.take() {
            for chan in channels {
                self.serve_connection_impl(chan);
            }
        }
        while let Poll::Ready(Some((index, channel))) =
            self.new_connection_receiver.poll_next_unpin(cx)
        {
            if let Some(stream) = self.services[index].service().connect(channel) {
                return Poll::Ready(Some(stream));
            }
        }
        self.shutdown.poll_unpin(cx).map(|_| None)
    }
}

#[cfg(test)]
mod tests {
    use {
        super::{ServiceFs, ServiceObj},
        fidl::endpoints::DiscoverableProtocolMarker,
        fidl_fuchsia_component::{RealmMarker, RealmRequestStream},
        fidl_fuchsia_sys::ServiceList,
    };

    enum Services {
        _Realm(RealmRequestStream),
    }

    #[test]
    fn has_sub_dirs() {
        let mut fs = ServiceFs::<ServiceObj<'_, Services>>::new();
        assert!(!fs.has_sub_dirs());
        fs.add_proxy_service::<RealmMarker, Services>();
        assert!(!fs.has_sub_dirs());
        fs.dir("test");
        assert!(fs.has_sub_dirs());
    }

    #[test]
    fn host_services_list() {
        let mut fs = ServiceFs::<ServiceObj<'_, Services>>::new();
        fs.add_proxy_service::<RealmMarker, Services>();
        fs.dir("test");
        assert!(matches!(fs.host_services_list(),
                     Ok(ServiceList { names, .. }) if names == vec![ RealmMarker::PROTOCOL_NAME ]));
    }
}

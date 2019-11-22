// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        framework::*,
        model::testing::breakpoints::*,
        model::{runner::Runner, *},
        root_realm_stop_notifier::RootRealmStopNotifier,
        startup::BuiltinRootCapabilities,
    },
    cm_rust::{data, CapabilityPath},
    failure::format_err,
    fidl::endpoints::{create_endpoints, create_proxy, Proxy, ServerEnd},
    fidl_fuchsia_io::{
        self as fio, DirectoryMarker, DirectoryProxy, MODE_TYPE_DIRECTORY, OPEN_RIGHT_READABLE,
        OPEN_RIGHT_WRITABLE,
    },
    fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync,
    fuchsia_component::server::*,
    fuchsia_zircon as zx,
    futures::future::join_all,
    futures::lock::Mutex,
    futures::stream::StreamExt,
    std::sync::Arc,
};

/// Parameters for initializing a component model, particularly the root of the component
/// instance tree.
pub struct ModelParams {
    /// The URL of the root component.
    pub root_component_url: String,
    /// The component resolver registry used in the root realm.
    /// In particular, it will be used to resolve the root component itself.
    pub root_resolver_registry: ResolverRegistry,
    /// The built-in ELF runner, used for starting components with an ELF binary.
    pub elf_runner: Arc<dyn Runner + Send + Sync + 'static>,
}

/// The component model holds authoritative state about a tree of component instances, including
/// each instance's identity, lifecycle, capabilities, and topological relationships.  It also
/// provides operations for instantiating, destroying, querying, and controlling component
/// instances at runtime.
///
/// To facilitate unit testing, the component model does not directly perform IPC.  Instead, it
/// delegates external interfacing concerns to other objects that implement traits such as
/// `Runner` and `Resolver`.
#[derive(Clone)]
pub struct Model {
    pub notifier: Arc<Mutex<Option<RootRealmStopNotifier>>>,
    pub root_realm: Arc<Realm>,

    /// The built-in ELF runner, used for starting components with an ELF binary.
    // TODO(fxb/4761): Remove. This should be a routed capability, and
    // not explicitly passed around in the model.
    pub elf_runner: Arc<dyn Runner + Send + Sync>,
}

pub struct BuiltinEnvironment {
    /// Builtin services that are available in the root realm.
    pub builtin_capabilities: Arc<BuiltinRootCapabilities>,
    pub realm_capability_host: RealmCapabilityHost,
    pub hub: Hub,
    pub breakpoint_system: Option<BreakpointSystem>,
}

impl BuiltinEnvironment {
    pub fn new(
        builtin_capabilities: Arc<BuiltinRootCapabilities>,
        realm_capability_host: RealmCapabilityHost,
        hub: Hub,
        breakpoint_system: Option<BreakpointSystem>,
    ) -> Self {
        Self { builtin_capabilities, realm_capability_host, hub, breakpoint_system }
    }

    /// Setup a ServiceFs that contains the Hub and (optionally) the breakpoints service
    async fn create_service_fs<'a>(
        model: &Model,
        hub: &Hub,
        breakpoint_system: &Option<BreakpointSystem>,
    ) -> Result<ServiceFs<ServiceObj<'a, ()>>, ModelError> {
        // Create the ServiceFs
        let mut service_fs = ServiceFs::new();

        // Setup the hub
        let (hub_proxy, hub_server_end) = create_proxy::<DirectoryMarker>().unwrap();
        hub.open_root(OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE, hub_server_end.into_channel())
            .await?;
        model.root_realm.hooks.install(hub.hooks()).await;
        service_fs.add_remote("hub", hub_proxy);

        // Setup the debug breakpoint system (optionally)
        if let Some(breakpoint_system) = breakpoint_system {
            // Install the breakpoint hooks at the root component.
            // This ensures that all events will reach this breakpoint system.
            model.root_realm.hooks.install(breakpoint_system.hooks()).await;

            // Register for RootComponentResolved event to halt the ComponentManager when the
            // root realm is first resolved.
            let root_realm_created_receiver =
                breakpoint_system.register(vec![EventType::RootComponentResolved]).await;

            // Setup a capability provider for external use. This provider is not used
            // by components within component manager, hence there is no associated hook for it.
            // Instead, it is used by external components that wish to debug component manager.
            let breakpoint_capability_provider = breakpoint_system.create_capability_provider();
            service_fs.dir("svc").add_fidl_service(move |stream| {
                breakpoint_capability_provider
                    .serve_async(stream, Some(root_realm_created_receiver.clone()))
                    .expect("failed to serve debug breakpoint capability");
            });
        }

        Ok(service_fs)
    }

    /// Bind ServiceFs to a provided channel
    async fn bind_service_fs(&self, model: &Model, channel: zx::Channel) -> Result<(), ModelError> {
        let mut service_fs =
            Self::create_service_fs(model, &self.hub, &self.breakpoint_system).await?;

        // Bind to the channel
        service_fs
            .serve_connection(channel)
            .map_err(|err| ModelError::namespace_creation_failed(err))?;

        // Start up ServiceFs
        fasync::spawn(async move {
            service_fs.collect::<()>().await;
        });
        Ok(())
    }

    /// Bind ServiceFs to the outgoing directory of this component, if it exists.
    pub async fn bind_service_fs_to_out(&self, model: &Model) -> Result<(), ModelError> {
        if let Some(handle) = fuchsia_runtime::take_startup_handle(
            fuchsia_runtime::HandleType::DirectoryRequest.into(),
        ) {
            self.bind_service_fs(model, zx::Channel::from(handle)).await?;
        }
        Ok(())
    }

    /// Bind ServiceFs to a new channel and return the Hub directory.
    /// Used mainly by integration tests.
    pub async fn bind_service_fs_for_hub(
        &self,
        model: &Model,
    ) -> Result<DirectoryProxy, ModelError> {
        // Create a channel that ServiceFs will operate on
        let (service_fs_proxy, service_fs_server_end) = create_proxy::<DirectoryMarker>().unwrap();

        self.bind_service_fs(model, service_fs_server_end.into_channel()).await?;

        // Open the Hub from within ServiceFs
        let (hub_client_end, hub_server_end) = create_endpoints::<DirectoryMarker>().unwrap();
        service_fs_proxy
            .open(
                OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
                MODE_TYPE_DIRECTORY,
                "hub",
                ServerEnd::new(hub_server_end.into_channel()),
            )
            .map_err(|err| ModelError::namespace_creation_failed(err))?;
        let hub_proxy = hub_client_end.into_proxy().unwrap();

        Ok(hub_proxy)
    }
}

/// Holds configuration options for the component manager.
#[derive(Clone)]
pub struct ComponentManagerConfig {
    /// How many children, maximum, are returned by a call to `ChildIterator.next()`.
    pub list_children_batch_size: usize,
}

impl ComponentManagerConfig {
    pub fn default() -> Self {
        ComponentManagerConfig { list_children_batch_size: 1000 }
    }
}

impl Model {
    /// Creates a new component model and initializes its topology.
    pub fn new(params: ModelParams) -> Model {
        Model {
            notifier: Arc::new(Mutex::new(Some(RootRealmStopNotifier::new()))),
            root_realm: Arc::new(Realm::new_root_realm(
                params.root_resolver_registry,
                params.root_component_url,
            )),
            elf_runner: params.elf_runner,
        }
    }

    pub async fn wait_for_root_realm_destroy(&self) {
        let mut notifier = self.notifier.lock().await;
        notifier
            .take()
            .expect("A root realm can only be destroyed once")
            .wait_for_root_realm_destroy()
            .await;
    }

    /// Given a realm and path, lazily bind to the instance in the realm, open its outgoing
    /// directory at that path, then bind its eager children.
    pub async fn bind_open_outgoing(
        &self,
        realm: Arc<Realm>,
        flags: u32,
        open_mode: u32,
        path: &CapabilityPath,
        server_chan: zx::Channel,
    ) -> Result<(), ModelError> {
        self.bind(&realm.abs_moniker).await?;
        let server_end = ServerEnd::new(server_chan);
        let execution = realm.lock_execution().await;
        if execution.runtime.is_none() {
            return Err(ModelError::capability_discovery_error(format_err!(
                "component hosting capability isn't running: {}",
                realm.abs_moniker
            )));
        }
        let out_dir = &execution
            .runtime
            .as_ref()
            .expect("bind_instance_open_outgoing: no runtime")
            .outgoing_dir
            .as_ref()
            .ok_or(ModelError::capability_discovery_error(format_err!(
                "component hosting capability is non-executable: {}",
                realm.abs_moniker
            )))?;
        let path = path.to_string();
        let path = io_util::canonicalize_path(&path);
        out_dir.open(flags, open_mode, path, server_end).map_err(|e| {
            ModelError::capability_discovery_error(format_err!(
                "failed to open outgoing dir for {}: {}",
                realm.abs_moniker,
                e
            ))
        })?;
        Ok(())
    }

    /// Given a realm and path, lazily bind to the instance in the realm, open its exposed
    /// directory, then bind its eager children.
    pub async fn bind_open_exposed(
        &self,
        realm: Arc<Realm>,
        server_chan: zx::Channel,
    ) -> Result<(), ModelError> {
        self.bind(&realm.abs_moniker).await?;
        let server_end = ServerEnd::new(server_chan);
        let execution = realm.lock_execution().await;
        if execution.runtime.is_none() {
            return Err(ModelError::capability_discovery_error(format_err!(
                "component hosting capability isn't running: {}",
                realm.abs_moniker
            )));
        }
        let exposed_dir = &execution
            .runtime
            .as_ref()
            .expect("bind_instance_open_exposed: no runtime")
            .exposed_dir;

        // TODO(fxb/36541): Until directory capabilities specify rights, we always open
        // directories using OPEN_FLAG_POSIX which automatically opens the new connection using
        // the same directory rights as the parent directory connection.
        let flags = fio::OPEN_RIGHT_READABLE | fio::OPEN_FLAG_POSIX;
        exposed_dir
            .root_dir
            .open(flags, fio::MODE_TYPE_DIRECTORY, vec![], server_end)
            .await
            .map_err(|e| {
                ModelError::capability_discovery_error(format_err!(
                    "failed to open exposed dir for {}: {}",
                    realm.abs_moniker,
                    e
                ))
            })?;
        Ok(())
    }

    /// Looks up a realm by absolute moniker. The component instance in the realm will be resolved
    /// if that has not already happened.
    pub async fn look_up_realm(
        &self,
        look_up_abs_moniker: &AbsoluteMoniker,
    ) -> Result<Arc<Realm>, ModelError> {
        let mut cur_realm = self.root_realm.clone();
        for moniker in look_up_abs_moniker.path().iter() {
            cur_realm = {
                Realm::resolve_decl(&cur_realm).await?;
                let cur_state = cur_realm.lock_state().await;
                let cur_state = cur_state.as_ref().expect("look_up_realm: not resolved");
                if let Some(r) = cur_state.all_child_realms().get(moniker) {
                    r.clone()
                } else {
                    return Err(ModelError::instance_not_found(look_up_abs_moniker.clone()));
                }
            };
        }
        Realm::resolve_decl(&cur_realm).await?;
        Ok(cur_realm)
    }

    /// Binds to the component instance with the specified moniker. This has the following effects:
    /// - Binds to the parent instance.
    /// - Starts the component instance, if it is not already running and not shut down.
    /// - Binds to any descendant component instances that need to be eagerly started.
    // TODO: This function starts the parent component, but doesn't track the bindings anywhere.
    // This means that when the child stops and the parent has no other reason to run, we won't
    // stop the parent. To solve this, we need to track the bindings.
    pub async fn bind<'a>(&'a self, abs_moniker: &'a AbsoluteMoniker) -> Result<(), ModelError> {
        async fn bind_one(model: &Model, m: AbsoluteMoniker) -> Result<(), ModelError> {
            let realm = model.look_up_realm(&m).await?;
            let eager_children = model.bind_single_instance(realm).await?;
            // If the bind to this realm's instance succeeded but the child is shut down, allow
            // the call to succeed. If the child is shut down, that shouldn't cause the client to
            // believe the bind to `abs_moniker` failed.
            //
            // TODO: Have a more general strategy for dealing with errors from eager binding. Should
            // we ever pass along the error?
            model.bind_eager_children_recursive(eager_children).await.or_else(|e| match e {
                ModelError::InstanceShutDown { .. } => Ok(()),
                _ => Err(e),
            })?;
            Ok(())
        }
        let mut cur_moniker = AbsoluteMoniker::root();
        bind_one(self, cur_moniker.clone()).await?;
        for m in abs_moniker.path().iter() {
            cur_moniker = cur_moniker.child(m.clone());
            bind_one(self, cur_moniker.clone()).await?;
        }
        Ok(())
    }

    /// Binds to the component instance in the given realm, starting it if it's not already
    /// running. Returns the list of child realms whose instances need to be eagerly started after
    /// this function returns. The caller is responsible for calling
    /// `bind_eager_children_recursive` to ensure eager children are recursively binded.
    async fn bind_single_instance(&self, realm: Arc<Realm>) -> Result<Vec<Arc<Realm>>, ModelError> {
        let component = realm.resolver_registry.resolve(&realm.component_url).await?;
        // The realm's lock needs to be held during `Runner::start` until the `Execution` is set in
        // case there are concurrent calls to `bind_single_instance`.
        let decl = {
            let mut state = realm.lock_state().await;
            if state.is_none() {
                *state = Some(RealmState::new(&*realm, component.decl).await?);
            }
            state.as_ref().unwrap().decl().clone()
        };

        // Fetch the component's runner.
        let runner = realm.resolve_runner(self).await?;
        {
            let mut execution = realm.lock_execution().await;
            if execution.is_shut_down() {
                return Err(ModelError::instance_shut_down(realm.abs_moniker.clone()));
            }
            if execution.runtime.is_some() {
                // TODO: Add binding to the execution once we track bindings.
                return Ok(vec![]);
            }
            execution.runtime = Some(
                self.init_execution_runtime(
                    &realm.abs_moniker,
                    component.resolved_url.ok_or(ModelError::ComponentInvalid)?,
                    component.package,
                    &decl,
                    runner.as_ref(),
                )
                .await?,
            );
        }
        let (event, eager_child_realms) = {
            let routing_facade = RoutingFacade::new(self.clone());
            let mut state = realm.lock_state().await;
            let state = state.as_mut().expect("bind_single_instance: not resolved");
            let eager_child_realms: Vec<_> = state
                .live_child_realms()
                .filter_map(|(_, r)| match r.startup {
                    fsys::StartupMode::Eager => Some(r.clone()),
                    fsys::StartupMode::Lazy => None,
                })
                .collect();
            let live_child_realms = state.live_child_realms().map(|(_, r)| r.clone()).collect();
            let event = Event::StartInstance {
                realm: realm.clone(),
                component_decl: state.decl().clone(),
                live_child_realms,
                routing_facade,
            };
            (event, eager_child_realms)
        };
        realm.hooks.dispatch(&event).await?;
        Ok(eager_child_realms)
    }

    /// Binds to a list of instances, and any eager children they may return.
    async fn bind_eager_children_recursive<'a>(
        &'a self,
        mut instances_to_bind: Vec<Arc<Realm>>,
    ) -> Result<(), ModelError> {
        loop {
            if instances_to_bind.is_empty() {
                break;
            }
            let futures: Vec<_> = instances_to_bind
                .iter()
                .map(|realm| {
                    Box::pin(async move { self.bind_single_instance(realm.clone()).await })
                })
                .collect();
            let res = join_all(futures).await;
            instances_to_bind.clear();
            for e in res {
                instances_to_bind.append(&mut e?);
            }
        }
        Ok(())
    }

    /// Returns a configured Runtime for a component.
    async fn init_execution_runtime(
        &self,
        abs_moniker: &AbsoluteMoniker,
        url: String,
        package: Option<fsys::Package>,
        decl: &cm_rust::ComponentDecl,
        runner: &(dyn Runner + Send + Sync),
    ) -> Result<Runtime, ModelError> {
        // Create incoming/outgoing directories, and populate them.
        let exposed_dir = ExposedDir::new(self, abs_moniker, decl.clone())?;
        let (outgoing_dir_client, outgoing_dir_server) =
            zx::Channel::create().map_err(|e| ModelError::namespace_creation_failed(e))?;
        let (runtime_dir_client, runtime_dir_server) =
            zx::Channel::create().map_err(|e| ModelError::namespace_creation_failed(e))?;
        let mut namespace = IncomingNamespace::new(package)?;
        let ns = namespace.populate(self.clone(), abs_moniker, decl).await?;

        let (client_endpoint, server_endpoint) =
            create_endpoints::<fsys::ComponentControllerMarker>()
                .expect("could not create component controller endpoints");
        let controller =
            client_endpoint.into_proxy().expect("failed to create ComponentControllerProxy");
        // Set up channels into/out of the new component.
        let runtime = Runtime::start_from(
            url,
            Some(namespace),
            Some(DirectoryProxy::from_channel(
                fasync::Channel::from_channel(outgoing_dir_client).unwrap(),
            )),
            Some(DirectoryProxy::from_channel(
                fasync::Channel::from_channel(runtime_dir_client).unwrap(),
            )),
            exposed_dir,
            Some(controller),
        )?;
        let start_info = fsys::ComponentStartInfo {
            resolved_url: Some(runtime.resolved_url.clone()),
            program: data::clone_option_dictionary(&decl.program),
            ns: Some(ns),
            outgoing_dir: Some(ServerEnd::new(outgoing_dir_server)),
            runtime_dir: Some(ServerEnd::new(runtime_dir_server)),
        };

        // Ask the runner to launch the runtime.
        runner.start(start_info, server_endpoint).await?;

        Ok(runtime)
    }
}

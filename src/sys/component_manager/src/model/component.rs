// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{
        actions::{
            shutdown, start, ActionSet, DestroyChildAction, DiscoverAction, ResolveAction,
            StartAction, StopAction, UnresolveAction,
        },
        context::{ModelContext, WeakModelContext},
        environment::Environment,
        error::ModelError,
        exposed_dir::ExposedDir,
        hooks::{Event, EventPayload, Hooks},
        namespace::{populate_and_get_logsink_decl, IncomingNamespace},
        ns_dir::NamespaceDir,
        routing::{
            self, route_and_open_capability, OpenOptions, OpenResourceError, OpenRunnerOptions,
            RouteRequest, RoutingError,
        },
    },
    ::routing::{
        capability_source::{BuiltinCapabilities, NamespaceCapabilities},
        component_id_index::{ComponentIdIndex, ComponentInstanceId},
        component_instance::{
            ComponentInstanceInterface, ExtendedInstanceInterface, ResolvedInstanceInterface,
            TopInstanceInterface, WeakComponentInstanceInterface, WeakExtendedInstanceInterface,
        },
        environment::EnvironmentInterface,
        error::ComponentInstanceError,
        policy::GlobalPolicyChecker,
        resolving::{
            ComponentAddress, ComponentResolutionContext, ResolvedComponent, ResolvedPackage,
        },
        DebugRouteMapper,
    },
    anyhow::format_err,
    async_trait::async_trait,
    clonable_error::ClonableError,
    cm_moniker::{IncarnationId, InstancedAbsoluteMoniker, InstancedChildMoniker},
    cm_runner::{component_controller::ComponentController, NullRunner, RemoteRunner, Runner},
    cm_rust::{
        self, CapabilityName, ChildDecl, CollectionDecl, ComponentDecl, NativeIntoFidl, UseDecl,
    },
    cm_task_scope::TaskScope,
    cm_util::channel,
    config_encoder::ConfigFields,
    fidl::endpoints::{self, ClientEnd, Proxy, ServerEnd},
    fidl_fuchsia_component as fcomponent, fidl_fuchsia_component_decl as fdecl,
    fidl_fuchsia_component_runner as fcrunner,
    fidl_fuchsia_hardware_power_statecontrol as fstatecontrol, fidl_fuchsia_io as fio,
    fidl_fuchsia_process as fprocess, fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync,
    fuchsia_component::client,
    fuchsia_zircon as zx,
    futures::{
        future::{join_all, AbortHandle, Abortable, BoxFuture, Either, FutureExt, TryFutureExt},
        lock::{MappedMutexGuard, Mutex, MutexGuard},
    },
    moniker::{AbsoluteMoniker, AbsoluteMonikerBase, ChildMoniker, ChildMonikerBase},
    std::iter::Iterator,
    std::{
        boxed::Box,
        clone::Clone,
        collections::{HashMap, HashSet},
        convert::{TryFrom, TryInto},
        fmt,
        ops::Drop,
        path::PathBuf,
        sync::{Arc, Weak},
        time::Duration,
    },
    tracing::{error, warn},
    vfs::{execution_scope::ExecutionScope, path::Path},
};

pub type WeakComponentInstance = WeakComponentInstanceInterface<ComponentInstance>;
pub type ExtendedInstance = ExtendedInstanceInterface<ComponentInstance>;
pub type WeakExtendedInstance = WeakExtendedInstanceInterface<ComponentInstance>;

/// Describes the reason a component instance is being requested to start.
#[derive(Clone, Debug, Hash, PartialEq, Eq)]
pub enum StartReason {
    /// Indicates that the target is starting the component because it wishes to access
    /// the capability at path.
    AccessCapability { target: AbsoluteMoniker, name: CapabilityName },
    /// Indicates that the component is starting because it is in a single-run collection.
    SingleRun,
    /// Indicates that the component was explicitly started for debugging purposes.
    Debug,
    /// Indicates that the component was marked as eagerly starting by the parent.
    // TODO(fxbug.dev/50714): Include the parent StartReason.
    // parent: ExtendedMoniker,
    // parent_start_reason: Option<Arc<StartReason>>
    Eager,
    /// Indicates that this component is starting because it is the root component.
    Root,
    /// Storage administration is occurring on this component.
    StorageAdmin,
}

impl fmt::Display for StartReason {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{}",
            match self {
                StartReason::AccessCapability { target, name } => {
                    format!("'{}' requested capability '{}'", target, name)
                }
                StartReason::SingleRun => "Instance is in a single_run collection".to_string(),
                StartReason::Debug => "Instance was started from debugging workflow".to_string(),
                StartReason::Eager => "Instance is an eager child".to_string(),
                StartReason::Root => "Instance is the root".to_string(),
                StartReason::StorageAdmin => "Storage administration on instance".to_string(),
            }
        )
    }
}

/// Component information returned by the resolver.
#[derive(Clone, Debug)]
pub struct Component {
    /// The URL of the resolved component.
    pub resolved_url: String,
    /// The context to be used to resolve a component from a path
    /// relative to this component (for example, a component in a subpackage).
    /// If `None`, the resolver cannot resolve relative path component URLs.
    pub context_to_resolve_children: Option<ComponentResolutionContext>,
    /// The declaration of the resolved manifest.
    pub decl: ComponentDecl,
    /// The package info, if the component came from a package.
    pub package: Option<Package>,
    /// The components validated configuration. If None, no configuration was provided.
    pub config: Option<ConfigFields>,
}

/// Package information possibly returned by the resolver.
#[derive(Clone, Debug)]
pub struct Package {
    /// The URL of the package itself.
    pub package_url: String,
    /// The package that this resolved component belongs to
    pub package_dir: fio::DirectoryProxy,
}

impl TryFrom<ResolvedComponent> for Component {
    type Error = ModelError;

    fn try_from(
        ResolvedComponent {
            resolved_by: _,
            resolved_url,
            context_to_resolve_children,
            decl,
            package,
            config_values,
        }: ResolvedComponent,
    ) -> Result<Self, Self::Error> {
        // Verify the component configuration, if it exists
        let config = if let Some(config_decl) = decl.config.as_ref() {
            let values = config_values.ok_or(ModelError::ConfigValuesMissing)?;
            let config = ConfigFields::resolve(config_decl, values)
                .map_err(ModelError::ConfigResolutionFailed)?;
            Some(config)
        } else {
            None
        };

        let package = package.map(|p| p.try_into()).transpose()?;
        Ok(Self { resolved_url, context_to_resolve_children, decl, package, config })
    }
}

impl TryFrom<ResolvedPackage> for Package {
    type Error = ModelError;

    fn try_from(package: ResolvedPackage) -> Result<Self, Self::Error> {
        Ok(Self {
            package_url: package.url,
            package_dir: package
                .directory
                .into_proxy()
                .expect("could not convert package dir to proxy"),
        })
    }
}

pub const DEFAULT_KILL_TIMEOUT: Duration = Duration::from_secs(1);

/// A special instance identified with component manager, at the top of the tree.
#[derive(Debug)]
pub struct ComponentManagerInstance {
    /// The list of capabilities offered from component manager's namespace.
    pub namespace_capabilities: NamespaceCapabilities,

    /// The list of capabilities offered from component manager as built-in capabilities.
    pub builtin_capabilities: BuiltinCapabilities,

    /// Tasks owned by component manager's instance.
    task_scope: TaskScope,

    /// Mutable state for component manager's instance.
    state: Mutex<ComponentManagerInstanceState>,
}

/// Mutable state for component manager's instance.
pub struct ComponentManagerInstanceState {
    /// The root component instance, this instance's only child.
    root: Option<Arc<ComponentInstance>>,

    /// Task that is rebooting the system, if any.
    reboot_task: Option<fasync::Task<()>>,
}

impl ComponentManagerInstance {
    pub fn new(
        namespace_capabilities: NamespaceCapabilities,
        builtin_capabilities: BuiltinCapabilities,
    ) -> Self {
        Self {
            namespace_capabilities,
            builtin_capabilities,
            state: Mutex::new(ComponentManagerInstanceState::new()),
            task_scope: TaskScope::new(),
        }
    }

    /// Returns a scope for this instance where tasks can be run
    pub fn task_scope(&self) -> TaskScope {
        self.task_scope.clone()
    }

    #[cfg(test)]
    pub async fn has_reboot_task(&self) -> bool {
        self.state.lock().await.reboot_task.is_some()
    }

    /// Returns the root component instance.
    ///
    /// REQUIRES: The root has already been set. Otherwise panics.
    pub async fn root(&self) -> Arc<ComponentInstance> {
        self.state.lock().await.root.as_ref().expect("root not set").clone()
    }

    /// Initializes the state of the instance. Panics if already initialized.
    pub(super) async fn init(&self, root: Arc<ComponentInstance>) {
        let mut state = self.state.lock().await;
        assert!(state.root.is_none(), "child of top instance already set");
        state.root = Some(root);
    }

    /// Triggers a graceful system reboot. Panics if the reboot call fails (which will trigger a
    /// forceful reboot if this is the root component manager instance).
    ///
    /// Returns as soon as the call has been made. In the background, component_manager will wait
    /// on the `Reboot` call.
    pub(super) async fn trigger_reboot(self: &Arc<Self>) {
        let mut state = self.state.lock().await;
        if state.reboot_task.is_some() {
            // Reboot task was already scheduled, nothing to do.
            return;
        }
        let this = self.clone();
        state.reboot_task = Some(fasync::Task::spawn(async move {
            let res = async move {
                let statecontrol_proxy = this.connect_to_statecontrol_admin().await?;
                statecontrol_proxy
                    .reboot(fstatecontrol::RebootReason::CriticalComponentFailure)
                    .await
                    .map_err(|e| ModelError::reboot_failed(e))
                    .and_then(|res| {
                        res.map_err(|s| {
                            ModelError::reboot_failed(format_err!(
                                "Admin/Reboot failed with status: {}",
                                zx::Status::from_raw(s)
                            ))
                        })
                    })
            }
            .await;
            if let Err(e) = res {
                // TODO(fxbug.dev/81115): Instead of panicking, we could fall back more gently by
                // triggering component_manager's shutdown.
                panic!(
                    "Component with on_terminate=REBOOT terminated, but triggering \
                    reboot failed. Crashing component_manager instead: {}",
                    e
                );
            }
        }));
    }

    /// Obtains a connection to power_manager's `statecontrol` protocol.
    async fn connect_to_statecontrol_admin(&self) -> Result<fstatecontrol::AdminProxy, ModelError> {
        let (exposed_dir, server) =
            endpoints::create_proxy::<fio::DirectoryMarker>().expect("failed to create proxy");
        let mut server = server.into_channel();
        let root = self.root().await;
        root.open_exposed(&mut server).await?;
        let statecontrol_proxy =
            client::connect_to_protocol_at_dir_root::<fstatecontrol::AdminMarker>(&exposed_dir)
                .map_err(|e| ModelError::reboot_failed(e))?;
        Ok(statecontrol_proxy)
    }
}

impl ComponentManagerInstanceState {
    pub fn new() -> Self {
        Self { reboot_task: None, root: None }
    }
}

impl TopInstanceInterface for ComponentManagerInstance {
    fn namespace_capabilities(&self) -> &NamespaceCapabilities {
        &self.namespace_capabilities
    }

    fn builtin_capabilities(&self) -> &BuiltinCapabilities {
        &self.builtin_capabilities
    }
}

/// Models a component instance, possibly with links to children.
pub struct ComponentInstance {
    /// The registry for resolving component URLs within the component instance.
    pub environment: Arc<Environment>,
    /// The component's URL.
    pub component_url: String,
    /// The mode of startup (lazy or eager).
    pub startup: fdecl::StartupMode,
    /// The policy to apply if the component terminates.
    pub on_terminate: fdecl::OnTerminate,
    /// The parent instance. Either a component instance or component manager's instance.
    pub parent: WeakExtendedInstance,
    /// The instanced absolute moniker of this instance.
    instanced_moniker: InstancedAbsoluteMoniker,
    /// The partial absolute moniker of this instance.
    pub abs_moniker: AbsoluteMoniker,
    /// The hooks scoped to this instance.
    pub hooks: Arc<Hooks>,
    /// Numbered handles to pass to the component on startup. These handles
    /// should only be present for components that run in collections with a
    /// `SingleRun` durability.
    pub numbered_handles: Mutex<Option<Vec<fprocess::HandleInfo>>>,
    /// Whether to persist isolated storage data of this component instance after it has been
    /// destroyed.
    pub persistent_storage: bool,

    /// The context this instance is under.
    context: WeakModelContext,

    // These locks must be taken in the order declared if held simultaneously.
    /// The component's mutable state.
    state: Mutex<InstanceState>,
    /// The component's execution state.
    execution: Mutex<ExecutionState>,
    /// Actions on the instance that must eventually be completed.
    actions: Mutex<ActionSet>,
    /// Tasks owned by this component instance.
    task_scope: TaskScope,
}

impl ComponentInstance {
    /// Instantiates a new root component instance.
    pub fn new_root(
        environment: Environment,
        context: Weak<ModelContext>,
        component_manager_instance: Weak<ComponentManagerInstance>,
        component_url: String,
    ) -> Arc<Self> {
        Self::new(
            Arc::new(environment),
            InstancedAbsoluteMoniker::root(),
            component_url,
            fdecl::StartupMode::Lazy,
            fdecl::OnTerminate::None,
            WeakModelContext::new(context),
            WeakExtendedInstance::AboveRoot(component_manager_instance),
            Arc::new(Hooks::new()),
            None,
            false,
        )
    }

    /// Instantiates a new component instance with the given contents.
    pub fn new(
        environment: Arc<Environment>,
        instanced_moniker: InstancedAbsoluteMoniker,
        component_url: String,
        startup: fdecl::StartupMode,
        on_terminate: fdecl::OnTerminate,
        context: WeakModelContext,
        parent: WeakExtendedInstance,
        hooks: Arc<Hooks>,
        numbered_handles: Option<Vec<fprocess::HandleInfo>>,
        persistent_storage: bool,
    ) -> Arc<Self> {
        let abs_moniker = instanced_moniker.without_instance_ids();
        Arc::new(Self {
            environment,
            instanced_moniker,
            abs_moniker,
            component_url,
            startup,
            on_terminate,
            context,
            parent,
            state: Mutex::new(InstanceState::New),
            execution: Mutex::new(ExecutionState::new()),
            actions: Mutex::new(ActionSet::new()),
            hooks,
            task_scope: TaskScope::new(),
            numbered_handles: Mutex::new(numbered_handles),
            persistent_storage,
        })
    }

    /// Locks and returns the instance's mutable state.
    pub async fn lock_state(&self) -> MutexGuard<'_, InstanceState> {
        self.state.lock().await
    }

    /// Locks and returns the instance's execution state.
    pub async fn lock_execution(&self) -> MutexGuard<'_, ExecutionState> {
        self.execution.lock().await
    }

    /// Locks and returns the instance's action set.
    pub async fn lock_actions(&self) -> MutexGuard<'_, ActionSet> {
        self.actions.lock().await
    }

    /// Gets the context, if it exists, or returns a `ContextNotFound` error.
    pub fn try_get_context(&self) -> Result<Arc<ModelContext>, ModelError> {
        self.context.upgrade()
    }

    /// Returns a scope for this instance where tasks can be run
    pub fn task_scope(&self) -> TaskScope {
        self.task_scope.clone()
    }

    /// Locks and returns a lazily resolved and populated `ResolvedInstanceState`. Does not
    /// register a `Resolve` action unless the resolved state is not already populated, so this
    /// function can be called re-entrantly from a Resolved hook. Returns an `InstanceNotFound`
    /// error if the instance is destroyed.
    pub async fn lock_resolved_state<'a>(
        self: &'a Arc<Self>,
    ) -> Result<MappedMutexGuard<'a, InstanceState, ResolvedInstanceState>, ComponentInstanceError>
    {
        fn get_resolved(s: &mut InstanceState) -> &mut ResolvedInstanceState {
            match s {
                InstanceState::Resolved(s) => s,
                _ => panic!("not resolved"),
            }
        }
        {
            let state = self.state.lock().await;
            match *state {
                InstanceState::Resolved(_) => {
                    return Ok(MutexGuard::map(state, get_resolved));
                }
                InstanceState::Destroyed => {
                    return Err(ComponentInstanceError::instance_not_found(
                        self.abs_moniker.clone(),
                    ));
                }
                InstanceState::New | InstanceState::Unresolved => {}
            }
            // Drop the lock before doing the work to resolve the state.
        }
        self.resolve()
            .await
            .map_err(|err| ComponentInstanceError::resolve_failed(self.abs_moniker.clone(), err))?;
        let state = self.state.lock().await;
        if let InstanceState::Destroyed = *state {
            return Err(ComponentInstanceError::instance_not_found(self.abs_moniker.clone()));
        }
        Ok(MutexGuard::map(state, get_resolved))
    }

    /// Resolves the component declaration, populating `ResolvedInstanceState` as necessary. A
    /// `Resolved` event is dispatched if the instance was not previously resolved or an error
    /// occurs.
    pub async fn resolve(self: &Arc<Self>) -> Result<Component, ModelError> {
        ActionSet::register(self.clone(), ResolveAction::new()).await
    }

    /// Unresolves the component using an UnresolveAction. The component will be shut down, then
    /// reset to the Discovered state without being destroyed. An Unresolved event is dispatched on
    /// success or error.
    pub async fn unresolve(self: &Arc<Self>) -> Result<(), ModelError> {
        ActionSet::register(self.clone(), UnresolveAction::new()).await
    }

    /// Locks on the instance and execution state of the component and creates a FIDL
    /// fuchsia.sys2.ResolvedState object.
    pub async fn create_fidl_resolved_state(
        self: &Arc<ComponentInstance>,
    ) -> Option<Box<fsys::ResolvedState>> {
        let state = self.lock_state().await;
        let execution = self.lock_execution().await;

        match &*state {
            InstanceState::Resolved(r) => {
                let uses =
                    r.decl().uses.clone().into_iter().map(|u| u.native_into_fidl()).collect();
                let exposes =
                    r.decl().exposes.clone().into_iter().map(|e| e.native_into_fidl()).collect();
                let config = r.config().cloned().map(|c| Box::new(c.into()));

                let pkg_dir = r.package().map(|p| &p.package_dir);
                let pkg_dir = try_clone_dir_endpoint(pkg_dir);

                let execution = if let Some(runtime) = &execution.runtime {
                    let out_dir = try_clone_dir_endpoint(runtime.outgoing_dir.as_ref());
                    let runtime_dir = try_clone_dir_endpoint(runtime.runtime_dir.as_ref());
                    let start_reason = runtime.start_reason.to_string();

                    Some(Box::new(fsys::ExecutionState { out_dir, runtime_dir, start_reason }))
                } else {
                    None
                };

                let (exposed_dir, expose_server) = fidl::endpoints::create_endpoints().unwrap();
                r.get_exposed_dir().open(
                    fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
                    fio::MODE_TYPE_DIRECTORY,
                    vfs::path::Path::dot(),
                    expose_server,
                );
                let exposed_dir = exposed_dir.into_channel();
                let exposed_dir =
                    fidl::endpoints::ClientEnd::<fio::DirectoryMarker>::new(exposed_dir);

                let (ns_dir, ns_server) = fidl::endpoints::create_endpoints().unwrap();
                r.get_ns_dir().open(
                    fio::OpenFlags::RIGHT_READABLE
                        | fio::OpenFlags::RIGHT_WRITABLE
                        | fio::OpenFlags::RIGHT_EXECUTABLE,
                    fio::MODE_TYPE_DIRECTORY,
                    vfs::path::Path::dot(),
                    ns_server,
                );
                let ns_dir = ns_dir.into_channel();
                let ns_dir = fidl::endpoints::ClientEnd::<fio::DirectoryMarker>::new(ns_dir);

                Some(Box::new(fsys::ResolvedState {
                    uses,
                    exposes,
                    config,
                    pkg_dir,
                    execution,
                    exposed_dir,
                    ns_dir,
                }))
            }
            _ => None,
        }
    }

    /// Locks on the instance and execution state of the component and creates a FIDL
    /// fuchsia.sys2.ResolvedDirectories object.
    pub async fn create_fidl_resolved_directories(
        self: &Arc<Self>,
    ) -> Option<Box<fsys::ResolvedDirectories>> {
        let mut state = self.lock_state().await;
        let execution = self.lock_execution().await;

        match &mut *state {
            InstanceState::Resolved(r) => {
                let pkg_dir = r.package().map(|p| &p.package_dir);
                let pkg_dir_endpoint = try_clone_dir_endpoint(pkg_dir);

                let execution_dirs = if let Some(runtime) = &execution.runtime {
                    let out_dir = try_clone_dir_endpoint(runtime.outgoing_dir.as_ref());
                    let runtime_dir = try_clone_dir_endpoint(runtime.runtime_dir.as_ref());

                    Some(Box::new(fsys::ExecutionDirectories { out_dir, runtime_dir }))
                } else {
                    None
                };

                let (exposed_dir, expose_server) = fidl::endpoints::create_endpoints().unwrap();
                r.get_exposed_dir().open(
                    fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
                    fio::MODE_TYPE_DIRECTORY,
                    vfs::path::Path::dot(),
                    expose_server,
                );
                let exposed_dir = exposed_dir.into_channel();
                let exposed_dir =
                    fidl::endpoints::ClientEnd::<fio::DirectoryMarker>::new(exposed_dir);

                let pkg_dir = r.package().map(|p| &p.package_dir);
                let (ns_entries, _) =
                    populate_and_get_logsink_decl(pkg_dir, self.as_weak(), r.decl()).await.unwrap();

                Some(Box::new(fsys::ResolvedDirectories {
                    ns_entries,
                    pkg_dir: pkg_dir_endpoint,
                    exposed_dir,
                    execution_dirs,
                }))
            }
            _ => None,
        }
    }

    /// Resolves a runner for this component.
    //
    // We use an explicit `BoxFuture` here instead of a standard async
    // function because we may need to recurse to resolve the runner:
    //
    //   resolve_runner -> open_capability_at_source -> bind -> resolve_runner
    //
    // Rust 1.40 doesn't support recursive async functions, so we
    // manually write out the type.
    pub fn resolve_runner<'a>(
        self: &'a Arc<Self>,
    ) -> BoxFuture<'a, Result<Arc<dyn Runner>, ModelError>> {
        async move {
            // Fetch component declaration.
            let decl = {
                let state = self.lock_state().await;
                match *state {
                    InstanceState::Resolved(ref s) => s.decl.clone(),
                    InstanceState::Destroyed => {
                        return Err(ModelError::instance_not_found(self.abs_moniker.clone()));
                    }
                    _ => {
                        panic!("resolve_runner: not resolved")
                    }
                }
            };

            // Find any explicit "use" runner declaration, resolve that.
            if let Some(runner) = decl.get_runner() {
                // Open up a channel to the runner.
                let (client_channel, server_channel) =
                    endpoints::create_endpoints::<fcrunner::ComponentRunnerMarker>()
                        .map_err(|_| ModelError::InsufficientResources)?;
                let mut server_channel = server_channel.into_channel();
                let options = OpenRunnerOptions {
                    flags: fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
                    open_mode: fio::MODE_TYPE_SERVICE,
                    server_chan: &mut server_channel,
                };
                route_and_open_capability(
                    RouteRequest::Runner(runner.clone()),
                    self,
                    OpenOptions::Runner(options),
                )
                .await?;

                return Ok(Arc::new(RemoteRunner::new(client_channel.into_proxy().unwrap()))
                    as Arc<dyn Runner>);
            }

            // Otherwise, use a null runner.
            Ok(Arc::new(NullRunner {}) as Arc<dyn Runner>)
        }
        .boxed()
    }

    /// Adds the dynamic child defined by `child_decl` to the given `collection_name`. Once
    /// added, the component instance exists but is not bound.
    ///
    /// Returns the collection durability of the collection the child was added to.
    pub async fn add_dynamic_child(
        self: &Arc<Self>,
        collection_name: String,
        child_decl: &ChildDecl,
        child_args: fcomponent::CreateChildArgs,
    ) -> Result<fdecl::Durability, ModelError> {
        match child_decl.startup {
            fdecl::StartupMode::Lazy => {}
            fdecl::StartupMode::Eager => {
                return Err(ModelError::unsupported("Eager startup"));
            }
        }
        let mut state = self.lock_resolved_state().await?;
        let collection_decl = state
            .decl()
            .find_collection(&collection_name)
            .ok_or_else(|| ModelError::collection_not_found(collection_name.clone()))?
            .clone();

        if let Some(handles) = &child_args.numbered_handles {
            if !handles.is_empty() && collection_decl.durability != fdecl::Durability::SingleRun {
                return Err(ModelError::unsupported(
                    "Numbered handles to child in a collection that is not SingleRun",
                ));
            }
        }

        if !collection_decl.allow_long_names && child_decl.name.len() > cm_types::MAX_NAME_LENGTH {
            return Err(ModelError::name_too_long(cm_types::MAX_NAME_LENGTH));
        }

        let dynamic_offers = child_args.dynamic_offers.map(|dynamic_offers| {
            if !dynamic_offers.is_empty()
                && collection_decl.allowed_offers != cm_types::AllowedOffers::StaticAndDynamic
            {
                return Err(ModelError::dynamic_offers_not_allowed(&collection_name));
            }

            cm_fidl_validator::validate_dynamic_offers(&dynamic_offers)
                .map_err(ModelError::dynamic_offer_invalid)?;

            dynamic_offers
                .into_iter()
                .map(|mut offer| {
                    use ::routing::component_instance::ResolvedInstanceInterfaceExt;
                    use cm_rust::OfferDeclCommon;

                    // Set the `target` field to point to the component
                    // we're creating. `fidl_into_native()` requires
                    // `target` to be set.
                    *offer_target_mut(&mut offer)
                        .expect("validation should have found unknown enum type") =
                        Some(fdecl::Ref::Child(fdecl::ChildRef {
                            name: child_decl.name.clone(),
                            collection: Some(collection_name.clone()),
                        }));
                    // This is safe because of the call to
                    // `validate_dynamic_offers` above.
                    let offer = cm_rust::FidlIntoNative::fidl_into_native(offer);

                    // The sources and targets of offers in CFv2 must always exist. For static
                    // offers, this is ensured by `cm_fidl_validator`. For dynamic offers, we
                    // check that the source exists here. The target _will_ exist by virtue of
                    // the fact that we're creating it now.
                    if !state.offer_source_exists(offer.source()) {
                        return Err(ModelError::dynamic_offer_source_not_found(offer.clone()));
                    }
                    Ok(offer)
                })
                .collect()
        });
        let dynamic_offers = dynamic_offers.transpose()?;
        let durability_nf = state
            .add_child(
                self,
                child_decl,
                Some(&collection_decl),
                child_args.numbered_handles,
                dynamic_offers,
            )
            .await?;
        durability_nf.await?;
        Ok(collection_decl.durability)
    }

    /// Removes the dynamic child, returning a future that will execute the
    /// destroy action.
    pub async fn remove_dynamic_child(
        self: &Arc<Self>,
        child_moniker: &ChildMoniker,
    ) -> Result<(), ModelError> {
        let incarnation = {
            let state = self.lock_resolved_state().await?;
            if let Some(c) = state.get_child(&child_moniker) {
                c.incarnation_id()
            } else {
                return Err(ModelError::instance_not_found_in_realm(
                    self.abs_moniker.clone(),
                    child_moniker.clone(),
                ));
            }
        };
        ActionSet::register(
            self.clone(),
            DestroyChildAction::new(child_moniker.clone(), incarnation),
        )
        .await
    }

    /// Performs the stop protocol for this component instance. `shut_down` determines whether
    /// the instance is to be put in the shutdown state; see documentation on [ExecutionState].
    ///
    /// REQUIRES: All dependents have already been stopped.
    pub async fn stop_instance(
        self: &Arc<Self>,
        shut_down: bool,
        is_recursive: bool,
    ) -> Result<(), ModelError> {
        let (was_running, stop_result) = {
            let mut execution = self.lock_execution().await;
            let was_running = execution.runtime.is_some();
            let shut_down = execution.shut_down | shut_down;

            let component_stop_result = {
                if let Some(runtime) = &mut execution.runtime {
                    let stop_timer = Box::pin(async move {
                        let timer = fasync::Timer::new(fasync::Time::after(zx::Duration::from(
                            self.environment.stop_timeout(),
                        )));
                        timer.await;
                    });
                    let kill_timer = Box::pin(async move {
                        let timer = fasync::Timer::new(fasync::Time::after(zx::Duration::from(
                            DEFAULT_KILL_TIMEOUT,
                        )));
                        timer.await;
                    });
                    let ret =
                        runtime.stop_component(stop_timer, kill_timer).await.map_err(|e| {
                            ModelError::RunnerCommunicationError {
                                moniker: self.abs_moniker.clone(),
                                operation: "stop".to_string(),
                                err: ClonableError::from(anyhow::Error::from(e)),
                            }
                        })?;
                    if ret.request == StopRequestSuccess::KilledAfterTimeout
                        || ret.request == StopRequestSuccess::Killed
                    {
                        warn!(
                            "component {} did not stop in {:?}. Killed it.",
                            self.abs_moniker,
                            self.environment.stop_timeout()
                        );
                    }
                    if !shut_down && self.on_terminate == fdecl::OnTerminate::Reboot {
                        warn!(
                            "Component with on_terminate=REBOOT terminated: {}. \
                            Rebooting the system",
                            self.abs_moniker
                        );
                        let top_instance = self.top_instance().await?;
                        top_instance.trigger_reboot().await;
                    }
                    ret.component_exit_status
                } else {
                    zx::Status::PEER_CLOSED
                }
            };

            execution.shut_down = shut_down;
            execution.runtime = None;

            (was_running, component_stop_result)
        };

        // If is_recursive is true, stop all the child instances of the component.
        if is_recursive {
            self.stop_children(shut_down, is_recursive).await?;
        }

        // When the component is stopped, any child instances in collections must be destroyed.
        self.destroy_dynamic_children().await?;
        if was_running {
            let event = Event::new(self, Ok(EventPayload::Stopped { status: stop_result }));
            self.hooks.dispatch(&event).await?;
        }
        if let ExtendedInstance::Component(parent) = self.try_get_parent()? {
            parent
                .destroy_child_if_single_run(
                    self.child_moniker().expect("child is root instance?"),
                    self.incarnation_id(),
                )
                .await?;
        }
        Ok(())
    }

    async fn destroy_child_if_single_run(
        self: &Arc<Self>,
        child_moniker: &ChildMoniker,
        incarnation: IncarnationId,
    ) -> Result<(), ModelError> {
        let single_run_colls = {
            let state = self.lock_state().await;
            let state = match *state {
                InstanceState::Resolved(ref s) => s,
                _ => {
                    // Component instance was not resolved, so no dynamic children.
                    return Ok(());
                }
            };
            let single_run_colls: HashSet<_> = state
                .decl()
                .collections
                .iter()
                .filter_map(|c| match c.durability {
                    fdecl::Durability::SingleRun => Some(c.name.clone()),
                    fdecl::Durability::Transient => None,
                })
                .collect();
            single_run_colls
        };
        if let Some(coll) = child_moniker.collection() {
            if single_run_colls.contains(coll) {
                let component = self.clone();
                let child_moniker = child_moniker.clone();
                fasync::Task::spawn(async move {
                    match ActionSet::register(
                        component.clone(),
                        DestroyChildAction::new(child_moniker, incarnation),
                    )
                    .await
                    {
                        Ok(_) => {}
                        Err(e) => {
                            warn!(
                                "component {} was not destroyed when stopped in single run collection: {}",
                                component.abs_moniker, e
                            );
                        }
                    }
                })
                .detach();
            }
        }
        Ok(())
    }

    /// Registers actions to stop all the children of this instance.
    async fn stop_children(
        self: &Arc<Self>,
        shut_down: bool,
        is_recursive: bool,
    ) -> Result<(), ModelError> {
        let child_instances: Vec<_> = {
            let state = self.lock_resolved_state().await?;
            state.children().map(|(_, v)| v.clone()).collect()
        };

        let mut futures = vec![];
        for child_instance in child_instances {
            let nf = ActionSet::register(
                child_instance.clone(),
                StopAction::new(shut_down, is_recursive),
            );
            futures.push(nf);
        }
        join_all(futures).await.into_iter().fold(Ok(()), |acc, r| acc.and_then(|_| r))
    }

    /// Destroys this component instance.
    /// REQUIRES: All children have already been destroyed.
    pub async fn destroy_instance(self: &Arc<Self>) -> Result<(), ModelError> {
        if self.persistent_storage {
            return Ok(());
        }
        // Clean up isolated storage.
        let decl = {
            let state = self.lock_state().await;
            match *state {
                InstanceState::Resolved(ref s) => s.decl.clone(),
                _ => {
                    // The instance was never resolved and therefore never ran, it can't possibly
                    // have storage to clean up.
                    return Ok(());
                }
            }
        };
        for use_ in decl.uses.iter() {
            if let UseDecl::Storage(use_storage) = use_ {
                match routing::route_and_delete_storage(use_storage.clone(), &self).await {
                    Ok(()) => (),
                    Err(ModelError::RoutingError { .. }) => {
                        // If the routing for this storage capability is invalid then there's no
                        // storage for us to delete. Ignore this error, and proceed.
                    }
                    Err(error) => {
                        // We received an error we weren't expecting, but we still want to destroy
                        // this instance. It's bad to leave storage state undeleted, but it would
                        // be worse to not continue with destroying this instance. Log the error,
                        // and proceed.
                        warn!(
                            component=%self.abs_moniker, %error,
                            "failed to delete storage during instance destruction, proceeding with destruction anyway",
                        );
                    }
                }
            }
        }
        Ok(())
    }

    /// Registers actions to destroy all dynamic children of collections belonging to this instance.
    async fn destroy_dynamic_children(self: &Arc<Self>) -> Result<(), ModelError> {
        let moniker_incarnations: Vec<_> = {
            let state = self.lock_state().await;
            let state = match *state {
                InstanceState::Resolved(ref s) => s,
                _ => {
                    // Component instance was not resolved, so no dynamic children.
                    return Ok(());
                }
            };
            state.children().map(|(k, c)| (k.clone(), c.incarnation_id())).collect()
        };
        let mut futures = vec![];
        // Destroy all children that belong to a collection.
        for (m, id) in moniker_incarnations {
            if m.collection().is_some() {
                let nf = ActionSet::register(self.clone(), DestroyChildAction::new(m, id));
                futures.push(nf);
            }
        }
        join_all(futures).await.into_iter().fold(Ok(()), |acc, r| acc.and_then(|_| r))
    }

    pub async fn open_outgoing(
        &self,
        flags: fio::OpenFlags,
        open_mode: u32,
        path: PathBuf,
        server_chan: &mut zx::Channel,
    ) -> Result<(), ModelError> {
        let execution = self.lock_execution().await;
        if execution.runtime.is_none() {
            return Err(RoutingError::source_instance_stopped(&self.abs_moniker).into());
        }
        let runtime = execution.runtime.as_ref().expect("bind_instance_open_outgoing: no runtime");
        let out_dir = &runtime.outgoing_dir.as_ref().ok_or_else(|| {
            ModelError::from(RoutingError::source_instance_not_executable(&self.abs_moniker))
        })?;
        let path = path.to_str().ok_or_else(|| ModelError::path_is_not_utf8(path.clone()))?;
        let path = fuchsia_fs::canonicalize_path(path);
        let server_chan = channel::take_channel(server_chan);
        let server_end = ServerEnd::new(server_chan);
        out_dir.open(flags, open_mode, path, server_end).map_err(|e| {
            ModelError::from(OpenResourceError::open_outgoing_failed(
                &self.instanced_moniker,
                path,
                e,
            ))
        })?;
        Ok(())
    }

    /// Connects `server_chan` to this instance's exposed directory if it has
    /// been resolved. Component must be resolved or destroyed before using
    /// this function, otherwise it will panic.
    pub async fn open_exposed(&self, server_chan: &mut zx::Channel) -> Result<(), ModelError> {
        let state = self.lock_state().await;
        match &*state {
            InstanceState::Resolved(resolved_instance_state) => {
                let exposed_dir = &resolved_instance_state.exposed_dir;
                // TODO(fxbug.dev/81010): open_exposed does not have a rights input parameter, so
                // this makes use of the POSIX_[WRITABLE|EXECUTABLE] flags to open a connection
                // with those rights if available from the parent directory connection but without
                // failing if not available.
                let flags = fio::OpenFlags::RIGHT_READABLE
                    | fio::OpenFlags::POSIX_WRITABLE
                    | fio::OpenFlags::POSIX_EXECUTABLE;
                let server_chan = channel::take_channel(server_chan);
                let server_end = ServerEnd::new(server_chan);
                exposed_dir.open(flags, fio::MODE_TYPE_DIRECTORY, Path::dot(), server_end);
                Ok(())
            }
            InstanceState::Destroyed => {
                Err(ModelError::instance_not_found(self.abs_moniker.clone()))
            }
            _ => {
                panic!("Component must be resolved or destroyed before using this function")
            }
        }
    }

    /// Binds to the component instance in this instance, starting it if it's not already running.
    pub async fn start(
        self: &Arc<Self>,
        reason: &StartReason,
    ) -> Result<fsys::StartResult, ModelError> {
        // Skip starting a component instance that was already started. It's important to bail out
        // here so we don't waste time starting eager children more than once.
        {
            let state = self.lock_state().await;
            let execution = self.lock_execution().await;
            if let Some(res) = start::should_return_early(&state, &execution, &self.abs_moniker) {
                return res;
            }
        }
        ActionSet::register(self.clone(), StartAction::new(reason.clone())).await?;

        let eager_children: Vec<_> = {
            let state = self.lock_state().await;
            match *state {
                InstanceState::Resolved(ref s) => s
                    .children()
                    .filter_map(|(_, r)| match r.startup {
                        fdecl::StartupMode::Eager => Some(r.clone()),
                        fdecl::StartupMode::Lazy => None,
                    })
                    .collect(),
                InstanceState::Destroyed => {
                    return Err(ModelError::instance_not_found(self.abs_moniker.clone()));
                }
                InstanceState::New | InstanceState::Unresolved => {
                    panic!("start: not resolved")
                }
            }
        };
        Self::start_eager_children_recursive(eager_children).await.or_else(|e| match e {
            ModelError::InstanceShutDown { .. } => Ok(()),
            _ => Err(e),
        })?;
        Ok(fsys::StartResult::Started)
    }

    /// Starts a list of instances, and any eager children they may return.
    // This function recursively calls `start`, so it returns a BoxFuture,
    fn start_eager_children_recursive<'a>(
        instances_to_bind: Vec<Arc<ComponentInstance>>,
    ) -> BoxFuture<'a, Result<(), ModelError>> {
        let f = async move {
            let futures: Vec<_> = instances_to_bind
                .iter()
                .map(|component| async move { component.start(&StartReason::Eager).await })
                .collect();
            join_all(futures)
                .await
                .into_iter()
                .fold(Ok(fsys::StartResult::Started), |acc, r| acc.and_then(|_| r))?;
            Ok(())
        };
        Box::pin(f)
    }

    pub fn incarnation_id(&self) -> IncarnationId {
        match self.instanced_moniker().leaf() {
            Some(m) => m.instance(),
            // Assign 0 to the root component instance
            None => 0,
        }
    }

    pub fn instance_id(self: &Arc<Self>) -> Option<ComponentInstanceId> {
        self.try_get_context()
            .map(|ctx| ctx.component_id_index().look_up_moniker(&self.abs_moniker).cloned())
            .unwrap_or(None)
    }

    /// Run the provided closure with this component's logger (if any) as the default. If the
    /// component does not have a logger, fall back to the global default.
    pub async fn with_logger_as_default<T>(&self, op: impl FnOnce() -> T) -> T {
        let execution = self.lock_execution().await;
        if let Some(Runtime { namespace: Some(ns), .. }) = &execution.runtime {
            if let Some(logger) = ns.get_attributed_logger() {
                tracing::subscriber::with_default(logger, op)
            } else {
                op()
            }
        } else {
            op()
        }
    }

    /// Scoped this server_end to the component instance's Runtime. For the duration
    /// of the component's lifetime, when it's running, this channel will be
    /// kept alive.
    pub async fn scope_to_runtime(self: &Arc<Self>, server_end: zx::Channel) {
        let mut execution = self.lock_execution().await;
        execution.scope_server_end(server_end);
    }

    /// Returns the top instance (component manager's instance) by traversing parent links.
    async fn top_instance(self: &Arc<Self>) -> Result<Arc<ComponentManagerInstance>, ModelError> {
        let mut current = self.clone();
        loop {
            match current.try_get_parent()? {
                ExtendedInstance::Component(parent) => {
                    current = parent.clone();
                }
                ExtendedInstance::AboveRoot(parent) => {
                    return Ok(parent);
                }
            }
        }
    }

    /// Returns the effective persistent storage setting for a child.
    /// If the CollectionDecl exists and the `persistent_storage` field is set, return the setting.
    /// Otherwise, if the CollectionDecl or its `persistent_storage` field is not set, return
    /// `self.persistent_storage` as a default value for the child to inherit.
    fn persistent_storage_for_child(&self, collection: Option<&CollectionDecl>) -> bool {
        let default_persistent_storage = self.persistent_storage;
        if let Some(collection) = collection {
            collection.persistent_storage.unwrap_or(default_persistent_storage)
        } else {
            default_persistent_storage
        }
    }
}

/// Extracts a mutable reference to the `target` field of an `OfferDecl`, or
/// `None` if the offer type is unknown.
fn offer_target_mut(offer: &mut fdecl::Offer) -> Option<&mut Option<fdecl::Ref>> {
    match offer {
        fdecl::Offer::Service(fdecl::OfferService { target, .. })
        | fdecl::Offer::Protocol(fdecl::OfferProtocol { target, .. })
        | fdecl::Offer::Directory(fdecl::OfferDirectory { target, .. })
        | fdecl::Offer::Storage(fdecl::OfferStorage { target, .. })
        | fdecl::Offer::Runner(fdecl::OfferRunner { target, .. })
        | fdecl::Offer::Resolver(fdecl::OfferResolver { target, .. })
        | fdecl::Offer::Event(fdecl::OfferEvent { target, .. }) => Some(target),
        fdecl::OfferUnknown!() => None,
    }
}

// A unit struct that implements `DebugRouteMapper` without recording any capability routes.
#[derive(Debug, Clone)]
pub struct NoopRouteMapper;

impl DebugRouteMapper for NoopRouteMapper {
    type RouteMap = ();

    fn get_route(self) -> () {}
}

#[async_trait]
impl ComponentInstanceInterface for ComponentInstance {
    type TopInstance = ComponentManagerInstance;
    type DebugRouteMapper = NoopRouteMapper;

    fn instanced_moniker(&self) -> &InstancedAbsoluteMoniker {
        &self.instanced_moniker
    }

    fn abs_moniker(&self) -> &AbsoluteMoniker {
        &self.abs_moniker
    }

    fn child_moniker(&self) -> Option<&ChildMoniker> {
        self.abs_moniker.leaf()
    }

    fn url(&self) -> &str {
        &self.component_url
    }

    fn environment(&self) -> &dyn EnvironmentInterface<Self> {
        self.environment.as_ref()
    }

    fn try_get_policy_checker(&self) -> Result<GlobalPolicyChecker, ComponentInstanceError> {
        let context = self.try_get_context().map_err(|_| {
            ComponentInstanceError::PolicyCheckerNotFound { moniker: self.abs_moniker.clone() }
        })?;
        Ok(context.policy().clone())
    }

    fn try_get_component_id_index(&self) -> Result<Arc<ComponentIdIndex>, ComponentInstanceError> {
        let context = self.try_get_context().map_err(|_| {
            ComponentInstanceError::ComponentIdIndexNotFound { moniker: self.abs_moniker.clone() }
        })?;
        Ok(context.component_id_index())
    }

    fn try_get_parent(&self) -> Result<ExtendedInstance, ComponentInstanceError> {
        self.parent.upgrade()
    }

    async fn lock_resolved_state<'a>(
        self: &'a Arc<Self>,
    ) -> Result<Box<dyn ResolvedInstanceInterface<Component = Self> + 'a>, ComponentInstanceError>
    {
        Ok(Box::new(ComponentInstance::lock_resolved_state(self).await?))
    }

    fn new_route_mapper() -> NoopRouteMapper {
        NoopRouteMapper
    }
}

impl std::fmt::Debug for ComponentInstance {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("ComponentInstance")
            .field("component_url", &self.component_url)
            .field("startup", &self.startup)
            .field("abs_moniker", &self.instanced_moniker)
            .finish()
    }
}

/// The execution state of a component.
pub struct ExecutionState {
    /// True if the component instance has shut down. This means that the component is stopped
    /// and cannot be restarted.
    shut_down: bool,

    /// Runtime support for the component. From component manager's point of view, the component
    /// instance is running iff this field is set.
    pub runtime: Option<Runtime>,
}

impl ExecutionState {
    /// Creates a new ExecutionState.
    pub fn new() -> Self {
        Self { shut_down: false, runtime: None }
    }

    /// Returns whether the instance has shut down.
    pub fn is_shut_down(&self) -> bool {
        self.shut_down
    }

    /// Enables the component to restart after being shut down. Used by the UnresolveAction.
    /// Use of this function is strongly discouraged.
    pub fn reset_shut_down(&mut self) {
        self.shut_down = false;
    }

    /// Scope server_end to `runtime` of this state. This ensures that the channel
    /// will be kept alive as long as runtime is set to Some(...). If it is
    /// None when this method is called, this operation is a no-op and the channel
    /// will be dropped.
    pub fn scope_server_end(&mut self, server_end: zx::Channel) {
        if let Some(runtime) = self.runtime.as_mut() {
            runtime.add_scoped_server_end(server_end);
        }
    }
}

/// The mutable state of a component instance.
pub enum InstanceState {
    /// The instance was just created.
    New,
    /// A Discovered event has been dispatched for the instance, but it has not been resolved yet.
    Unresolved,
    /// The instance has been resolved.
    Resolved(ResolvedInstanceState),
    /// The instance has been destroyed. It has no content and no further actions may be registered
    /// on it.
    Destroyed,
}

impl InstanceState {
    /// Changes the state, checking invariants.
    /// The allowed transitions:
    /// • New -> Discovered <-> Resolved -> Destroyed
    /// • {New, Discovered, Resolved} -> Destroyed
    pub fn set(&mut self, next: Self) {
        match (&self, &next) {
            (Self::New, Self::New)
            | (Self::New, Self::Resolved(_))
            | (Self::Unresolved, Self::Unresolved)
            | (Self::Unresolved, Self::New)
            | (Self::Resolved(_), Self::Resolved(_))
            | (Self::Resolved(_), Self::New)
            | (Self::Destroyed, Self::Destroyed)
            | (Self::Destroyed, Self::New)
            | (Self::Destroyed, Self::Unresolved)
            | (Self::Destroyed, Self::Resolved(_)) => {
                panic!("Invalid instance state transition from {:?} to {:?}", self, next);
            }
            _ => {
                *self = next;
            }
        }
    }
}

impl fmt::Debug for InstanceState {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        let s = match self {
            Self::New => "New",
            Self::Unresolved => "Discovered",
            Self::Resolved(_) => "Resolved",
            Self::Destroyed => "Destroyed",
        };
        f.write_str(s)
    }
}

/// Expose instance state in the format in which the `shutdown` action expects
/// to see it.
///
/// Largely shares its implementation with `ResolvedInstanceInterface`.
impl shutdown::Component for ResolvedInstanceState {
    fn uses(&self) -> Vec<UseDecl> {
        <Self as ResolvedInstanceInterface>::uses(self)
    }

    fn exposes(&self) -> Vec<cm_rust::ExposeDecl> {
        <Self as ResolvedInstanceInterface>::exposes(self)
    }

    fn offers(&self) -> Vec<cm_rust::OfferDecl> {
        // Includes both static and dynamic offers.
        <Self as ResolvedInstanceInterface>::offers(self)
    }

    fn capabilities(&self) -> Vec<cm_rust::CapabilityDecl> {
        <Self as ResolvedInstanceInterface>::capabilities(self)
    }

    fn collections(&self) -> Vec<cm_rust::CollectionDecl> {
        <Self as ResolvedInstanceInterface>::collections(self)
    }

    fn environments(&self) -> Vec<cm_rust::EnvironmentDecl> {
        self.decl.environments.clone()
    }

    fn children(&self) -> Vec<shutdown::Child> {
        // Includes both static and dynamic children.
        ResolvedInstanceState::children(self)
            .map(|(moniker, instance)| shutdown::Child {
                moniker: moniker.clone(),
                environment_name: instance.environment().name().map(|n| n.to_string()),
            })
            .collect()
    }
}

/// The mutable state of a resolved component instance.
pub struct ResolvedInstanceState {
    /// The ExecutionScope for this component. Pseudo directories should be hosted with this
    /// scope to tie their life-time to that of the component.
    execution_scope: ExecutionScope,
    /// The component's declaration.
    decl: ComponentDecl,
    /// All child instances, indexed by child moniker.
    children: HashMap<ChildMoniker, Arc<ComponentInstance>>,
    /// The next unique identifier for a dynamic children created in this realm.
    /// (Static instances receive identifier 0.)
    next_dynamic_instance_id: IncarnationId,
    /// The set of named Environments defined by this instance.
    environments: HashMap<String, Arc<Environment>>,
    /// Hosts a directory mapping the component's exposed capabilities.
    exposed_dir: ExposedDir,
    /// Hosts a directory mapping the component's namespace.
    ns_dir: NamespaceDir,
    /// Contains information about the package, if one exists
    package: Option<Package>,
    /// Contains the resolved configuration fields for this component, if they exist
    config: Option<ConfigFields>,
    /// Dynamic offers targeting this component's dynamic children.
    ///
    /// Invariant: the `target` field of all offers must refer to a live dynamic
    /// child (i.e., a member of `live_children`), and if the `source` field
    /// refers to a dynamic child, it must also be live.
    dynamic_offers: Vec<cm_rust::OfferDecl>,
    /// The as-resolved location of the component: either an absolute component
    /// URL, or (with a package context) a relative path URL.
    address: ComponentAddress,
    /// The context to be used to resolve a component from a path
    /// relative to this component (for example, a component in a subpackage).
    /// If `None`, the resolver cannot resolve relative path component URLs.
    context_to_resolve_children: Option<ComponentResolutionContext>,
}

impl ResolvedInstanceState {
    pub async fn new(
        component: &Arc<ComponentInstance>,
        decl: ComponentDecl,
        package: Option<Package>,
        config: Option<ConfigFields>,
        address: ComponentAddress,
        context_to_resolve_children: Option<ComponentResolutionContext>,
    ) -> Result<Self, ModelError> {
        let exposed_dir = ExposedDir::new(
            ExecutionScope::new(),
            WeakComponentInstance::new(&component),
            decl.clone(),
        )?;
        let ns_dir = NamespaceDir::new(
            ExecutionScope::new(),
            WeakComponentInstance::new(&component),
            decl.clone(),
            package.clone().map(|p| p.package_dir),
        )?;
        let mut state = Self {
            execution_scope: ExecutionScope::new(),
            decl: decl.clone(),
            children: HashMap::new(),
            next_dynamic_instance_id: 1,
            environments: Self::instantiate_environments(component, &decl),
            exposed_dir,
            ns_dir,
            package,
            config,
            dynamic_offers: vec![],
            address,
            context_to_resolve_children,
        };
        state.add_static_children(component, &decl).await?;
        Ok(state)
    }

    /// Returns a reference to the component's validated declaration.
    pub fn decl(&self) -> &ComponentDecl {
        &self.decl
    }

    #[cfg(test)]
    pub fn decl_as_mut(&mut self) -> &mut ComponentDecl {
        &mut self.decl
    }

    /// This component's `ExecutionScope`.
    /// Pseudo-directories can be opened with this scope to tie their lifetime
    /// to this component.
    pub fn execution_scope(&self) -> &ExecutionScope {
        &self.execution_scope
    }

    /// Returns an iterator over all children.
    pub fn children(&self) -> impl Iterator<Item = (&ChildMoniker, &Arc<ComponentInstance>)> {
        self.children.iter().map(|(k, v)| (k, v))
    }

    /// Returns a reference to a child.
    pub fn get_child(&self, m: &ChildMoniker) -> Option<&Arc<ComponentInstance>> {
        self.children.get(m)
    }

    /// Returns a vector of the children in `collection`.
    pub fn children_in_collection(
        &self,
        collection: &str,
    ) -> Vec<(ChildMoniker, Arc<ComponentInstance>)> {
        self.children()
            .filter(move |(m, _)| match m.collection() {
                Some(name) if name == collection => true,
                _ => false,
            })
            .map(|(m, c)| (m.clone(), Arc::clone(c)))
            .collect()
    }

    /// Return all children that match the `ChildMoniker` regardless of
    /// whether that child is live.
    pub fn get_all_children_by_name(&self, m: &ChildMoniker) -> Vec<Arc<ComponentInstance>> {
        self.children
            .iter()
            .filter(|(child, _)| m.name() == child.name() && m.collection() == child.collection())
            .map(|(_, c)| c.clone())
            .collect()
    }

    /// Returns the exposed directory bound to this instance.
    pub fn get_exposed_dir(&self) -> &ExposedDir {
        &self.exposed_dir
    }

    /// Returns the namespace directory of this instance.
    pub fn get_ns_dir(&self) -> &NamespaceDir {
        &self.ns_dir
    }

    /// Returns the resolved structured configuration of this instance, if any.
    pub fn config(&self) -> Option<&ConfigFields> {
        self.config.as_ref()
    }

    /// Returns information about the package of the instance, if any.
    pub fn package(&self) -> Option<&Package> {
        self.package.as_ref()
    }

    /// Extends an instanced absolute moniker with the live child with moniker `p`. Returns `None`
    /// if no matching child was found.
    pub fn extend_moniker_with(
        &self,
        moniker: &InstancedAbsoluteMoniker,
        child_moniker: &ChildMoniker,
    ) -> Option<InstancedAbsoluteMoniker> {
        match self.get_child(child_moniker).map(|c| c.incarnation_id()) {
            Some(instance_id) => Some(
                moniker
                    .child(InstancedChildMoniker::from_child_moniker(child_moniker, instance_id)),
            ),
            None => None,
        }
    }

    /// Removes a child.
    pub fn remove_child(&mut self, moniker: &ChildMoniker) {
        if self.children.remove(moniker).is_none() {
            return;
        }

        // Delete any dynamic offers whose `source` or `target` matches the
        // component we're deleting.
        self.dynamic_offers.retain(|offer| {
            use cm_rust::OfferDeclCommon;
            let source_matches = offer.source()
                == &cm_rust::OfferSource::Child(cm_rust::ChildRef {
                    name: moniker.name().to_string(),
                    collection: moniker.collection().map(|c| c.to_string()),
                });
            let target_matches = offer.target()
                == &cm_rust::OfferTarget::Child(cm_rust::ChildRef {
                    name: moniker.name().to_string(),
                    collection: moniker.collection().map(|c| c.to_string()),
                });
            !source_matches && !target_matches
        });
    }

    /// Creates a set of Environments instantiated from their EnvironmentDecls.
    fn instantiate_environments(
        component: &Arc<ComponentInstance>,
        decl: &ComponentDecl,
    ) -> HashMap<String, Arc<Environment>> {
        let mut environments = HashMap::new();
        for env_decl in &decl.environments {
            environments.insert(
                env_decl.name.clone(),
                Arc::new(Environment::from_decl(component, env_decl)),
            );
        }
        environments
    }

    /// Retrieve an environment for `child`, inheriting from `component`'s environment if
    /// necessary.
    fn environment_for_child(
        &mut self,
        component: &Arc<ComponentInstance>,
        child: &ChildDecl,
        collection: Option<&CollectionDecl>,
    ) -> Arc<Environment> {
        // For instances in a collection, the environment (if any) is designated in the collection.
        // Otherwise, it's specified in the ChildDecl.
        let environment_name = match collection {
            Some(c) => c.environment.as_ref(),
            None => child.environment.as_ref(),
        };
        if let Some(environment_name) = environment_name {
            Arc::clone(
                self.environments
                    .get(environment_name)
                    .expect(&format!("Environment not found: {}", environment_name)),
            )
        } else {
            // Auto-inherit the environment from this component instance.
            Arc::new(Environment::new_inheriting(component))
        }
    }

    /// Adds a new child of this instance for the given `ChildDecl`. Returns a
    /// future to wait on the child's `Discover` action, or `None` if a child
    /// with the same name already existed. This function always succeeds - an
    /// error returned by the `Future` means that the `Discover` action failed,
    /// but the creation of the child still succeeded.
    async fn add_child(
        &mut self,
        component: &Arc<ComponentInstance>,
        child: &ChildDecl,
        collection: Option<&CollectionDecl>,
        numbered_handles: Option<Vec<fidl_fuchsia_process::HandleInfo>>,
        dynamic_offers: Option<Vec<cm_rust::OfferDecl>>,
    ) -> Result<BoxFuture<'static, Result<(), ModelError>>, ModelError> {
        let child = self.add_child_internal(
            component,
            child,
            collection,
            numbered_handles,
            dynamic_offers,
        )?;
        // We can dispatch a Discovered event for the component now that it's installed in the
        // tree, which means any Discovered hooks will capture it.
        let mut actions = child.lock_actions().await;
        Ok(actions.register_no_wait(&child, DiscoverAction::new()).boxed())
    }

    /// Adds a new child of this instance for the given `ChildDecl`. Returns
    /// a result indicating if the new child instance has been successfully added.
    /// Like `add_child`, but doesn't register a `Discover` action, and therefore
    /// doesn't return a future to wait for.
    #[cfg(test)]
    pub fn add_child_no_discover(
        &mut self,
        component: &Arc<ComponentInstance>,
        child: &ChildDecl,
        collection: Option<&CollectionDecl>,
    ) -> Result<(), ModelError> {
        self.add_child_internal(component, child, collection, None, None).map(|_| ())
    }

    fn add_child_internal(
        &mut self,
        component: &Arc<ComponentInstance>,
        child: &ChildDecl,
        collection: Option<&CollectionDecl>,
        numbered_handles: Option<Vec<fidl_fuchsia_process::HandleInfo>>,
        dynamic_offers: Option<Vec<cm_rust::OfferDecl>>,
    ) -> Result<Arc<ComponentInstance>, ModelError> {
        let child_moniker = ChildMoniker::try_new(&child.name, collection.map(|c| &c.name))?;
        if self.get_child(&child_moniker).is_some() {
            return Err(ModelError::instance_already_exists(
                component.abs_moniker().clone(),
                child_moniker,
            ));
        }
        // TODO(fxb/108376): next_dynamic_instance_id should be per-collection.
        let instance_id = match collection {
            Some(_) => {
                let id = self.next_dynamic_instance_id;
                self.next_dynamic_instance_id += 1;
                id
            }
            None => 0,
        };
        let instanced_moniker =
            InstancedChildMoniker::from_child_moniker(&child_moniker, instance_id);
        let child = ComponentInstance::new(
            self.environment_for_child(component, child, collection.clone()),
            component.instanced_moniker.child(instanced_moniker),
            child.url.clone(),
            child.startup,
            child.on_terminate.unwrap_or(fdecl::OnTerminate::None),
            component.context.clone(),
            WeakExtendedInstance::Component(WeakComponentInstance::from(component)),
            component.hooks.clone(),
            numbered_handles,
            component.persistent_storage_for_child(collection),
        );
        self.children.insert(child_moniker, child.clone());
        if let Some(dynamic_offers) = dynamic_offers {
            self.dynamic_offers.extend(dynamic_offers.into_iter());
        }
        Ok(child)
    }

    async fn add_static_children(
        &mut self,
        component: &Arc<ComponentInstance>,
        decl: &ComponentDecl,
    ) -> Result<(), ModelError> {
        for child in decl.children.iter() {
            self.add_child(component, child, None, None, None).await?;
        }
        Ok(())
    }
}

impl ResolvedInstanceInterface for ResolvedInstanceState {
    type Component = ComponentInstance;

    fn uses(&self) -> Vec<UseDecl> {
        self.decl.uses.clone()
    }

    fn exposes(&self) -> Vec<cm_rust::ExposeDecl> {
        self.decl.exposes.clone()
    }

    fn offers(&self) -> Vec<cm_rust::OfferDecl> {
        self.decl.offers.iter().chain(self.dynamic_offers.iter()).cloned().collect()
    }

    fn capabilities(&self) -> Vec<cm_rust::CapabilityDecl> {
        self.decl.capabilities.clone()
    }

    fn collections(&self) -> Vec<cm_rust::CollectionDecl> {
        self.decl.collections.clone()
    }

    fn get_child(&self, moniker: &ChildMoniker) -> Option<Arc<ComponentInstance>> {
        ResolvedInstanceState::get_child(self, moniker).map(Arc::clone)
    }

    fn children_in_collection(
        &self,
        collection: &str,
    ) -> Vec<(ChildMoniker, Arc<ComponentInstance>)> {
        ResolvedInstanceState::children_in_collection(self, collection)
    }

    fn address(&self) -> ComponentAddress {
        self.address.clone()
    }

    fn context_to_resolve_children(&self) -> Option<ComponentResolutionContext> {
        self.context_to_resolve_children.clone()
    }
}

/// The execution state for a component instance that has started running.
pub struct Runtime {
    /// Holder for objects related to the component's incoming namespace.
    pub namespace: Option<IncomingNamespace>,

    /// A client handle to the component instance's outgoing directory.
    pub outgoing_dir: Option<fio::DirectoryProxy>,

    /// A client handle to the component instance's runtime directory hosted by the runner.
    pub runtime_dir: Option<fio::DirectoryProxy>,

    /// Used to interact with the Runner to influence the component's execution.
    pub controller: Option<ComponentController>,

    /// Approximates when the component was started.
    pub timestamp: zx::Time,

    /// Describes why the component instance was started
    pub start_reason: StartReason,

    /// Allows the spawned background context, which is watching for the
    /// controller channel to close, to be aborted when the `Runtime` is
    /// dropped.
    exit_listener: Option<AbortHandle>,

    /// Channels scoped to lifetime of this component's execution context. This
    /// should only be used for the server_end of the `fuchsia.component.Binder`
    /// connection.
    binder_server_ends: Vec<zx::Channel>,
}

#[derive(Debug, PartialEq)]
/// Represents the result of a request to stop a component, which has two
/// pieces. There is what happened to sending the request over the FIDL
/// channel to the controller and then what the exit status of the component
/// is. For example, the component might have exited with error before the
/// request was sent, in which case we encountered no error processing the stop
/// request and the component is considered to have terminated abnormally.
pub struct ComponentStopOutcome {
    /// The result of the request to stop the component.
    pub request: StopRequestSuccess,
    /// The final status of the component.
    pub component_exit_status: zx::Status,
}

#[derive(Debug, PartialEq)]
/// Outcomes of the stop request that are considered success. A request success
/// indicates that the request was sent without error over the
/// ComponentController channel or that sending the request was not necessary
/// because the component stopped previously.
pub enum StopRequestSuccess {
    /// Component stopped before its stopped was requested.
    AlreadyStopped,
    /// The component did not stop in time, but was killed before the kill
    /// timeout.
    Killed,
    /// The component did not stop in time and was killed after the kill
    /// timeout was reached.
    KilledAfterTimeout,
    /// The component had no Controller, no request was sent, and therefore no
    /// error occurred in the send process.
    NoController,
    /// The component stopped within the timeout.
    Stopped,
    /// The component stopped after the timeout, but before the kill message
    /// could be sent.
    StoppedWithTimeoutRace,
}
#[derive(Debug, PartialEq)]
pub enum StopComponentError {
    SendStopFailed,
    SendKillFailed,
}

impl fmt::Display for StopComponentError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::SendKillFailed => write!(f, "failed to send `kill` message"),
            Self::SendStopFailed => write!(f, "failed to send `stop` message"),
        }
    }
}

impl std::error::Error for StopComponentError {
    fn description(&self) -> &str {
        match self {
            Self::SendKillFailed => "Error killing component",
            Self::SendStopFailed => "Error stopping component",
        }
    }
    fn cause(&self) -> Option<&dyn std::error::Error> {
        Some(self)
    }
}

impl Runtime {
    pub fn start_from(
        namespace: Option<IncomingNamespace>,
        outgoing_dir: Option<fio::DirectoryProxy>,
        runtime_dir: Option<fio::DirectoryProxy>,
        controller: Option<fcrunner::ComponentControllerProxy>,
        start_reason: StartReason,
    ) -> Result<Self, ModelError> {
        let timestamp = zx::Time::get_monotonic();
        Ok(Runtime {
            namespace,
            outgoing_dir,
            runtime_dir,
            controller: controller.map(ComponentController::new),
            timestamp,
            exit_listener: None,
            binder_server_ends: vec![],
            start_reason,
        })
    }

    /// If the Runtime has a controller this creates a background context which
    /// watches for the controller's channel to close. If the channel closes,
    /// the background context attempts to use the WeakComponentInstance to stop the
    /// component.
    pub fn watch_for_exit(&mut self, component: WeakComponentInstance) {
        if let Some(controller) = &self.controller {
            let epitaph_fut = controller.wait_for_epitaph();
            let (abort_client, abort_server) = AbortHandle::new_pair();
            let watcher = Abortable::new(
                async move {
                    epitaph_fut.await;
                    if let Ok(component) = component.upgrade() {
                        ActionSet::register(component, StopAction::new(false, false))
                            .await
                            .unwrap_or_else(|error| error!(%error, "failed to register action"));
                    }
                },
                abort_server,
            );
            fasync::Task::spawn(watcher.unwrap_or_else(|_| ())).detach();
            self.exit_listener = Some(abort_client);
        }
    }

    pub async fn wait_on_channel_close(&mut self) {
        if let Some(controller) = &self.controller {
            controller.wait_for_epitaph().await;
            self.controller = None;
        }
    }

    /// Stop the component. The timer defines how long the component is given
    /// to stop itself before we request the controller terminate the
    /// component.
    pub async fn stop_component<'a, 'b>(
        &'a mut self,
        stop_timer: BoxFuture<'a, ()>,
        kill_timer: BoxFuture<'b, ()>,
    ) -> Result<ComponentStopOutcome, StopComponentError> {
        // Potentially there is no controller, perhaps because the component
        // has no running code. In this case this is a no-op.
        if let Some(ref controller) = self.controller {
            stop_component_internal(controller, stop_timer, kill_timer).await
        } else {
            // TODO(jmatt) Need test coverage
            Ok(ComponentStopOutcome {
                request: StopRequestSuccess::NoController,
                component_exit_status: zx::Status::OK,
            })
        }
    }

    /// Add a channel scoped to the lifetime of this object.
    pub fn add_scoped_server_end(&mut self, server_end: zx::Channel) {
        self.binder_server_ends.push(server_end);
    }
}

impl Drop for Runtime {
    fn drop(&mut self) {
        if let Some(watcher) = &self.exit_listener {
            watcher.abort();
        }
    }
}

async fn stop_component_internal<'a, 'b>(
    controller: &ComponentController,
    stop_timer: BoxFuture<'a, ()>,
    kill_timer: BoxFuture<'b, ()>,
) -> Result<ComponentStopOutcome, StopComponentError> {
    let result = match do_runner_stop(controller, stop_timer).await {
        Some(r) => r,
        None => {
            // We must have hit the stop timeout because calling stop didn't return
            // a result, move to killing the component.
            do_runner_kill(controller, kill_timer).await
        }
    };

    match result {
        Ok(request_outcome) => Ok(ComponentStopOutcome {
            request: request_outcome,
            component_exit_status: controller.wait_for_epitaph().await,
        }),
        Err(e) => Err(e),
    }
}

async fn do_runner_stop<'a>(
    controller: &ComponentController,
    stop_timer: BoxFuture<'a, ()>,
) -> Option<Result<StopRequestSuccess, StopComponentError>> {
    // Ask the controller to stop the component
    match controller.stop() {
        Ok(()) => {}
        Err(e) => {
            if fidl::Error::is_closed(&e) {
                // Channel was closed already, component is considered stopped
                return Some(Ok(StopRequestSuccess::AlreadyStopped));
            } else {
                // There was some problem sending the message, perhaps a
                // protocol error, but there isn't really a way to recover.
                return Some(Err(StopComponentError::SendStopFailed));
            }
        }
    }

    let channel_close = Box::pin(async move {
        controller.on_closed().await.expect("failed waiting for channel to close");
    });
    // Wait for either the timer to fire or the channel to close
    match futures::future::select(stop_timer, channel_close).await {
        Either::Left(((), _channel_close)) => None,
        Either::Right((_timer, _close_result)) => Some(Ok(StopRequestSuccess::Stopped)),
    }
}

fn try_clone_dir_endpoint(
    dir: Option<&fio::DirectoryProxy>,
) -> Option<ClientEnd<fio::DirectoryMarker>> {
    if let Some(dir) = dir {
        if let Ok(cloned_dir) = fuchsia_fs::clone_directory(&dir, fio::OpenFlags::CLONE_SAME_RIGHTS)
        {
            let cloned_dir_channel = cloned_dir.into_channel().unwrap().into_zx_channel();
            Some(ClientEnd::new(cloned_dir_channel))
        } else {
            None
        }
    } else {
        None
    }
}

async fn do_runner_kill<'a>(
    controller: &ComponentController,
    kill_timer: BoxFuture<'a, ()>,
) -> Result<StopRequestSuccess, StopComponentError> {
    match controller.kill() {
        Ok(()) => {
            // Wait for the controller to close the channel
            let channel_close = Box::pin(async move {
                controller.on_closed().await.expect("error waiting for channel to close");
            });

            // If the control channel closes first, report the component to be
            // kill "normally", otherwise report it as killed after timeout.
            match futures::future::select(kill_timer, channel_close).await {
                Either::Left(((), _channel_close)) => Ok(StopRequestSuccess::KilledAfterTimeout),
                Either::Right((_timer, _close_result)) => Ok(StopRequestSuccess::Killed),
            }
        }
        Err(e) => {
            if fidl::Error::is_closed(&e) {
                // Even though we hit the timeout, the channel is closed,
                // so we assume stop succeeded and there was a race with
                // the timeout
                Ok(StopRequestSuccess::StoppedWithTimeoutRace)
            } else {
                // There was some problem sending the message, perhaps a
                // protocol error, but there isn't really a way to recover.
                Err(StopComponentError::SendKillFailed)
            }
        }
    }
}

#[cfg(test)]
pub mod tests {
    use {
        super::*,
        crate::model::{
            actions::{test_utils::is_discovered, ShutdownAction},
            events::{registry::EventSubscription, stream::EventStream},
            hooks::EventType,
            starter::Starter,
            testing::{
                mocks::{ControlMessage, ControllerActionResponse, MockController},
                routing_test_helpers::{RoutingTest, RoutingTestBuilder},
                test_helpers::{component_decl_with_test_runner, ActionsTest, ComponentInfo},
            },
        },
        assert_matches::assert_matches,
        cm_rust::{
            Availability, CapabilityDecl, CapabilityPath, ChildRef, DependencyType, EventMode,
            ExposeDecl, ExposeProtocolDecl, ExposeSource, ExposeTarget, OfferDecl,
            OfferDirectoryDecl, OfferProtocolDecl, OfferSource, OfferTarget, ProtocolDecl,
            UseProtocolDecl, UseSource,
        },
        cm_rust_testing::{
            ChildDeclBuilder, CollectionDeclBuilder, ComponentDeclBuilder, EnvironmentDeclBuilder,
        },
        component_id_index::gen_instance_id,
        fidl::endpoints,
        fuchsia_async as fasync,
        fuchsia_zircon::{self as zx, AsHandleRef, Koid},
        futures::lock::Mutex,
        moniker::AbsoluteMoniker,
        routing_test_helpers::component_id_index::make_index_file,
        std::panic,
        std::{boxed::Box, collections::HashMap, sync::Arc, task::Poll},
    };

    #[fuchsia::test]
    /// Test scenario where we tell the controller to stop the component and
    /// the component stops immediately.
    async fn stop_component_well_behaved_component_stop() {
        // Create a mock controller which simulates immediately shutting down
        // the component.
        let stop_timeout = zx::Duration::from_millis(50);
        let kill_timeout = zx::Duration::from_millis(10);
        let (client, server) =
            endpoints::create_endpoints::<fcrunner::ComponentControllerMarker>().unwrap();
        let server_channel_koid = server
            .as_handle_ref()
            .basic_info()
            .expect("failed to get basic info on server channel")
            .koid;

        // Create a request map which the MockController will fill with
        // requests it received related to mocked component.
        let requests: Arc<Mutex<HashMap<Koid, Vec<ControlMessage>>>> =
            Arc::new(Mutex::new(HashMap::new()));
        let controller = MockController::new(server, requests.clone(), server_channel_koid);
        controller.serve();

        let stop_timer = Box::pin(fasync::Timer::new(fasync::Time::after(stop_timeout)));
        let kill_timer = Box::pin(async move {
            let timer = fasync::Timer::new(fasync::Time::after(kill_timeout));
            timer.await;
        });
        let client_proxy = client.into_proxy().expect("failed to convert client to proxy");
        let component = ComponentController::new(client_proxy);
        match stop_component_internal(&component, stop_timer, kill_timer).await {
            Ok(ComponentStopOutcome {
                request: StopRequestSuccess::Stopped,
                component_exit_status: zx::Status::OK,
            }) => {}
            Ok(result) => {
                panic!("unexpected successful stop result {:?}", result);
            }
            Err(e) => {
                panic!("unexpected error stopping component {:?}", e);
            }
        }

        let msg_map = requests.lock().await;
        let msg_list =
            msg_map.get(&server_channel_koid).expect("No messages received on the channel");

        // The controller should have only seen a STOP message since it stops
        // the component immediately.
        assert_eq!(msg_list, &vec![ControlMessage::Stop]);
    }

    #[fuchsia::test]
    /// Test where the control channel is already closed when we try to stop
    /// the component.
    async fn stop_component_successful_component_already_gone() {
        let stop_timeout = zx::Duration::from_millis(100);
        let kill_timeout = zx::Duration::from_millis(1);
        let (client, server) =
            endpoints::create_endpoints::<fcrunner::ComponentControllerMarker>().unwrap();

        let stop_timer = Box::pin(fasync::Timer::new(fasync::Time::after(stop_timeout)));
        let kill_timer = Box::pin(async move {
            let timer = fasync::Timer::new(fasync::Time::after(kill_timeout));
            timer.await;
        });
        let client_proxy = client.into_proxy().expect("failed to convert client to proxy");
        let component = ComponentController::new(client_proxy);

        // Drop the server end so it closes
        drop(server);
        match stop_component_internal(&component, stop_timer, kill_timer).await {
            Ok(ComponentStopOutcome {
                request: StopRequestSuccess::AlreadyStopped,
                component_exit_status: zx::Status::PEER_CLOSED,
            }) => {}
            Ok(result) => {
                panic!("unexpected successful stop result {:?}", result);
            }
            Err(e) => {
                panic!("unexpected error stopping component {:?}", e);
            }
        }
    }

    #[fuchsia::test]
    /// The scenario where the controller stops the component after a delay
    /// which is before the controller reaches its timeout.
    fn stop_component_successful_stop_with_delay() {
        let mut exec = fasync::TestExecutor::new_with_fake_time().unwrap();

        // Create a mock controller which simulates shutting down the component
        // after a delay. The delay is much shorter than the period allotted
        // for the component to stop.
        let stop_timeout = zx::Duration::from_seconds(5);
        let kill_timeout = zx::Duration::from_millis(1);
        let (client, server) =
            endpoints::create_endpoints::<fcrunner::ComponentControllerMarker>().unwrap();
        let server_channel_koid = server
            .as_handle_ref()
            .basic_info()
            .expect("failed to get basic info on server channel")
            .koid;

        // Create a request map which the MockController will fill with
        // requests it received related to mocked component.
        let requests: Arc<Mutex<HashMap<Koid, Vec<ControlMessage>>>> =
            Arc::new(Mutex::new(HashMap::new()));
        let component_stop_delay = zx::Duration::from_millis(stop_timeout.into_millis() / 1_000);
        let controller = MockController::new_with_responses(
            server,
            requests.clone(),
            server_channel_koid,
            // stop the component after 60ms
            ControllerActionResponse { close_channel: true, delay: Some(component_stop_delay) },
            ControllerActionResponse { close_channel: true, delay: Some(component_stop_delay) },
        );
        controller.serve();

        // Create the stop call that we expect to stop the component.
        let stop_timer = Box::pin(fasync::Timer::new(fasync::Time::after(stop_timeout)));
        let kill_timer = Box::pin(async move {
            let timer = fasync::Timer::new(fasync::Time::after(kill_timeout));
            timer.await;
        });
        let client_proxy = client.into_proxy().expect("failed to convert client to proxy");
        let component = ComponentController::new(client_proxy);
        let mut stop_future = Box::pin(stop_component_internal(&component, stop_timer, kill_timer));

        // Poll the stop component future to where it has asked the controller
        // to stop the component. This should also cause the controller to
        // spawn the future with a delay to close the control channel.
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut stop_future));

        // Advance the clock beyond where the future to close the channel
        // should fire.
        let new_time =
            fasync::Time::from_nanos(exec.now().into_nanos() + component_stop_delay.into_nanos());
        exec.set_fake_time(new_time);
        exec.wake_expired_timers();

        // The controller channel should be closed so we can drive the stop
        // future to completion.
        match exec.run_until_stalled(&mut stop_future) {
            Poll::Ready(Ok(ComponentStopOutcome {
                request: StopRequestSuccess::Stopped,
                component_exit_status: zx::Status::OK,
            })) => {}
            Poll::Ready(Ok(result)) => {
                panic!("unexpected successful stop result {:?}", result);
            }
            Poll::Ready(Err(e)) => {
                panic!("unexpected error stopping component {:?}", e);
            }
            Poll::Pending => {
                panic!("future should have completed!");
            }
        }

        // Check that what we expect to be in the message map is there.
        let mut test_fut = Box::pin(async {
            let msg_map = requests.lock().await;
            let msg_list =
                msg_map.get(&server_channel_koid).expect("No messages received on the channel");

            // The controller should have only seen a STOP message since it stops
            // the component before the timeout is hit.
            assert_eq!(msg_list, &vec![ControlMessage::Stop]);
        });
        assert_eq!(Poll::Ready(()), exec.run_until_stalled(&mut test_fut));
    }

    #[fuchsia::test]
    /// Test scenario where the controller does not stop the component within
    /// the allowed period and the component stop state machine has to send
    /// the `kill` message to the controller. The runner then does not kill the
    /// component within the kill time out period.
    fn stop_component_successful_with_kill_timeout_result() {
        let mut exec = fasync::TestExecutor::new_with_fake_time().unwrap();

        // Create a controller which takes far longer than allowed to stop the
        // component.
        let stop_timeout = zx::Duration::from_seconds(5);
        let kill_timeout = zx::Duration::from_millis(200);
        let (client, server) =
            endpoints::create_endpoints::<fcrunner::ComponentControllerMarker>().unwrap();
        let server_channel_koid = server
            .as_handle_ref()
            .basic_info()
            .expect("failed to get basic info on server channel")
            .koid;

        // Create a request map which the MockController will fill with
        // requests it received related to mocked component.
        let requests: Arc<Mutex<HashMap<Koid, Vec<ControlMessage>>>> =
            Arc::new(Mutex::new(HashMap::new()));
        let stop_resp_delay = zx::Duration::from_millis(stop_timeout.into_millis() / 10);
        // since we want the mock controller to close the controller channel
        // before the kill timeout, set the response delay to less than the timeout
        let kill_resp_delay = zx::Duration::from_millis(kill_timeout.into_millis() * 2);
        let controller = MockController::new_with_responses(
            server,
            requests.clone(),
            server_channel_koid,
            // Process the stop message, but fail to close the channel. Channel
            // closure is the indication that a component stopped.
            ControllerActionResponse { close_channel: false, delay: Some(stop_resp_delay) },
            ControllerActionResponse { close_channel: true, delay: Some(kill_resp_delay) },
        );
        controller.serve();

        let stop_timer = Box::pin(async move {
            let timer = fasync::Timer::new(fasync::Time::after(stop_timeout));
            timer.await;
        });
        let kill_timer = Box::pin(async move {
            let timer = fasync::Timer::new(fasync::Time::after(kill_timeout));
            timer.await;
        });
        let client_proxy = client.into_proxy().expect("failed to convert client to proxy");
        let component = ComponentController::new(client_proxy);
        let epitaph_fut = component.wait_for_epitaph();
        let mut stop_fut = Box::pin(stop_component_internal(&component, stop_timer, kill_timer));

        // it should be the case we stall waiting for a response from the
        // controller
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut stop_fut));

        // Roll time passed the stop timeout.
        let mut new_time =
            fasync::Time::from_nanos(exec.now().into_nanos() + stop_timeout.into_nanos());
        exec.set_fake_time(new_time);
        exec.wake_expired_timers();
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut stop_fut));
        // Roll time beyond the kill timeout period
        new_time = fasync::Time::from_nanos(exec.now().into_nanos() + kill_timeout.into_nanos());
        exec.set_fake_time(new_time);
        exec.wake_expired_timers();

        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut stop_fut));

        // This future waits for the client channel to close. This creates a
        // rendezvous between the controller's execution context and the test.
        // Without this the message map state may be inconsistent.
        let mut check_msgs = Box::pin(async {
            epitaph_fut.await;

            let msg_map = requests.lock().await;
            let msg_list =
                msg_map.get(&server_channel_koid).expect("No messages received on the channel");

            assert_eq!(msg_list, &vec![ControlMessage::Stop, ControlMessage::Kill]);
        });
        // On this poll we advance the controller's state to where it has
        // started the timer to close the channel.
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut check_msgs));

        // Roll time beyond through the remainder of the response delay. The
        // delay period started when the controller received the kill request.
        new_time = fasync::Time::from_nanos(
            exec.now().into_nanos() + kill_resp_delay.into_nanos() - kill_timeout.into_nanos(),
        );
        exec.set_fake_time(new_time);
        exec.wake_expired_timers();

        // At this point stop_component() will have completed, but the
        // controller's future is not polled to completion, since it is not
        // required to complete the stop_component future.
        assert_eq!(
            Poll::Ready(Ok(ComponentStopOutcome {
                request: StopRequestSuccess::KilledAfterTimeout,
                component_exit_status: zx::Status::OK
            })),
            exec.run_until_stalled(&mut stop_fut)
        );

        // Now we expect the message check future to complete because the
        // controller should have closed the channel.
        assert_eq!(Poll::Ready(()), exec.run_until_stalled(&mut check_msgs));
    }

    #[fuchsia::test]
    /// Test scenario where the controller does not stop the component within
    /// the allowed period and the component stop state machine has to send
    /// the `kill` message to the controller. The controller then kills the
    /// component before the kill timeout is reached.
    fn stop_component_successful_with_kill_result() {
        let mut exec = fasync::TestExecutor::new_with_fake_time().unwrap();

        // Create a controller which takes far longer than allowed to stop the
        // component.
        let stop_timeout = zx::Duration::from_seconds(5);
        let kill_timeout = zx::Duration::from_millis(200);
        let (client, server) =
            endpoints::create_endpoints::<fcrunner::ComponentControllerMarker>().unwrap();
        let server_channel_koid = server
            .as_handle_ref()
            .basic_info()
            .expect("failed to get basic info on server channel")
            .koid;

        // Create a request map which the MockController will fill with
        // requests it received related to mocked component.
        let requests: Arc<Mutex<HashMap<Koid, Vec<ControlMessage>>>> =
            Arc::new(Mutex::new(HashMap::new()));
        let kill_resp_delay = zx::Duration::from_millis(kill_timeout.into_millis() / 2);
        let controller = MockController::new_with_responses(
            server,
            requests.clone(),
            server_channel_koid,
            // Process the stop message, but fail to close the channel. Channel
            // closure is the indication that a component stopped.
            ControllerActionResponse { close_channel: false, delay: None },
            ControllerActionResponse { close_channel: true, delay: Some(kill_resp_delay) },
        );
        controller.serve();

        let stop_timer = Box::pin(async move {
            let timer = fasync::Timer::new(fasync::Time::after(stop_timeout));
            timer.await;
        });
        let kill_timer = Box::pin(async move {
            let timer = fasync::Timer::new(fasync::Time::after(kill_timeout));
            timer.await;
        });
        let client_proxy = client.into_proxy().expect("failed to convert client to proxy");
        let component = ComponentController::new(client_proxy);
        let mut stop_fut = Box::pin(stop_component_internal(&component, stop_timer, kill_timer));

        // it should be the case we stall waiting for a response from the
        // controller
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut stop_fut));

        // Roll time passed the stop timeout.
        let mut new_time =
            fasync::Time::from_nanos(exec.now().into_nanos() + stop_timeout.into_nanos());
        exec.set_fake_time(new_time);
        exec.wake_expired_timers();
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut stop_fut));

        // Roll forward to where the mock controller should have closed the
        // controller channel.
        new_time = fasync::Time::from_nanos(exec.now().into_nanos() + kill_resp_delay.into_nanos());
        exec.set_fake_time(new_time);
        exec.wake_expired_timers();

        // At this point stop_component() will have completed, but the
        // controller's future was not polled to completion.
        assert_eq!(
            Poll::Ready(Ok(ComponentStopOutcome {
                request: StopRequestSuccess::Killed,
                component_exit_status: zx::Status::OK
            })),
            exec.run_until_stalled(&mut stop_fut)
        );
    }

    #[fuchsia::test]
    /// In this case we expect success, but that the stop state machine races
    /// with the controller. The state machine's timer expires, but when it
    /// goes to send the kill message, it finds the control channel is closed,
    /// indicating the component stopped.
    fn stop_component_successful_race_with_controller() {
        let mut exec = fasync::TestExecutor::new_with_fake_time().unwrap();

        // Create a controller which takes far longer than allowed to stop the
        // component.
        let stop_timeout = zx::Duration::from_seconds(5);
        let kill_timeout = zx::Duration::from_millis(1);
        let (client, server) =
            endpoints::create_endpoints::<fcrunner::ComponentControllerMarker>().unwrap();
        let server_channel_koid = server
            .as_handle_ref()
            .basic_info()
            .expect("failed to get basic info on server channel")
            .koid;

        // Create a request map which the MockController will fill with
        // requests it received related to mocked component.
        let requests: Arc<Mutex<HashMap<Koid, Vec<ControlMessage>>>> =
            Arc::new(Mutex::new(HashMap::new()));
        let close_delta = zx::Duration::from_millis(10);
        let resp_delay =
            zx::Duration::from_millis(stop_timeout.into_millis() + close_delta.into_millis());
        let controller = MockController::new_with_responses(
            server,
            requests.clone(),
            server_channel_koid,
            // Process the stop message, but fail to close the channel after
            // the timeout of stop_component()
            ControllerActionResponse { close_channel: true, delay: Some(resp_delay) },
            // This is irrelevant because the controller should never receive
            // the kill message
            ControllerActionResponse { close_channel: true, delay: Some(resp_delay) },
        );
        controller.serve();

        let stop_timer = Box::pin(fasync::Timer::new(fasync::Time::after(stop_timeout)));
        let kill_timer = Box::pin(async move {
            let timer = fasync::Timer::new(fasync::Time::after(kill_timeout));
            timer.await;
        });
        let client_proxy = client.into_proxy().expect("failed to convert client to proxy");
        let component = ComponentController::new(client_proxy);
        let epitaph_fut = component.wait_for_epitaph();
        let mut stop_fut = Box::pin(stop_component_internal(&component, stop_timer, kill_timer));

        // it should be the case we stall waiting for a response from the
        // controller
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut stop_fut));

        // Roll time passed the stop timeout and beyond when the controller
        // will close the channel
        let new_time = fasync::Time::from_nanos(exec.now().into_nanos() + resp_delay.into_nanos());
        exec.set_fake_time(new_time);
        exec.wake_expired_timers();

        // This future waits for the client channel to close. This creates a
        // rendezvous between the controller's execution context and the test.
        // Without this the message map state may be inconsistent.
        let mut check_msgs = Box::pin(async {
            epitaph_fut.await;

            let msg_map = requests.lock().await;
            let msg_list =
                msg_map.get(&server_channel_koid).expect("No messages received on the channel");

            assert_eq!(msg_list, &vec![ControlMessage::Stop]);
        });

        // Expect the message check future to complete because the controller
        // should close the channel.
        assert_eq!(Poll::Ready(()), exec.run_until_stalled(&mut check_msgs));

        // At this point stop_component() should now poll to completion because
        // the control channel is closed, but stop_component will perceive this
        // happening after its timeout expired.
        assert_eq!(
            Poll::Ready(Ok(ComponentStopOutcome {
                request: StopRequestSuccess::StoppedWithTimeoutRace,
                component_exit_status: zx::Status::OK
            })),
            exec.run_until_stalled(&mut stop_fut)
        );
    }

    #[fuchsia::test]
    async fn started_event_timestamp_matches_component() {
        let test =
            RoutingTest::new("root", vec![("root", ComponentDeclBuilder::new().build())]).await;

        let mut event_source = test
            .builtin_environment
            .event_source_factory
            .create_for_above_root()
            .await
            .expect("create event source");
        let mut event_stream = event_source
            .subscribe(
                vec![
                    EventType::Discovered.into(),
                    EventType::Resolved.into(),
                    EventType::Started.into(),
                    EventType::DebugStarted.into(),
                ]
                .into_iter()
                .map(|event| EventSubscription::new(event, EventMode::Async))
                .collect(),
            )
            .await
            .expect("subscribe to event stream");

        let model = test.model.clone();
        let (f, bind_handle) = async move {
            model.start_instance(&vec![].into(), &StartReason::Root).await.expect("failed to bind")
        }
        .remote_handle();
        fasync::Task::spawn(f).detach();
        let discovered_timestamp =
            wait_until_event_get_timestamp(&mut event_stream, EventType::Discovered).await;
        let resolved_timestamp =
            wait_until_event_get_timestamp(&mut event_stream, EventType::Resolved).await;
        let started_timestamp =
            wait_until_event_get_timestamp(&mut event_stream, EventType::Started).await;
        let debug_started_timestamp =
            wait_until_event_get_timestamp(&mut event_stream, EventType::DebugStarted).await;

        assert!(discovered_timestamp < resolved_timestamp);
        assert!(resolved_timestamp < started_timestamp);
        assert!(started_timestamp == debug_started_timestamp);

        let component = bind_handle.await;
        let component_timestamp =
            component.lock_execution().await.runtime.as_ref().unwrap().timestamp;
        assert_eq!(component_timestamp, started_timestamp);
    }

    #[fuchsia::test]
    /// Validate that if the ComponentController channel is closed that the
    /// the component is stopped.
    async fn test_early_component_exit() {
        let components = vec![
            ("root", ComponentDeclBuilder::new().add_eager_child("a").build()),
            ("a", ComponentDeclBuilder::new().add_eager_child("b").build()),
            ("b", component_decl_with_test_runner()),
        ];
        let test = ActionsTest::new("root", components, None).await;

        let mut event_source = test
            .builtin_environment
            .lock()
            .await
            .event_source_factory
            .create_for_above_root()
            .await
            .expect("failed creating event source");
        let mut stop_event_stream = event_source
            .subscribe(vec![EventSubscription::new(EventType::Stopped.into(), EventMode::Async)])
            .await
            .expect("couldn't susbscribe to event stream");

        let a_moniker: AbsoluteMoniker = vec!["a"].into();
        let b_moniker: AbsoluteMoniker = vec!["a", "b"].into();

        let component_b = test.look_up(b_moniker.clone()).await;

        // Start the root so it and its eager children start.
        let _root = test
            .model
            .start_instance(&vec![].into(), &StartReason::Root)
            .await
            .expect("failed to start root");
        test.runner
            .wait_for_urls(&["test:///root_resolved", "test:///a_resolved", "test:///b_resolved"])
            .await;

        // Check that the eagerly-started 'b' has a runtime, which indicates
        // it is running.
        assert!(component_b.lock_execution().await.runtime.is_some());

        let b_info = ComponentInfo::new(component_b.clone()).await;
        b_info.check_not_shut_down(&test.runner).await;

        // Tell the runner to close the controller channel
        test.runner.abort_controller(&b_info.channel_id);

        // Verify that we get a stop event as a result of the controller
        // channel close being observed.
        let stop_event = stop_event_stream
            .wait_until(EventType::Stopped, b_moniker.clone())
            .await
            .unwrap()
            .event;
        assert_eq!(stop_event.target_moniker, b_moniker.clone().into());

        // Verify that a parent of the exited component can still be stopped
        // properly.
        ActionSet::register(test.look_up(a_moniker.clone()).await, ShutdownAction::new())
            .await
            .expect("Couldn't trigger shutdown");
        // Check that we get a stop even which corresponds to the parent.
        let parent_stop = stop_event_stream
            .wait_until(EventType::Stopped, a_moniker.clone())
            .await
            .unwrap()
            .event;
        assert_eq!(parent_stop.target_moniker, a_moniker.clone().into());
    }

    #[fuchsia::test]
    async fn unresolve_test() {
        let components = vec![
            ("root", ComponentDeclBuilder::new().add_lazy_child("a").build()),
            ("a", ComponentDeclBuilder::new().add_eager_child("b").build()),
            ("b", ComponentDeclBuilder::new().add_eager_child("c").add_eager_child("d").build()),
            ("c", component_decl_with_test_runner()),
            ("d", component_decl_with_test_runner()),
        ];
        let test = ActionsTest::new("root", components, None).await;

        // Resolve each component.
        test.look_up(vec![].into()).await;
        let component_a = test.look_up(vec!["a"].into()).await;
        let component_b = test.look_up(vec!["a", "b"].into()).await;
        let component_c = test.look_up(vec!["a", "b", "c"].into()).await;
        let component_d = test.look_up(vec!["a", "b", "d"].into()).await;

        // Just unresolve component a and children
        assert_matches!(component_a.unresolve().await, Ok(()));
        assert!(is_discovered(&component_a).await);
        assert!(is_discovered(&component_b).await);
        assert!(is_discovered(&component_c).await);
        assert!(is_discovered(&component_d).await);

        // Unresolve again, which is ok because UnresolveAction is idempotent.
        assert_matches!(component_a.unresolve().await, Ok(()));
        assert!(is_discovered(&component_a).await);
    }

    #[fuchsia::test]
    async fn realm_instance_id() {
        let components = vec![
            ("root", ComponentDeclBuilder::new().add_eager_child("a").build()),
            ("a", ComponentDeclBuilder::new().add_eager_child("b").build()),
            ("b", component_decl_with_test_runner()),
        ];

        let instance_id = Some(gen_instance_id(&mut rand::thread_rng()));
        let component_id_index_path = make_index_file(component_id_index::Index {
            instances: vec![component_id_index::InstanceIdEntry {
                instance_id: instance_id.clone(),
                appmgr_moniker: None,
                moniker: Some(AbsoluteMoniker::root()),
            }],
            ..component_id_index::Index::default()
        })
        .unwrap();
        let test = RoutingTestBuilder::new("root", components)
            .set_component_id_index_path(
                component_id_index_path.path().to_str().unwrap().to_string(),
            )
            .build()
            .await;

        let root_realm =
            test.model.start_instance(&AbsoluteMoniker::root(), &StartReason::Root).await.unwrap();
        assert_eq!(instance_id, root_realm.instance_id());

        let a_realm = test
            .model
            .start_instance(&AbsoluteMoniker::from(vec!["a"]), &StartReason::Root)
            .await
            .unwrap();
        assert_eq!(None, a_realm.instance_id());
    }

    async fn wait_until_event_get_timestamp(
        event_stream: &mut EventStream,
        event_type: EventType,
    ) -> zx::Time {
        event_stream.wait_until(event_type, vec![].into()).await.unwrap().event.timestamp.clone()
    }

    #[fuchsia::test]
    async fn already_started() {
        let components = vec![("root", ComponentDeclBuilder::new().build())];

        let instance_id = Some(gen_instance_id(&mut rand::thread_rng()));
        let component_id_index_path = make_index_file(component_id_index::Index {
            instances: vec![component_id_index::InstanceIdEntry {
                instance_id: instance_id.clone(),
                appmgr_moniker: None,
                moniker: Some(AbsoluteMoniker::root()),
            }],
            ..component_id_index::Index::default()
        })
        .unwrap();
        let test = RoutingTestBuilder::new("root", components)
            .set_component_id_index_path(
                component_id_index_path.path().to_str().unwrap().to_string(),
            )
            .build()
            .await;

        let root_realm =
            test.model.start_instance(&AbsoluteMoniker::root(), &StartReason::Root).await.unwrap();

        assert_eq!(
            fsys::StartResult::AlreadyStarted,
            root_realm.start(&StartReason::Root).await.unwrap()
        );
    }

    #[fuchsia::test]
    async fn shutdown_component_interface_no_dynamic() {
        let example_offer = OfferDecl::Directory(OfferDirectoryDecl {
            source: OfferSource::static_child("a".to_string()),
            target: OfferTarget::static_child("b".to_string()),
            source_name: "foo".into(),
            target_name: "foo".into(),
            dependency_type: DependencyType::Strong,
            rights: None,
            subdir: None,
            availability: Availability::Required,
        });
        let example_capability = ProtocolDecl { name: "bar".into(), source_path: None };
        let example_expose = ExposeDecl::Protocol(ExposeProtocolDecl {
            source: ExposeSource::Self_,
            target: ExposeTarget::Parent,
            source_name: "bar".into(),
            target_name: "bar".into(),
        });
        let example_use = UseDecl::Protocol(UseProtocolDecl {
            source: UseSource::Parent,
            source_name: "baz".into(),
            target_path: CapabilityPath::try_from("/svc/baz").expect("parsing"),
            dependency_type: DependencyType::Strong,
            availability: Availability::Required,
        });

        let env_a = EnvironmentDeclBuilder::new()
            .name("env_a")
            .extends(fdecl::EnvironmentExtends::Realm)
            .build();
        let env_b = EnvironmentDeclBuilder::new()
            .name("env_b")
            .extends(fdecl::EnvironmentExtends::Realm)
            .build();

        let root_decl = ComponentDeclBuilder::new()
            .add_environment(env_a.clone())
            .add_environment(env_b.clone())
            .add_child(ChildDeclBuilder::new().name("a").environment("env_a").build())
            .add_child(ChildDeclBuilder::new().name("b").environment("env_b").build())
            .add_lazy_child("c")
            .add_transient_collection("coll")
            .offer(example_offer.clone())
            .expose(example_expose.clone())
            .protocol(example_capability.clone())
            .use_(example_use.clone())
            .build();
        let components = vec![
            ("root", root_decl.clone()),
            ("a", component_decl_with_test_runner()),
            ("b", component_decl_with_test_runner()),
            ("c", component_decl_with_test_runner()),
        ];

        let test = RoutingTestBuilder::new("root", components).build().await;

        let root_component =
            test.model.start_instance(&AbsoluteMoniker::root(), &StartReason::Root).await.unwrap();

        let root_resolved = root_component.lock_resolved_state().await.expect("resolve failed");

        assert_eq!(
            vec![CapabilityDecl::Protocol(example_capability)],
            shutdown::Component::capabilities(&*root_resolved)
        );
        assert_eq!(vec![example_use], shutdown::Component::uses(&*root_resolved));
        assert_eq!(vec![example_offer], shutdown::Component::offers(&*root_resolved));
        assert_eq!(vec![example_expose], shutdown::Component::exposes(&*root_resolved));
        assert_eq!(
            vec![root_decl.collections[0].clone()],
            shutdown::Component::collections(&*root_resolved)
        );
        assert_eq!(vec![env_a, env_b], shutdown::Component::environments(&*root_resolved));

        let mut children = shutdown::Component::children(&*root_resolved);
        children.sort();
        assert_eq!(
            vec![
                shutdown::Child {
                    moniker: "a".into(),
                    environment_name: Some("env_a".to_string()),
                },
                shutdown::Child {
                    moniker: "b".into(),
                    environment_name: Some("env_b".to_string()),
                },
                shutdown::Child { moniker: "c".into(), environment_name: None },
            ],
            children
        );
    }

    #[fuchsia::test]
    async fn shutdown_component_interface_dynamic_children_and_offers() {
        let example_offer = OfferDecl::Directory(OfferDirectoryDecl {
            source: OfferSource::static_child("a".to_string()),
            target: OfferTarget::static_child("b".to_string()),
            source_name: "foo".into(),
            target_name: "foo".into(),
            dependency_type: DependencyType::Strong,
            rights: None,
            subdir: None,
            availability: Availability::Required,
        });

        let components = vec![
            (
                "root",
                ComponentDeclBuilder::new()
                    .add_environment(
                        EnvironmentDeclBuilder::new()
                            .name("env_a")
                            .extends(fdecl::EnvironmentExtends::Realm)
                            .build(),
                    )
                    .add_environment(
                        EnvironmentDeclBuilder::new()
                            .name("env_b")
                            .extends(fdecl::EnvironmentExtends::Realm)
                            .build(),
                    )
                    .add_child(ChildDeclBuilder::new().name("a").environment("env_a").build())
                    .add_lazy_child("b")
                    .add_collection(
                        CollectionDeclBuilder::new_transient_collection("coll_1")
                            .allowed_offers(cm_types::AllowedOffers::StaticAndDynamic)
                            .build(),
                    )
                    .add_collection(
                        CollectionDeclBuilder::new_transient_collection("coll_2")
                            .environment("env_b")
                            .allowed_offers(cm_types::AllowedOffers::StaticAndDynamic)
                            .build(),
                    )
                    .offer(example_offer.clone())
                    .build(),
            ),
            ("a", component_decl_with_test_runner()),
            ("b", component_decl_with_test_runner()),
        ];

        let test = ActionsTest::new("root", components, Some(vec![].into())).await;

        test.create_dynamic_child("coll_1", "a").await;
        test.create_dynamic_child_with_args(
            "coll_1",
            "b",
            fcomponent::CreateChildArgs {
                dynamic_offers: Some(vec![fdecl::Offer::Protocol(fdecl::OfferProtocol {
                    source: Some(fdecl::Ref::Child(fdecl::ChildRef {
                        name: "a".to_string(),
                        collection: Some("coll_1".to_string()),
                    })),
                    source_name: Some("dyn_offer_source_name".to_string()),
                    target_name: Some("dyn_offer_target_name".to_string()),
                    dependency_type: Some(fdecl::DependencyType::Strong),
                    ..fdecl::OfferProtocol::EMPTY
                })]),
                ..fcomponent::CreateChildArgs::EMPTY
            },
        )
        .await;
        test.create_dynamic_child("coll_2", "a").await;

        let example_dynamic_offer = OfferDecl::Protocol(OfferProtocolDecl {
            source: OfferSource::Child(ChildRef {
                name: "a".to_string(),
                collection: Some("coll_1".to_string()),
            }),
            target: OfferTarget::Child(ChildRef {
                name: "b".to_string(),
                collection: Some("coll_1".to_string()),
            }),
            source_name: "dyn_offer_source_name".into(),
            target_name: "dyn_offer_target_name".into(),
            dependency_type: DependencyType::Strong,
            availability: Availability::Required,
        });

        let root_component = test.look_up(vec![].into()).await;

        {
            let root_resolved = root_component.lock_resolved_state().await.expect("resolving");

            let mut children = shutdown::Component::children(&*root_resolved);
            children.sort();
            pretty_assertions::assert_eq!(
                vec![
                    shutdown::Child {
                        moniker: "a".into(),
                        environment_name: Some("env_a".to_string()),
                    },
                    shutdown::Child { moniker: "b".into(), environment_name: None },
                    shutdown::Child { moniker: "coll_1:a".into(), environment_name: None },
                    shutdown::Child { moniker: "coll_1:b".into(), environment_name: None },
                    shutdown::Child {
                        moniker: "coll_2:a".into(),
                        environment_name: Some("env_b".to_string()),
                    },
                ],
                children
            );
            pretty_assertions::assert_eq!(
                vec![example_offer.clone(), example_dynamic_offer.clone()],
                shutdown::Component::offers(&*root_resolved)
            )
        }

        // Destroy `coll_1:b`. It should not be listed. The dynamic offer should be deleted.
        ActionSet::register(root_component.clone(), DestroyChildAction::new("coll_1:b".into(), 2))
            .await
            .expect("destroy failed");

        {
            let root_resolved = root_component.lock_resolved_state().await.expect("resolving");

            let mut children = shutdown::Component::children(&*root_resolved);
            children.sort();
            pretty_assertions::assert_eq!(
                vec![
                    shutdown::Child {
                        moniker: "a".into(),
                        environment_name: Some("env_a".to_string()),
                    },
                    shutdown::Child { moniker: "b".into(), environment_name: None },
                    shutdown::Child { moniker: "coll_1:a".into(), environment_name: None },
                    shutdown::Child {
                        moniker: "coll_2:a".into(),
                        environment_name: Some("env_b".to_string()),
                    },
                ],
                children
            );

            pretty_assertions::assert_eq!(
                vec![example_offer.clone()],
                shutdown::Component::offers(&*root_resolved)
            )
        }

        // Recreate `coll_1:b`, this time with a dynamic offer from `a` in the other
        // collection. Both versions should be listed.
        test.create_dynamic_child_with_args(
            "coll_1",
            "b",
            fcomponent::CreateChildArgs {
                dynamic_offers: Some(vec![fdecl::Offer::Protocol(fdecl::OfferProtocol {
                    source: Some(fdecl::Ref::Child(fdecl::ChildRef {
                        name: "a".to_string(),
                        collection: Some("coll_2".to_string()),
                    })),
                    source_name: Some("dyn_offer2_source_name".to_string()),
                    target_name: Some("dyn_offer2_target_name".to_string()),
                    dependency_type: Some(fdecl::DependencyType::Strong),
                    ..fdecl::OfferProtocol::EMPTY
                })]),
                ..fcomponent::CreateChildArgs::EMPTY
            },
        )
        .await;

        let example_dynamic_offer2 = OfferDecl::Protocol(OfferProtocolDecl {
            source: OfferSource::Child(ChildRef {
                name: "a".to_string(),
                collection: Some("coll_2".to_string()),
            }),
            target: OfferTarget::Child(ChildRef {
                name: "b".to_string(),
                collection: Some("coll_1".to_string()),
            }),
            source_name: "dyn_offer2_source_name".into(),
            target_name: "dyn_offer2_target_name".into(),
            dependency_type: DependencyType::Strong,
            availability: Availability::Required,
        });

        {
            let root_resolved = root_component.lock_resolved_state().await.expect("resolving");

            let mut children = shutdown::Component::children(&*root_resolved);
            children.sort();
            pretty_assertions::assert_eq!(
                vec![
                    shutdown::Child {
                        moniker: "a".into(),
                        environment_name: Some("env_a".to_string()),
                    },
                    shutdown::Child { moniker: "b".into(), environment_name: None },
                    shutdown::Child { moniker: "coll_1:a".into(), environment_name: None },
                    shutdown::Child { moniker: "coll_1:b".into(), environment_name: None },
                    shutdown::Child {
                        moniker: "coll_2:a".into(),
                        environment_name: Some("env_b".to_string()),
                    },
                ],
                children
            );

            pretty_assertions::assert_eq!(
                vec![example_offer.clone(), example_dynamic_offer2.clone()],
                shutdown::Component::offers(&*root_resolved)
            )
        }
    }

    async fn new_component() -> Arc<ComponentInstance> {
        ComponentInstance::new(
            Arc::new(Environment::empty()),
            InstancedAbsoluteMoniker::root(),
            "fuchsia-pkg://fuchsia.com/foo#at_root.cm".to_string(),
            fdecl::StartupMode::Lazy,
            fdecl::OnTerminate::None,
            WeakModelContext::new(Weak::new()),
            WeakExtendedInstanceInterface::AboveRoot(Weak::new()),
            Arc::new(Hooks::new()),
            None,
            false,
        )
    }

    async fn new_resolved() -> InstanceState {
        let comp = new_component().await;
        let decl = ComponentDeclBuilder::new().build();
        let ris = ResolvedInstanceState::new(
            &comp,
            decl,
            None,
            None,
            ComponentAddress::from(&comp.component_url, &comp).await.unwrap(),
            None,
        )
        .await
        .unwrap();
        InstanceState::Resolved(ris)
    }

    #[fuchsia::test]
    async fn instance_state_transitions_test() {
        // New --> Discovered.
        let mut is = InstanceState::New;
        is.set(InstanceState::Unresolved);
        assert_matches!(is, InstanceState::Unresolved);

        // New --> Destroyed.
        let mut is = InstanceState::New;
        is.set(InstanceState::Destroyed);
        assert_matches!(is, InstanceState::Destroyed);

        // Discovered --> Resolved.
        let mut is = InstanceState::Unresolved;
        is.set(new_resolved().await);
        assert_matches!(is, InstanceState::Resolved(_));

        // Discovered --> Destroyed.
        let mut is = InstanceState::Unresolved;
        is.set(InstanceState::Destroyed);
        assert_matches!(is, InstanceState::Destroyed);

        // Resolved --> Discovered.
        let mut is = new_resolved().await;
        is.set(InstanceState::Unresolved);
        assert_matches!(is, InstanceState::Unresolved);

        // Resolved --> Destroyed.
        let mut is = new_resolved().await;
        is.set(InstanceState::Destroyed);
        assert_matches!(is, InstanceState::Destroyed);
    }

    // Macro to make the panicking tests more readable.
    macro_rules! panic_test {
        (   [$(
                $test_name:ident( // Test case name.
                    $($args:expr),+$(,)? // Arguments for test case.
                )
            ),+$(,)?]
        ) => {
            $(paste::paste!{
                #[allow(non_snake_case)]
                #[fuchsia_async::run_until_stalled(test)]
                #[should_panic]
                async fn [< confirm_invalid_transition___ $test_name>]() {
                    confirm_invalid_transition($($args,)+).await;
                }
            })+
        }
    }

    async fn confirm_invalid_transition(cur: InstanceState, next: InstanceState) {
        let mut is = cur;
        is.set(next);
    }

    // Use the panic_test! macro to enumerate the invalid InstanceState transitions that are invalid
    // and should panic. As a result of the macro, the test names will be generated like
    // `confirm_invalid_transition___p2r`.
    panic_test!([
        // Destroyed !-> {Destroyed, Resolved, Discovered, New}..
        p2p(InstanceState::Destroyed, InstanceState::Destroyed),
        p2r(InstanceState::Destroyed, new_resolved().await),
        p2d(InstanceState::Destroyed, InstanceState::Unresolved),
        p2n(InstanceState::Destroyed, InstanceState::New),
        // Resolved !-> {Resolved, New}.
        r2r(new_resolved().await, new_resolved().await),
        r2n(new_resolved().await, InstanceState::New),
        // Discovered !-> {Discovered, New}.
        d2d(InstanceState::Unresolved, InstanceState::Unresolved),
        d2n(InstanceState::Unresolved, InstanceState::New),
        // New !-> {Resolved, New}.
        n2r(InstanceState::New, new_resolved().await),
        n2n(InstanceState::New, InstanceState::New),
    ]);
}

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{
        actions::{
            shutdown, start, ActionSet, DestroyChildAction, DiscoverAction, PurgeChildAction,
            ResolveAction, StartAction, StopAction,
        },
        context::{ModelContext, WeakModelContext},
        environment::Environment,
        error::ModelError,
        exposed_dir::ExposedDir,
        hooks::{Event, EventPayload, Hooks},
        namespace::IncomingNamespace,
        resolver::ResolvedComponent,
        routing::{
            self, route_and_open_capability, OpenOptions, OpenResourceError, OpenRunnerOptions,
            RouteRequest, RoutingError,
        },
    },
    ::cm_logger::fmt::LOGGER as MODEL_LOGGER,
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
        DebugRouteMapper,
    },
    anyhow::format_err,
    async_trait::async_trait,
    clonable_error::ClonableError,
    cm_moniker::{InstanceId, InstancedAbsoluteMoniker, InstancedChildMoniker},
    cm_runner::{component_controller::ComponentController, NullRunner, RemoteRunner, Runner},
    cm_rust::{self, CapabilityName, ChildDecl, CollectionDecl, ComponentDecl, UseDecl},
    cm_task_scope::TaskScope,
    cm_util::channel,
    config_encoder::ConfigFields,
    fidl::endpoints::{self, Proxy, ServerEnd},
    fidl_fuchsia_component as fcomponent, fidl_fuchsia_component_decl as fdecl,
    fidl_fuchsia_component_runner as fcrunner,
    fidl_fuchsia_hardware_power_statecontrol as fstatecontrol,
    fidl_fuchsia_io::{
        self as fio, DirectoryProxy, MODE_TYPE_SERVICE, OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE,
    },
    fidl_fuchsia_process as fprocess, fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync,
    fuchsia_component::client,
    fuchsia_zircon as zx,
    futures::{
        future::{
            join_all, AbortHandle, Abortable, BoxFuture, Either, Future, FutureExt, TryFutureExt,
        },
        lock::{MappedMutexGuard, Mutex, MutexGuard},
    },
    log::{error, warn},
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
    pub package_dir: DirectoryProxy,
}

impl TryFrom<ResolvedComponent> for Component {
    type Error = ModelError;

    fn try_from(
        ResolvedComponent { resolved_url, decl, package, config_values }: ResolvedComponent,
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
        Ok(Self { resolved_url, decl, package, config })
    }
}

impl TryFrom<fsys::Package> for Package {
    type Error = ModelError;

    fn try_from(package: fsys::Package) -> Result<Self, Self::Error> {
        Ok(Self {
            package_url: package.package_url.ok_or(ModelError::PackageUrlMissing)?,
            package_dir: package
                .package_dir
                .ok_or(ModelError::PackageDirectoryMissing)?
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

    /// The context this instance is under.
    context: WeakModelContext,

    // These locks must be taken in the order declared if held simultaneously.
    /// The component's mutable state.
    state: Mutex<InstanceState>,
    /// The componet's execution state.
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
            Arc::new(Hooks::new(None)),
            None,
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
    ) -> Arc<Self> {
        let abs_moniker = instanced_moniker.to_absolute_moniker();
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

    /// Gets the context, if it exists, or returns a '`ContextNotFound` error.
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
                InstanceState::Purged => {
                    return Err(ComponentInstanceError::instance_not_found(
                        self.abs_moniker.clone(),
                    ));
                }
                InstanceState::New | InstanceState::Discovered => {}
            }
            // Drop the lock before doing the work to resolve the state.
        }
        self.resolve()
            .await
            .map_err(|err| ComponentInstanceError::resolve_failed(self.abs_moniker.clone(), err))?;
        let state = self.state.lock().await;
        if let InstanceState::Purged = *state {
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
                    InstanceState::Purged => {
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
                    flags: OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
                    open_mode: MODE_TYPE_SERVICE,
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
        let res = {
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
                if !handles.is_empty() && collection_decl.durability != fdecl::Durability::SingleRun
                {
                    return Err(ModelError::unsupported(
                        "Numbered handles to child in a collection that is not SingleRun",
                    ));
                }
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

            match collection_decl.durability {
                fdecl::Durability::Transient => {}
                fdecl::Durability::SingleRun => {}
                fdecl::Durability::Persistent => {
                    return Err(ModelError::unsupported("Persistent durability"));
                }
            };
            (
                state
                    .add_child(
                        self,
                        child_decl,
                        Some(&collection_decl),
                        child_args.numbered_handles,
                        dynamic_offers,
                    )
                    .await,
                collection_decl.durability,
            )
        };
        match res {
            (Some(discover_nf), durability) => {
                discover_nf.await?;
                Ok(durability)
            }
            (None, _) => {
                let child_moniker =
                    ChildMoniker::new(child_decl.name.clone(), Some(collection_name));
                Err(ModelError::instance_already_exists(self.abs_moniker.clone(), child_moniker))
            }
        }
    }

    /// Removes the dynamic child, returning a future that will execute the
    /// destroy action.
    pub async fn remove_dynamic_child(
        self: &Arc<Self>,
        child_moniker: &ChildMoniker,
    ) -> Result<impl Future<Output = Result<(), ModelError>>, ModelError> {
        let tup = {
            let state = self.lock_resolved_state().await?;
            state.live_children.get(&child_moniker).map(|t| t.clone())
        };
        if let Some(tup) = tup {
            let (instance, _) = tup;
            let instanced_moniker =
                InstancedChildMoniker::from_child_moniker(child_moniker, instance);
            ActionSet::register(self.clone(), DestroyChildAction::new(instanced_moniker.clone()))
                .await?;
            let mut actions = self.lock_actions().await;
            let nf = actions.register_no_wait(self, PurgeChildAction::new(instanced_moniker));
            Ok(nf)
        } else {
            Err(ModelError::instance_not_found_in_realm(
                self.abs_moniker.clone(),
                child_moniker.clone(),
            ))
        }
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

        // When the component is stopped, any child instances in transient collections must be
        // destroyed.
        self.destroy_non_persistent_children().await?;
        if was_running {
            let event = Event::new(self, Ok(EventPayload::Stopped { status: stop_result }));
            self.hooks.dispatch(&event).await?;
        }
        if let ExtendedInstance::Component(parent) = self.try_get_parent()? {
            let instanced_moniker = self.instanced_child_moniker().unwrap();
            parent.destroy_child_if_single_run(&instanced_moniker).await?;
        }
        Ok(())
    }

    async fn destroy_child_if_single_run(
        self: &Arc<Self>,
        instanced_moniker: &InstancedChildMoniker,
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
                    fdecl::Durability::Persistent => None,
                    fdecl::Durability::Transient => None,
                })
                .collect();
            single_run_colls
        };
        if let Some(coll) = instanced_moniker.collection() {
            if single_run_colls.contains(coll) {
                let component = self.clone();
                let instanced_moniker = instanced_moniker.clone();
                fasync::Task::spawn(async move {
                    match ActionSet::register(
                        component.clone(),
                        DestroyChildAction::new(instanced_moniker.clone()),
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
                    let mut actions = component.lock_actions().await;

                    // This returns a Future that does not need to be polled.
                    let _ = actions
                        .register_no_wait(&component, PurgeChildAction::new(instanced_moniker.clone()));
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
            state.all_children().values().map(|m| m.clone()).collect()
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
    // TODO: Need to:
    // - Delete the instance's persistent marker, if it was a persistent dynamic instance
    pub async fn destroy_instance(self: &Arc<Self>) -> Result<(), ModelError> {
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
                    Err(e) => {
                        // We received an error we weren't expecting, but we still want to destroy
                        // this instance. It's bad to leave storage state undeleted, but it would
                        // be worse to not continue with destroying this instance. Log the error,
                        // and proceed.
                        warn!("failed to delete storage during instance destruction for component {}, proceeding with destruction anyway: {}", self.abs_moniker, e);
                    }
                }
            }
        }
        Ok(())
    }

    /// Registers actions to destroy all children of this instance that live in non-persistent
    /// collections.
    async fn destroy_non_persistent_children(self: &Arc<Self>) -> Result<(), ModelError> {
        let (transient_colls, instanced_monikers) = {
            let state = self.lock_state().await;
            let state = match *state {
                InstanceState::Resolved(ref s) => s,
                _ => {
                    // Component instance was not resolved, so no dynamic children.
                    return Ok(());
                }
            };
            let transient_colls: HashSet<_> = state
                .decl()
                .collections
                .iter()
                .filter_map(|c| match c.durability {
                    fdecl::Durability::Transient => Some(c.name.clone()),
                    fdecl::Durability::Persistent => None,
                    fdecl::Durability::SingleRun => Some(c.name.clone()),
                })
                .collect();
            let instanced_monikers: Vec<_> =
                state.all_children().keys().map(|m| m.clone()).collect();
            (transient_colls, instanced_monikers)
        };
        let mut futures = vec![];
        for m in instanced_monikers {
            // Delete a child if its collection is in the set of transient collections created
            // above.
            if let Some(coll) = m.collection() {
                if transient_colls.contains(coll) {
                    ActionSet::register(self.clone(), DestroyChildAction::new(m.clone())).await?;
                    let nf = ActionSet::register(self.clone(), PurgeChildAction::new(m));
                    futures.push(nf);
                }
            }
        }
        join_all(futures).await.into_iter().fold(Ok(()), |acc, r| acc.and_then(|_| r))
    }

    pub async fn open_outgoing(
        &self,
        flags: u32,
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
        let path = io_util::canonicalize_path(path);
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
                // TODO(fxbug.dev/36541): Until directory capabilities specify rights, we always
                // open directories using OPEN_FLAG_POSIX_WRITABLE and OPEN_FLAG_POSIX_EXECUTABLE
                // which expands the new connection's rights to those of the parent connection.
                let flags = fio::OPEN_RIGHT_READABLE
                    | fio::OPEN_FLAG_POSIX_WRITABLE
                    | fio::OPEN_FLAG_POSIX_EXECUTABLE;
                let server_chan = channel::take_channel(server_chan);
                let server_end = ServerEnd::new(server_chan);
                exposed_dir.open(flags, fio::MODE_TYPE_DIRECTORY, Path::dot(), server_end);
                Ok(())
            }
            InstanceState::Purged => Err(ModelError::instance_not_found(self.abs_moniker.clone())),
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
                    .live_children()
                    .filter_map(|(_, r)| match r.startup {
                        fdecl::StartupMode::Eager => Some(r.clone()),
                        fdecl::StartupMode::Lazy => None,
                    })
                    .collect(),
                InstanceState::Purged => {
                    return Err(ModelError::instance_not_found(self.abs_moniker.clone()));
                }
                InstanceState::New | InstanceState::Discovered => {
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

    pub fn instance_id(self: &Arc<Self>) -> Option<ComponentInstanceId> {
        self.try_get_context()
            .map(|ctx| ctx.component_id_index().look_up_moniker(&self.abs_moniker).cloned())
            .unwrap_or(None)
    }

    /// Logs to the `fuchsia.logger.LogSink` capability in the component's incoming namespace,
    /// or to component manager's logger if the component is not running or has not requested
    /// `fuchsia.logger.LogSink`.
    pub async fn log(&self, level: log::Level, message: String) {
        let execution = self.lock_execution().await;
        let logger = match &execution.runtime {
            Some(Runtime { namespace: Some(ns), .. }) => ns.get_logger(),
            _ => &*MODEL_LOGGER,
        };
        logger.log(level, format_args!("{}", &message))
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

    fn instanced_child_moniker(&self) -> Option<&InstancedChildMoniker> {
        self.instanced_moniker.leaf()
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
    Discovered,
    /// The instance has been resolved.
    Resolved(ResolvedInstanceState),
    /// The instance has been destroyed. It has no content and no further actions may be registered
    /// on it.
    Purged,
}

impl InstanceState {
    /// Changes the state, checking invariants.
    pub fn set(&mut self, next: Self) {
        let invalid = match (&self, &next) {
            (Self::New, Self::Resolved(_))
            | (Self::Discovered, Self::New)
            | (Self::Resolved(_), Self::Discovered)
            | (Self::Resolved(_), Self::New)
            | (Self::Purged, Self::New)
            | (Self::Purged, Self::Discovered)
            | (Self::Purged, Self::Resolved(_)) => true,
            _ => false,
        };
        if invalid {
            panic!("Invalid instance state transition from {:?} to {:?}", self, next);
        }
        *self = next;
    }
}

impl fmt::Debug for InstanceState {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let s = match self {
            Self::New => "New",
            Self::Discovered => "Discovered",
            Self::Resolved(_) => "Resolved",
            Self::Purged => "Purged",
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
        ResolvedInstanceState::all_children(self)
            .iter()
            .map(|(moniker, instance)| shutdown::Child {
                moniker: moniker.clone(),
                environment_name: instance.environment().name().map(|n| n.to_string()),
                // Component is live if `live_children` contains its partial
                // moniker, and the `instance_id` of the live child matches.
                is_live: self
                    .live_children
                    .get(&moniker.to_child_moniker())
                    .map(|(live_instance_id, _)| *live_instance_id)
                    == Some(moniker.instance()),
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
    /// All child instances, indexed by instanced moniker.
    children: HashMap<InstancedChildMoniker, Arc<ComponentInstance>>,
    /// Child instances that have not been deleted, indexed by child moniker.
    live_children: HashMap<ChildMoniker, (InstanceId, Arc<ComponentInstance>)>,
    /// The next unique identifier for a dynamic children created in this realm.
    /// (Static instances receive identifier 0.)
    next_dynamic_instance_id: InstanceId,
    /// The set of named Environments defined by this instance.
    environments: HashMap<String, Arc<Environment>>,
    /// Hosts a directory mapping the component's exposed capabilities.
    exposed_dir: ExposedDir,
    /// Dynamic offers targeting this component's dynamic children.
    ///
    /// Invariant: the `target` field of all offers must refer to a live dynamic
    /// child (i.e., a member of `live_children`), and if the `source` field
    /// refers to a dynamic child, it must also be live.
    dynamic_offers: Vec<cm_rust::OfferDecl>,
}

impl ResolvedInstanceState {
    pub async fn new(
        component: &Arc<ComponentInstance>,
        decl: ComponentDecl,
    ) -> Result<Self, ModelError> {
        let exposed_dir = ExposedDir::new(
            ExecutionScope::new(),
            WeakComponentInstance::new(&component),
            decl.clone(),
        )?;
        let mut state = Self {
            execution_scope: ExecutionScope::new(),
            decl: decl.clone(),
            children: HashMap::new(),
            live_children: HashMap::new(),
            next_dynamic_instance_id: 1,
            environments: Self::instantiate_environments(component, &decl),
            exposed_dir,
            dynamic_offers: vec![],
        };
        state.add_static_children(component, &decl).await;
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

    /// Returns an iterator over live children.
    pub fn live_children(&self) -> impl Iterator<Item = (&ChildMoniker, &Arc<ComponentInstance>)> {
        self.live_children.iter().map(|(k, v)| (k, &v.1))
    }

    /// Returns a reference to a live child.
    pub fn get_live_child(&self, m: &ChildMoniker) -> Option<Arc<ComponentInstance>> {
        self.live_children.get(m).map(|(_, v)| v.clone())
    }

    /// Returns a vector of the live children in `collection`.
    pub fn live_children_in_collection(
        &self,
        collection: &str,
    ) -> Vec<(ChildMoniker, Arc<ComponentInstance>)> {
        self.live_children()
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
            .map(|(_, component)| component.clone())
            .collect()
    }

    /// Returns a live child's instance id.
    pub fn get_live_child_instance_id(&self, m: &ChildMoniker) -> Option<InstanceId> {
        self.live_children.get(m).map(|(i, _)| *i)
    }

    /// Given a `ChildMoniker` returns the `InstancedChildMoniker`
    pub fn get_live_child_moniker(&self, m: &ChildMoniker) -> Option<InstancedChildMoniker> {
        self.live_children.get(m).map(|(i, _)| InstancedChildMoniker::from_child_moniker(m, *i))
    }

    pub fn get_all_child_monikers(&self, m: &ChildMoniker) -> Vec<InstancedChildMoniker> {
        self.children
            .iter()
            .filter(|(child, _)| m.name() == child.name() && m.collection() == child.collection())
            .map(|(child, _)| child.clone())
            .collect()
    }

    /// Returns a reference to the list of all children.
    pub fn all_children(&self) -> &HashMap<InstancedChildMoniker, Arc<ComponentInstance>> {
        &self.children
    }

    /// Returns a child `ComponentInstance`. The child may or may not be live.
    pub fn get_child(&self, cm: &InstancedChildMoniker) -> Option<Arc<ComponentInstance>> {
        self.children.get(cm).map(|i| i.clone())
    }

    /// Returns the exposed directory bound to this instance.
    pub fn get_exposed_dir(&self) -> &ExposedDir {
        &self.exposed_dir
    }

    /// Extends an instanced absolute moniker with the live child with moniker `p`. Returns `None`
    /// if no matching child was found.
    pub fn extend_moniker_with(
        &self,
        moniker: &InstancedAbsoluteMoniker,
        child_moniker: &ChildMoniker,
    ) -> Option<InstancedAbsoluteMoniker> {
        match self.get_live_child_instance_id(child_moniker) {
            Some(instance_id) => Some(
                moniker
                    .child(InstancedChildMoniker::from_child_moniker(child_moniker, instance_id)),
            ),
            None => None,
        }
    }

    /// Returns all deleting children.
    pub fn get_deleting_children(&self) -> HashMap<InstancedChildMoniker, Arc<ComponentInstance>> {
        let mut deleting_children = HashMap::new();
        for (m, r) in self.all_children().iter() {
            if self.get_live_child(&m.to_child_moniker()).is_none() {
                deleting_children.insert(m.clone(), r.clone());
            }
        }
        deleting_children
    }

    /// Marks a live child deleting. No-op if the child is already deleting.
    pub fn mark_child_deleted(&mut self, child_moniker: &ChildMoniker) {
        if self.live_children.remove(&child_moniker).is_none() {
            return;
        }

        // Delete any dynamic offers whose `source` or `target` matches the
        // component we're deleting.
        self.dynamic_offers.retain(|offer| {
            use cm_rust::OfferDeclCommon;
            let source_matches = offer.source()
                == &cm_rust::OfferSource::Child(cm_rust::ChildRef {
                    name: child_moniker.name.clone(),
                    collection: child_moniker.collection.clone(),
                });
            let target_matches = offer.target()
                == &cm_rust::OfferTarget::Child(cm_rust::ChildRef {
                    name: child_moniker.name.clone(),
                    collection: child_moniker.collection.clone(),
                });
            !source_matches && !target_matches
        });
    }

    /// Removes a child.
    pub fn remove_child(&mut self, moniker: &InstancedChildMoniker) {
        self.children.remove(moniker);
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
    ) -> Option<BoxFuture<'static, Result<(), ModelError>>> {
        let child = self
            .add_child_internal(component, child, collection, numbered_handles, dynamic_offers)
            .await?;

        // We can dispatch a Discovered event for the component now that it's installed in the
        // tree, which means any Discovered hooks will capture it.
        let mut actions = child.lock_actions().await;
        Some(actions.register_no_wait(&child, DiscoverAction::new()).boxed())
    }

    /// Adds a new child of this instance for the given `ChildDecl`. Returns
    /// `true` if the creation succeeded, and `false` if a child with the same
    /// name already existed. Like `add_child`, but doesn't register a
    /// `Discover` action, and therefore doesn't return a future to wait for.
    #[cfg(test)]
    pub async fn add_child_no_discover(
        &mut self,
        component: &Arc<ComponentInstance>,
        child: &ChildDecl,
        collection: Option<&CollectionDecl>,
    ) -> bool {
        self.add_child_internal(component, child, collection, None, None).await.is_some()
    }

    async fn add_child_internal(
        &mut self,
        component: &Arc<ComponentInstance>,
        child: &ChildDecl,
        collection: Option<&CollectionDecl>,
        numbered_handles: Option<Vec<fidl_fuchsia_process::HandleInfo>>,
        dynamic_offers: Option<Vec<cm_rust::OfferDecl>>,
    ) -> Option<Arc<ComponentInstance>> {
        let instance_id = match collection {
            Some(_) => {
                let id = self.next_dynamic_instance_id;
                self.next_dynamic_instance_id += 1;
                id
            }
            None => 0,
        };
        let instanced_moniker = InstancedChildMoniker::new(
            child.name.clone(),
            collection.map(|c| c.name.clone()),
            instance_id,
        );
        let child_moniker = instanced_moniker.to_child_moniker();
        if self.get_live_child(&child_moniker).is_some() {
            return None;
        }
        let child = ComponentInstance::new(
            self.environment_for_child(component, child, collection.clone()),
            component.instanced_moniker.child(instanced_moniker.clone()),
            child.url.clone(),
            child.startup,
            child.on_terminate.unwrap_or(fdecl::OnTerminate::None),
            component.context.clone(),
            WeakExtendedInstance::Component(WeakComponentInstance::from(component)),
            Arc::new(Hooks::new(Some(component.hooks.clone()))),
            numbered_handles,
        );
        self.children.insert(instanced_moniker, child.clone());
        self.live_children.insert(child_moniker, (instance_id, child.clone()));
        if let Some(dynamic_offers) = dynamic_offers {
            self.dynamic_offers.extend(dynamic_offers.into_iter());
        }
        Some(child)
    }

    async fn add_static_children(
        &mut self,
        component: &Arc<ComponentInstance>,
        decl: &ComponentDecl,
    ) {
        for child in decl.children.iter() {
            self.add_child(component, child, None, None, None).await;
        }
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

    fn get_live_child(&self, moniker: &ChildMoniker) -> Option<Arc<ComponentInstance>> {
        ResolvedInstanceState::get_live_child(self, moniker)
    }

    fn live_children_in_collection(
        &self,
        collection: &str,
    ) -> Vec<(ChildMoniker, Arc<ComponentInstance>)> {
        ResolvedInstanceState::live_children_in_collection(self, collection)
    }
}

/// The execution state for a component instance that has started running.
pub struct Runtime {
    /// Holder for objects related to the component's incoming namespace.
    pub namespace: Option<IncomingNamespace>,

    /// A client handle to the component instance's outgoing directory.
    pub outgoing_dir: Option<DirectoryProxy>,

    /// A client handle to the component instance's runtime directory hosted by the runner.
    pub runtime_dir: Option<DirectoryProxy>,

    /// Used to interact with the Runner to influence the component's execution.
    pub controller: Option<ComponentController>,

    /// Approximates when the component was started.
    pub timestamp: zx::Time,

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
    /// error occured in the send process.
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
        outgoing_dir: Option<DirectoryProxy>,
        runtime_dir: Option<DirectoryProxy>,
        controller: Option<fcrunner::ComponentControllerProxy>,
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
                            .unwrap_or_else(|e| error!("failed to register action: {}", e));
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
            actions::ShutdownAction,
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
            CapabilityDecl, CapabilityPath, ChildRef, DependencyType, EventMode, ExposeDecl,
            ExposeProtocolDecl, ExposeSource, ExposeTarget, OfferDecl, OfferDirectoryDecl,
            OfferProtocolDecl, OfferSource, OfferTarget, ProtocolDecl, UseProtocolDecl, UseSource,
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

    #[test]
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
                panic!("future shoud have completed!");
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

    #[test]
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

    #[test]
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

    #[test]
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
    async fn started_and_running_event_timestamp_matches_component() {
        let test =
            RoutingTest::new("root", vec![("root", ComponentDeclBuilder::new().build())]).await;

        let mut event_source = test
            .builtin_environment
            .event_source_factory
            .create_for_debug()
            .await
            .expect("create event source");
        let mut event_stream = event_source
            .subscribe(
                vec![
                    EventType::Discovered.into(),
                    EventType::Resolved.into(),
                    EventType::Started.into(),
                ]
                .into_iter()
                .map(|event| EventSubscription::new(event, EventMode::Async))
                .collect(),
            )
            .await
            .expect("subscribe to event stream");
        event_source.start_component_tree().await;

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

        assert!(discovered_timestamp < resolved_timestamp);
        assert!(resolved_timestamp < started_timestamp);

        let component = bind_handle.await;
        let component_timestamp =
            component.lock_execution().await.runtime.as_ref().unwrap().timestamp;
        assert_eq!(component_timestamp, started_timestamp);

        let mut event_stream = event_source
            .subscribe(vec![EventSubscription::new(EventType::Running.into(), EventMode::Async)])
            .await
            .expect("subscribe to event stream");
        let event = event_stream.wait_until(EventType::Running, vec![].into()).await.unwrap().event;
        assert_matches!(
            event.result,
            Ok(EventPayload::Running { started_timestamp: timestamp })
            if timestamp == started_timestamp);
        assert!(event.timestamp > started_timestamp);
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
            .create_for_debug()
            .await
            .expect("failed creating event source");
        let mut stop_event_stream = event_source
            .subscribe(vec![EventSubscription::new(EventType::Stopped.into(), EventMode::Async)])
            .await
            .expect("couldn't susbscribe to event stream");

        event_source.start_component_tree().await;
        let a_moniker: InstancedAbsoluteMoniker = vec!["a:0"].into();
        let b_moniker: InstancedAbsoluteMoniker = vec!["a:0", "b:0"].into();

        let component_b = test.look_up(b_moniker.to_absolute_moniker()).await;

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
        ActionSet::register(
            test.look_up(a_moniker.to_absolute_moniker()).await,
            ShutdownAction::new(),
        )
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
                    moniker: "a:0".into(),
                    environment_name: Some("env_a".to_string()),
                    is_live: true
                },
                shutdown::Child {
                    moniker: "b:0".into(),
                    environment_name: Some("env_b".to_string()),
                    is_live: true
                },
                shutdown::Child { moniker: "c:0".into(), environment_name: None, is_live: true },
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
                            .allowed_offers(cm_types::AllowedOffers::StaticAndDynamic)
                            .environment("env_b")
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
        });

        let root_component = test.look_up(vec![].into()).await;

        {
            let root_resolved = root_component.lock_resolved_state().await.expect("resolving");

            let mut children = shutdown::Component::children(&*root_resolved);
            children.sort();
            pretty_assertions::assert_eq!(
                vec![
                    shutdown::Child {
                        moniker: "a:0".into(),
                        environment_name: Some("env_a".to_string()),
                        is_live: true
                    },
                    shutdown::Child {
                        moniker: "b:0".into(),
                        environment_name: None,
                        is_live: true
                    },
                    shutdown::Child {
                        moniker: "coll_1:a:1".into(),
                        environment_name: None,
                        is_live: true
                    },
                    shutdown::Child {
                        moniker: "coll_1:b:2".into(),
                        environment_name: None,
                        is_live: true
                    },
                    shutdown::Child {
                        moniker: "coll_2:a:3".into(),
                        environment_name: Some("env_b".to_string()),
                        is_live: true
                    },
                ],
                children
            );
            pretty_assertions::assert_eq!(
                vec![example_offer.clone(), example_dynamic_offer.clone()],
                shutdown::Component::offers(&*root_resolved)
            )
        }

        // Destroy `coll_1:b`. It should still be listed, but shouldn't be live.
        // The dynamic offer should be deleted.
        ActionSet::register(root_component.clone(), DestroyChildAction::new("coll_1:b:2".into()))
            .await
            .expect("destroy failed");

        {
            let root_resolved = root_component.lock_resolved_state().await.expect("resolving");

            let mut children = shutdown::Component::children(&*root_resolved);
            children.sort();
            pretty_assertions::assert_eq!(
                vec![
                    shutdown::Child {
                        moniker: "a:0".into(),
                        environment_name: Some("env_a".to_string()),
                        is_live: true
                    },
                    shutdown::Child {
                        moniker: "b:0".into(),
                        environment_name: None,
                        is_live: true
                    },
                    shutdown::Child {
                        moniker: "coll_1:a:1".into(),
                        environment_name: None,
                        is_live: true
                    },
                    shutdown::Child {
                        moniker: "coll_1:b:2".into(),
                        environment_name: None,
                        is_live: false
                    },
                    shutdown::Child {
                        moniker: "coll_2:a:3".into(),
                        environment_name: Some("env_b".to_string()),
                        is_live: true
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
        });

        {
            let root_resolved = root_component.lock_resolved_state().await.expect("resolving");

            let mut children = shutdown::Component::children(&*root_resolved);
            children.sort();
            pretty_assertions::assert_eq!(
                vec![
                    shutdown::Child {
                        moniker: "a:0".into(),
                        environment_name: Some("env_a".to_string()),
                        is_live: true
                    },
                    shutdown::Child {
                        moniker: "b:0".into(),
                        environment_name: None,
                        is_live: true
                    },
                    shutdown::Child {
                        moniker: "coll_1:a:1".into(),
                        environment_name: None,
                        is_live: true
                    },
                    shutdown::Child {
                        moniker: "coll_1:b:2".into(),
                        environment_name: None,
                        is_live: false
                    },
                    shutdown::Child {
                        moniker: "coll_1:b:4".into(),
                        environment_name: None,
                        is_live: true
                    },
                    shutdown::Child {
                        moniker: "coll_2:a:3".into(),
                        environment_name: Some("env_b".to_string()),
                        is_live: true
                    },
                ],
                children
            );

            pretty_assertions::assert_eq!(
                vec![example_offer.clone(), example_dynamic_offer2.clone()],
                shutdown::Component::offers(&*root_resolved)
            )
        }

        // Finish destroying the original `coll_1:b`.
        ActionSet::register(root_component.clone(), PurgeChildAction::new("coll_1:b:2".into()))
            .await
            .expect("purge failed");

        {
            let root_resolved = root_component.lock_resolved_state().await.expect("resolving");

            let mut children = shutdown::Component::children(&*root_resolved);
            children.sort();
            pretty_assertions::assert_eq!(
                vec![
                    shutdown::Child {
                        moniker: "a:0".into(),
                        environment_name: Some("env_a".to_string()),
                        is_live: true
                    },
                    shutdown::Child {
                        moniker: "b:0".into(),
                        environment_name: None,
                        is_live: true
                    },
                    shutdown::Child {
                        moniker: "coll_1:a:1".into(),
                        environment_name: None,
                        is_live: true
                    },
                    shutdown::Child {
                        moniker: "coll_1:b:4".into(),
                        environment_name: None,
                        is_live: true
                    },
                    shutdown::Child {
                        moniker: "coll_2:a:3".into(),
                        environment_name: Some("env_b".to_string()),
                        is_live: true
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
}

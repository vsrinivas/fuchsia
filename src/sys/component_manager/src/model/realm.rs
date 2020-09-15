// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        channel,
        model::{
            actions::{Action, ActionSet, Notification},
            binding,
            environment::Environment,
            error::ModelError,
            exposed_dir::ExposedDir,
            hooks::{Event, EventError, EventErrorPayload, EventPayload, Hooks},
            moniker::{AbsoluteMoniker, ChildMoniker, ExtendedMoniker, InstanceId, PartialMoniker},
            namespace::IncomingNamespace,
            resolver::Resolver,
            routing::{self, RoutingError},
            runner::{NullRunner, RemoteRunner, Runner},
        },
    },
    clonable_error::ClonableError,
    cm_rust::{
        self, CapabilityPath, ChildDecl, CollectionDecl, ComponentDecl, UseDecl, UseStorageDecl,
    },
    fidl::endpoints::{create_endpoints, Proxy, ServerEnd},
    fidl_fuchsia_component_runner as fcrunner,
    fidl_fuchsia_io::{self as fio, DirectoryProxy, MODE_TYPE_SERVICE, OPEN_RIGHT_READABLE},
    fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync,
    fuchsia_zircon::{self as zx, AsHandleRef},
    futures::future::TryFutureExt,
    futures::{
        future::{join_all, AbortHandle, Abortable, BoxFuture, Either, FutureExt},
        lock::{MappedMutexGuard, Mutex, MutexGuard},
        StreamExt,
    },
    log::warn,
    std::convert::TryInto,
    std::iter::Iterator,
    std::{
        boxed::Box,
        clone::Clone,
        collections::{HashMap, HashSet},
        fmt,
        ops::Drop,
        path::PathBuf,
        sync::{Arc, Weak},
        time::Duration,
    },
    vfs::path::Path,
};

/// Describes the reason a realm is being requested to start.
#[derive(Clone, Debug, Hash, PartialEq, Eq)]
pub enum BindReason {
    /// Indicates that the target is starting the component because it wishes to access
    /// the capability at path.
    AccessCapability { target: ExtendedMoniker, path: CapabilityPath },
    /// Indicates that the component is starting becasue the framework wishes to use
    /// /pkgfs.
    BasePkgResolver,
    /// Indicates that the component is starting because a call to bind_child was made.
    BindChild { parent: AbsoluteMoniker },
    /// Indicates that the component was marked as eagerly starting by the parent.
    // TODO(fxb/50714): Include the parent BindReason.
    // parent: ExtendedMoniker,
    // parent_bind_reason: Option<Arc<BindReason>>
    Eager,
    /// Indicates that this component is starting because it is the root component.
    Root,
    /// Indicates that this component is starting because it was scheduled by WorkScheduler.
    Scheduled,
    /// This is an unsupported BindReason. If you are seeing this then this is a bug.
    Unsupported,
}

impl fmt::Display for BindReason {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{}",
            match self {
                BindReason::AccessCapability { target, path } => {
                    format!("'{}' requested access to '{}'", target, path)
                }
                BindReason::BasePkgResolver => {
                    "the base package resolver attempted to open /pkgfs".to_string()
                }
                BindReason::BindChild { parent } => {
                    format!("its parent '{}' requested to bind to it", parent)
                }
                BindReason::Eager => "it's eager".to_string(),
                BindReason::Root => "it's the root".to_string(),
                BindReason::Scheduled => "it was scheduled to run".to_string(),
                BindReason::Unsupported => "this is a bug".to_string(),
            }
        )
    }
}
/// A returned type corresponding to a resolved component manifest.
pub struct Component {
    /// The URL of the resolved component.
    pub resolved_url: String,
    /// The declaration of the resolved manifest.
    pub decl: ComponentDecl,
    /// The package that this resolved component belongs to.
    pub package: Option<fsys::Package>,
}

pub const DEFAULT_KILL_TIMEOUT: Duration = Duration::from_secs(1);

/// A wrapper for a weak reference to `Realm`. Provides the absolute moniker of the
/// realm, which is useful for error reporting if the original `Realm` has been destroyed.
#[derive(Clone)]
pub struct WeakRealm {
    inner: Weak<Realm>,
    /// The absolute moniker of the original realm.
    pub moniker: AbsoluteMoniker,
}

impl From<&Arc<Realm>> for WeakRealm {
    fn from(realm: &Arc<Realm>) -> Self {
        Self { inner: Arc::downgrade(realm), moniker: realm.abs_moniker.clone() }
    }
}

impl WeakRealm {
    /// Attempts to upgrade this `WeakRealm` into an `Arc<Realm>`, if the original realm has not
    /// been destroyed.
    pub fn upgrade(&self) -> Result<Arc<Realm>, ModelError> {
        self.inner.upgrade().ok_or_else(|| ModelError::instance_not_found(self.moniker.clone()))
    }
}

impl fmt::Debug for WeakRealm {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("WeakRealm").field("moniker", &self.moniker).finish()
    }
}

/// A realm is a container for an individual component instance and its children.  It is provided
/// by the parent of the instance or by the component manager itself in the case of the root realm.
///
/// The realm's properties influence the runtime behavior of the subtree of component instances
/// that it contains, including component resolution, execution, and service discovery.
pub struct Realm {
    /// The registry for resolving component URLs within the realm.
    pub environment: Environment,
    /// The component's URL.
    pub component_url: String,
    /// The mode of startup (lazy or eager).
    pub startup: fsys::StartupMode,
    /// The parent, a.k.a. containing realm. `None` for the root realm.
    parent: Option<Weak<Realm>>,
    /// The absolute moniker of this realm.
    pub abs_moniker: AbsoluteMoniker,
    /// The hooks scoped to this realm.
    pub hooks: Arc<Hooks>,

    // These locks must be taken in the order declared if held simultaneously.
    /// The component's mutable state.
    state: Mutex<Option<RealmState>>,
    /// The component's execution state.
    execution: Mutex<ExecutionState>,
    /// Actions on the realm that must eventually be completed.
    actions: Mutex<ActionSet>,
}

impl Realm {
    /// Instantiates a new root realm.
    pub fn new_root_realm(environment: Environment, component_url: String) -> Self {
        Self {
            environment,
            abs_moniker: AbsoluteMoniker::root(),
            component_url,
            // Started by main().
            startup: fsys::StartupMode::Lazy,
            parent: None,
            state: Mutex::new(None),
            execution: Mutex::new(ExecutionState::new()),
            actions: Mutex::new(ActionSet::new()),
            hooks: Arc::new(Hooks::new(None)),
        }
    }

    /// Returns a new `WeakRealm` pointing to this realm.
    pub fn as_weak(self: &Arc<Self>) -> WeakRealm {
        WeakRealm { inner: Arc::downgrade(self), moniker: self.abs_moniker.clone() }
    }

    /// Locks and returns the realm's mutable state. There is no guarantee that the realm
    /// has a populated `RealmState`. Use [`lock_resolved_state`] if the `RealmState` should
    /// be resolved.
    ///
    /// [`lock_resolved_state`]: Realm::lock_resolved_state
    pub async fn lock_state(&self) -> MutexGuard<'_, Option<RealmState>> {
        self.state.lock().await
    }

    /// Locks and returns the realm's execution state.
    pub async fn lock_execution(&self) -> MutexGuard<'_, ExecutionState> {
        self.execution.lock().await
    }

    /// Locks and returns the realm's action set.
    pub async fn lock_actions(&self) -> MutexGuard<'_, ActionSet> {
        self.actions.lock().await
    }

    /// Gets the parent, if it still exists, or returns an `InstanceNotFound` error. Returns `None`
    /// for the root component.
    pub fn try_get_parent(&self) -> Result<Option<Arc<Realm>>, ModelError> {
        self.parent
            .as_ref()
            .map(|p| {
                p.upgrade()
                    .ok_or(ModelError::instance_not_found(self.abs_moniker.parent().unwrap()))
            })
            .transpose()
    }

    /// Locks and returns a lazily resolved and populated `RealmState`.
    pub async fn lock_resolved_state<'a>(
        self: &'a Arc<Self>,
    ) -> Result<MappedMutexGuard<'a, Option<RealmState>, RealmState>, ModelError> {
        {
            let state = self.state.lock().await;
            if state.is_some() {
                return Ok(MutexGuard::map(state, |s| s.as_mut().unwrap()));
            }
            // Drop the lock before doing the work to resolve the state.
        }
        self.resolve().await?;
        Ok(MutexGuard::map(self.state.lock().await, |s| s.as_mut().unwrap()))
    }

    /// Resolves the component declaration and creates a new populated `RealmState` as necessary.
    /// A `Resolved` event is dispatched if a new `RealmState` is created or an error occurs.
    pub async fn resolve(self: &Arc<Self>) -> Result<Component, ModelError> {
        let component =
            self.environment.resolve(&self.component_url).await.map_err(|err| err.into());
        self.populate_decl(component).await
    }

    /// Populates the component declaration of this realm's Instance using the provided
    /// `component` if not already populated.
    async fn populate_decl(
        self: &Arc<Self>,
        component: Result<fsys::Component, ModelError>,
    ) -> Result<Component, ModelError> {
        let result = async move {
            let component = component?;
            let decl = component.decl.ok_or(ModelError::ComponentInvalid)?;
            let decl = decl
                .try_into()
                .map_err(|e| ModelError::manifest_invalid(self.component_url.clone(), e))?;

            let created_new_realm_state = {
                let mut state = self.lock_state().await;
                if state.is_none() {
                    *state = Some(RealmState::new(self, &decl).await?);
                    true
                } else {
                    false
                }
            };

            let component = Component {
                resolved_url: component.resolved_url.ok_or(ModelError::ComponentInvalid)?,
                decl,
                package: component.package,
            };
            Ok((created_new_realm_state, component))
        }
        .await;

        // If a `RealmState` was installed in this call, first dispatch
        // `Resolved` for the component itself and then dispatch
        // `Discovered` for every static child that was discovered in the
        // manifest.
        match result {
            Ok((false, component)) => {
                return Ok(component);
            }
            Ok((true, component)) => {
                if self.parent.is_none() {
                    let event = Event::new(self, Ok(EventPayload::Discovered));
                    self.hooks.dispatch(&event).await?;
                }
                let event =
                    Event::new(self, Ok(EventPayload::Resolved { decl: component.decl.clone() }));
                self.hooks.dispatch(&event).await?;
                for child in component.decl.children.iter() {
                    let child_moniker = ChildMoniker::new(child.name.clone(), None, 0);
                    let child_abs_moniker = self.abs_moniker.child(child_moniker);
                    let event = Event::child_discovered(child_abs_moniker, child.url.clone());
                    self.hooks.dispatch(&event).await?;
                }
                return Ok(component);
            }
            Err(e) => {
                let event = Event::new(self, Err(EventError::new(&e, EventErrorPayload::Resolved)));
                self.hooks.dispatch(&event).await?;
                return Err(e);
            }
        }
    }

    /// Resolves and populates this component's meta directory handle if it has not been done so
    /// already, and returns a reference to the handle. If this component does not use meta storage
    /// Ok(None) will be returned.
    pub async fn resolve_meta_dir(
        self: &Arc<Self>,
        bind_reason: &BindReason,
    ) -> Result<Option<Arc<DirectoryProxy>>, ModelError> {
        {
            // If our meta directory has already been resolved, just return the answer.
            let state = self.lock_state().await;
            let state = state.as_ref().expect("resolve_meta_dir: not resolved");
            if state.meta_dir.is_some() {
                return Ok(Some(state.meta_dir.as_ref().unwrap().clone()));
            }

            // If we don't even have a meta directory, return None.
            if !state.decl().uses.iter().any(|u| u == &UseDecl::Storage(UseStorageDecl::Meta)) {
                return Ok(None);
            }

            // Don't hold the state lock while performing routing for the meta storage capability,
            // as the routing logic may want to acquire the lock for this component's state.
        }

        let (meta_client_chan, mut server_chan) =
            zx::Channel::create().expect("failed to create channel");
        routing::route_and_open_storage_capability(
            &UseStorageDecl::Meta,
            fio::MODE_TYPE_DIRECTORY,
            self,
            &mut server_chan,
            bind_reason,
        )
        .await?;
        let meta_dir = Arc::new(DirectoryProxy::from_channel(
            fasync::Channel::from_channel(meta_client_chan).unwrap(),
        ));

        let mut state = self.lock_state().await;
        let state = state.as_mut().expect("resolve_meta_dir: not resolved");
        state.meta_dir = Some(meta_dir.clone());
        Ok(Some(meta_dir))
    }

    /// Resolves a runner for this component.
    //
    // We use an explicit `BoxFuture` here instead of a standard async
    // function because we may need to recurse to resolve the runner:
    //
    //   resolve_runner -> route_use_capability -> bind -> resolve_runner
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
                state.as_ref().expect("resolve_runner: not resolved").decl().clone()
            };

            // Find any explicit "use" runner declaration, resolve that.
            let runner_decl = decl.get_used_runner();
            if let Some(runner_decl) = runner_decl {
                // Open up a channel to the runner.
                let (client_channel, server_channel) =
                    create_endpoints::<fcrunner::ComponentRunnerMarker>()
                        .map_err(|_| ModelError::InsufficientResources)?;
                let mut server_channel = server_channel.into_channel();
                routing::route_use_capability(
                    OPEN_RIGHT_READABLE,
                    MODE_TYPE_SERVICE,
                    String::new(),
                    &UseDecl::Runner(runner_decl.clone()),
                    self,
                    &mut server_channel,
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
    pub async fn add_dynamic_child(
        self: &Arc<Self>,
        collection_name: String,
        child_decl: &ChildDecl,
    ) -> Result<(), ModelError> {
        match child_decl.startup {
            fsys::StartupMode::Lazy => {}
            fsys::StartupMode::Eager => {
                return Err(ModelError::unsupported("Eager startup"));
            }
        }
        let child_realm = {
            let mut state = self.lock_resolved_state().await?;
            let collection_decl = state
                .decl()
                .find_collection(&collection_name)
                .ok_or_else(|| ModelError::collection_not_found(collection_name.clone()))?
                .clone();
            match collection_decl.durability {
                fsys::Durability::Transient => {}
                fsys::Durability::Persistent => {
                    return Err(ModelError::unsupported("Persistent durability"));
                }
            }
            if let Some(child_realm) =
                state.add_child_realm(self, child_decl, Some(&collection_decl)).await?
            {
                child_realm
            } else {
                let partial_moniker =
                    PartialMoniker::new(child_decl.name.clone(), Some(collection_name));
                return Err(ModelError::instance_already_exists(
                    self.abs_moniker.clone(),
                    partial_moniker,
                ));
            }
        };
        // Call hooks outside of lock
        let event = Event::new(&child_realm, Ok(EventPayload::Discovered));
        self.hooks.dispatch(&event).await?;
        Ok(())
    }

    /// Removes the dynamic child `partial_moniker`, returning the notification for the destroy
    /// action which the caller may await on.
    pub async fn remove_dynamic_child(
        self: &Arc<Self>,
        partial_moniker: &PartialMoniker,
    ) -> Result<Notification, ModelError> {
        let tup = {
            let state = self.lock_resolved_state().await?;
            state.live_child_realms.get(&partial_moniker).map(|t| t.clone())
        };
        if let Some(tup) = tup {
            let (instance, _) = tup;
            let child_moniker = ChildMoniker::from_partial(partial_moniker, instance);
            ActionSet::register(self.clone(), Action::MarkDeleting(child_moniker.clone()))
                .await
                .await?;
            let nf = ActionSet::register(self.clone(), Action::DeleteChild(child_moniker)).await;
            Ok(nf)
        } else {
            Err(ModelError::instance_not_found_in_realm(
                self.abs_moniker.clone(),
                partial_moniker.clone(),
            ))
        }
    }

    /// Performs the stop protocol for this component instance.
    ///
    /// Returns whether the instance was already running.
    ///
    /// REQUIRES: All dependents have already been stopped.
    pub async fn stop_instance(self: &Arc<Self>, shut_down: bool) -> Result<(), ModelError> {
        let (was_running, stop_result) = {
            let mut execution = self.lock_execution().await;
            let was_running = execution.runtime.is_some();

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
                    runtime
                        .stop_component(stop_timer, kill_timer)
                        .await
                        .map_err(|e| ModelError::RunnerCommunicationError {
                            moniker: self.abs_moniker.clone(),
                            operation: "stop".to_string(),
                            err: ClonableError::from(anyhow::Error::from(e)),
                        })?
                        .component_exit_status
                } else {
                    zx::Status::PEER_CLOSED
                }
            };

            execution.runtime = None;
            execution.shut_down |= shut_down;
            (was_running, component_stop_result)
        };
        // When the realm is stopped, any child instances in transient collections must be
        // destroyed.
        self.destroy_transient_children().await?;
        if was_running {
            let event = Event::new(self, Ok(EventPayload::Stopped { status: stop_result }));
            self.hooks.dispatch(&event).await?;
        }
        Ok(())
    }

    /// Destroys this component instance.
    /// REQUIRES: All children have already been destroyed.
    // TODO: Need to:
    // - Delete the instance's persistent marker, if it was a persistent dynamic instance
    pub async fn destroy_instance(self: &Arc<Self>) -> Result<(), ModelError> {
        // Clean up isolated storage.
        let decl = {
            let state = self.lock_state().await;
            if let Some(state) = state.as_ref() {
                state.decl.clone()
            } else {
                // The instance was never resolved and therefore never ran, it can't possibly have
                // storage to clean up.
                return Ok(());
            }
        };
        for use_ in decl.uses.iter() {
            if let UseDecl::Storage(use_storage) = use_ {
                routing::route_and_delete_storage(&use_storage, &self).await?;
                break;
            }
        }
        Ok(())
    }

    /// Registers actions to destroy all children of `realm` that live in transient collections.
    async fn destroy_transient_children(self: &Arc<Self>) -> Result<(), ModelError> {
        let (transient_colls, child_monikers) = {
            let state = self.lock_state().await;
            if state.is_none() {
                // Component instance was not resolved, so no dynamic children.
                return Ok(());
            }
            let state = state.as_ref().unwrap();
            let transient_colls: HashSet<_> = state
                .decl()
                .collections
                .iter()
                .filter_map(|c| match c.durability {
                    fsys::Durability::Transient => Some(c.name.clone()),
                    fsys::Durability::Persistent => None,
                })
                .collect();
            let child_monikers: Vec<_> =
                state.all_child_realms().keys().map(|m| m.clone()).collect();
            (transient_colls, child_monikers)
        };
        let mut futures = vec![];
        for m in child_monikers {
            // Delete a child if its collection is in the set of transient collections created
            // above.
            if let Some(coll) = m.collection() {
                if transient_colls.contains(coll) {
                    ActionSet::register(self.clone(), Action::MarkDeleting(m.clone()))
                        .await
                        .await?;
                    let nf = ActionSet::register(self.clone(), Action::DeleteChild(m)).await;
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
            ModelError::from(RoutingError::open_outgoing_failed(&self.abs_moniker, path, e))
        })?;
        Ok(())
    }

    pub async fn open_exposed(&self, server_chan: &mut zx::Channel) -> Result<(), ModelError> {
        let execution = self.lock_execution().await;
        if execution.runtime.is_none() {
            return Err(RoutingError::source_instance_stopped(&self.abs_moniker).into());
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
        let server_chan = channel::take_channel(server_chan);
        let server_end = ServerEnd::new(server_chan);
        exposed_dir.open(flags, fio::MODE_TYPE_DIRECTORY, Path::empty(), server_end);
        Ok(())
    }

    /// Binds to the component instance in this realm, starting it if it's not already running.
    /// Binds to the parent realm's component instance if it is not already bound.
    pub async fn bind(self: &Arc<Self>, reason: &BindReason) -> Result<Arc<Self>, ModelError> {
        // Push all Realms on the way to the root onto a stack.
        let mut realms = Vec::new();
        let mut current = Arc::clone(self);
        realms.push(Arc::clone(&current));
        while let Some(parent) = current.parent.as_ref().and_then(|w| w.upgrade()) {
            realms.push(Arc::clone(&parent));
            current = parent;
        }

        // Now bind to each realm starting at the root (last element).
        for realm in realms.into_iter().rev() {
            binding::bind_at(realm, reason).await?;
        }
        Ok(Arc::clone(self))
    }
}

impl std::fmt::Debug for Realm {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("Realm")
            .field("component_url", &self.component_url)
            .field("startup", &self.startup)
            .field("abs_moniker", &self.abs_moniker)
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

    /// Returns whether the realm has shut down.
    pub fn is_shut_down(&self) -> bool {
        self.shut_down
    }
}

/// The mutable state of a component.
pub struct RealmState {
    /// The component's validated declaration.
    decl: ComponentDecl,
    /// Realms of all child instances, indexed by instanced moniker.
    child_realms: HashMap<ChildMoniker, Arc<Realm>>,
    /// Realms of child instances that have not been deleted, indexed by child moniker.
    live_child_realms: HashMap<PartialMoniker, (InstanceId, Arc<Realm>)>,
    /// The component's meta directory. Evaluated on demand by the `resolve_meta_dir`
    /// getter.
    // TODO: Store this under a separate Mutex?
    meta_dir: Option<Arc<DirectoryProxy>>,
    /// The next unique identifier for a dynamic component instance created in the realm.
    /// (Static instances receive identifier 0.)
    next_dynamic_instance_id: InstanceId,
}

impl RealmState {
    pub async fn new(realm: &Arc<Realm>, decl: &ComponentDecl) -> Result<Self, ModelError> {
        let mut state = Self {
            child_realms: HashMap::new(),
            live_child_realms: HashMap::new(),
            decl: decl.clone(),
            meta_dir: None,
            next_dynamic_instance_id: 1,
        };
        state.add_static_child_realms(realm, &decl).await?;
        Ok(state)
    }

    /// Returns a reference to the component's validated declaration.
    pub fn decl(&self) -> &ComponentDecl {
        &self.decl
    }

    /// Returns an iterator over live child realms.
    pub fn live_child_realms(&self) -> impl Iterator<Item = (&PartialMoniker, &Arc<Realm>)> {
        self.live_child_realms.iter().map(|(k, v)| (k, &v.1))
    }

    /// Returns a reference to a live child.
    pub fn get_live_child_realm(&self, m: &PartialMoniker) -> Option<Arc<Realm>> {
        self.live_child_realms.get(m).map(|(_, v)| v.clone())
    }

    /// Return all child realms that match the `PartialMoniker` regardless of
    /// whether that child is live.
    pub fn get_all_child_realms_by_name(&self, m: &PartialMoniker) -> Vec<Arc<Realm>> {
        self.child_realms
            .iter()
            .filter(|(child, _)| m.name() == child.name() && m.collection() == child.collection())
            .map(|(_, realm)| realm.clone())
            .collect()
    }

    /// Returns a live child's instance id.
    pub fn get_live_child_instance_id(&self, m: &PartialMoniker) -> Option<InstanceId> {
        self.live_child_realms.get(m).map(|(i, _)| *i)
    }

    /// Given a `PartialMoniker` returns the `ChildMoniker`
    pub fn get_live_child_moniker(&self, m: &PartialMoniker) -> Option<ChildMoniker> {
        self.live_child_realms.get(m).map(|(i, _)| ChildMoniker::from_partial(m, *i))
    }

    pub fn get_all_child_monikers(&self, m: &PartialMoniker) -> Vec<ChildMoniker> {
        self.child_realms
            .iter()
            .filter(|(child, _)| m.name() == child.name() && m.collection() == child.collection())
            .map(|(child, _)| child.clone())
            .collect()
    }

    /// Returns a reference to the list of all child realms.
    pub fn all_child_realms(&self) -> &HashMap<ChildMoniker, Arc<Realm>> {
        &self.child_realms
    }

    /// Returns a child `Realm`. The child may or may not be live.
    pub fn get_child_instance(&self, cm: &ChildMoniker) -> Option<Arc<Realm>> {
        self.child_realms.get(cm).map(|i| i.clone())
    }

    /// Extends an absolute moniker with the live child with partial moniker `p`. Returns `None`
    /// if no matching child was found.
    pub fn extend_moniker_with(
        &self,
        moniker: &AbsoluteMoniker,
        partial: &PartialMoniker,
    ) -> Option<AbsoluteMoniker> {
        match self.get_live_child_instance_id(partial) {
            Some(instance_id) => {
                Some(moniker.child(ChildMoniker::from_partial(partial, instance_id)))
            }
            None => None,
        }
    }

    /// Returns all deleting child realms.
    pub fn get_deleting_child_realms(&self) -> HashMap<ChildMoniker, Arc<Realm>> {
        let mut deleting_realms = HashMap::new();
        for (m, r) in self.all_child_realms().iter() {
            if self.get_live_child_realm(&m.to_partial()).is_none() {
                deleting_realms.insert(m.clone(), r.clone());
            }
        }
        deleting_realms
    }

    /// Marks a live child realm deleting. No-op if the child is already deleting.
    pub fn mark_child_realm_deleting(&mut self, partial_moniker: &PartialMoniker) {
        self.live_child_realms.remove(&partial_moniker);
    }

    /// Removes a child realm.
    pub fn remove_child_realm(&mut self, moniker: &ChildMoniker) {
        self.child_realms.remove(moniker);
    }

    /// Construct an environment for `child`, inheriting from `realm`'s environment if
    /// necessary.
    fn environment_for_child(
        &self,
        realm: &Arc<Realm>,
        child: &ChildDecl,
        collection: Option<&CollectionDecl>,
    ) -> Result<Environment, ModelError> {
        // For instances in a collection, the environment (if any) is designated in the collection.
        // Otherwise, it's specified in the ChildDecl.
        let environment_name = match collection {
            Some(c) => c.environment.as_ref(),
            None => child.environment.as_ref(),
        };
        if let Some(environment_name) = environment_name {
            // The child has an environment assigned to it. Find that environment
            // in the list of environment declarations.
            let decl = self
                .decl
                .environments
                .iter()
                .find(|env| env.name == *environment_name)
                .ok_or_else(|| {
                    ModelError::environment_not_found(environment_name, realm.abs_moniker.clone())
                })?;
            Environment::from_decl(realm, decl).map_err(|e| ModelError::EnvironmentInvalid {
                name: environment_name.to_string(),
                moniker: realm.abs_moniker.clone(),
                err: e,
            })
        } else {
            // Auto-inherit the environment from this realm.
            Ok(Environment::new_inheriting(realm))
        }
    }

    /// Adds a new child of this realm for the given `ChildDecl`. Returns the child realm,
    /// or None if it already existed.
    async fn add_child_realm(
        &mut self,
        realm: &Arc<Realm>,
        child: &ChildDecl,
        collection: Option<&CollectionDecl>,
    ) -> Result<Option<Arc<Realm>>, ModelError> {
        let instance_id = match collection {
            Some(_) => {
                let id = self.next_dynamic_instance_id;
                self.next_dynamic_instance_id += 1;
                id
            }
            None => 0,
        };
        let child_moniker =
            ChildMoniker::new(child.name.clone(), collection.map(|c| c.name.clone()), instance_id);
        let partial_moniker = child_moniker.to_partial();
        if self.get_live_child_realm(&partial_moniker).is_none() {
            let child_realm = Arc::new(Realm {
                environment: self.environment_for_child(realm, child, collection.clone())?,
                abs_moniker: realm.abs_moniker.child(child_moniker.clone()),
                component_url: child.url.clone(),
                startup: child.startup,
                parent: Some(Arc::downgrade(realm)),
                state: Mutex::new(None),
                execution: Mutex::new(ExecutionState::new()),
                actions: Mutex::new(ActionSet::new()),
                hooks: Arc::new(Hooks::new(Some(realm.hooks.clone()))),
            });
            self.child_realms.insert(child_moniker, child_realm.clone());
            self.live_child_realms.insert(partial_moniker, (instance_id, child_realm.clone()));
            Ok(Some(child_realm))
        } else {
            Ok(None)
        }
    }

    async fn add_static_child_realms(
        &mut self,
        realm: &Arc<Realm>,
        decl: &ComponentDecl,
    ) -> Result<(), ModelError> {
        for child in decl.children.iter() {
            self.add_child_realm(realm, child, None).await?;
        }
        Ok(())
    }
}

/// The execution state for a component instance that has started running.
pub struct Runtime {
    /// The resolved component URL returned by the resolver.
    pub resolved_url: String,

    /// Holder for objects related to the component's incoming namespace.
    pub namespace: Option<IncomingNamespace>,

    /// A client handle to the component instance's outgoing directory.
    pub outgoing_dir: Option<DirectoryProxy>,

    /// A client handle to the component instance's runtime directory hosted by the runner.
    pub runtime_dir: Option<DirectoryProxy>,

    /// Hosts a directory mapping the component's exposed capabilities.
    pub exposed_dir: ExposedDir,

    /// Used to interact with the Runner to influence the component's execution.
    pub controller: Option<fcrunner::ComponentControllerProxy>,

    /// Approximates when the component was started.
    pub timestamp: zx::Time,

    /// Allows the spawned background context, which is watching for the
    /// controller channel to close, to be aborted when the `Runtime` is
    /// dropped.
    exit_listener: Option<AbortHandle>,
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
        resolved_url: String,
        namespace: Option<IncomingNamespace>,
        outgoing_dir: Option<DirectoryProxy>,
        runtime_dir: Option<DirectoryProxy>,
        exposed_dir: ExposedDir,
        controller: Option<fcrunner::ComponentControllerProxy>,
    ) -> Result<Self, ModelError> {
        let timestamp = zx::Time::get(zx::ClockId::Monotonic);
        Ok(Runtime {
            resolved_url,
            namespace,
            outgoing_dir,
            runtime_dir,
            exposed_dir,
            controller,
            timestamp,
            exit_listener: None,
        })
    }

    /// If the Runtime has a controller this creates a background context which
    /// watches for the controller's channel to close. If the channel closes,
    /// the background context attempts to use the WeakRealm to stop the
    /// component.
    pub fn watch_for_exit(&mut self, realm: WeakRealm) {
        if let Some(controller) = &self.controller {
            let controller_clone = controller.clone();
            let (abort_client, abort_server) = AbortHandle::new_pair();
            let watcher = Abortable::new(
                async move {
                    if let Ok(_) = fasync::OnSignals::new(
                        &controller_clone.as_handle_ref(),
                        zx::Signals::CHANNEL_PEER_CLOSED,
                    )
                    .await
                    {
                        if let Ok(realm) = realm.upgrade() {
                            let _ = ActionSet::register(realm, Action::Stop).await.await;
                        }
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
            let controller_ref = controller.as_ref();
            fasync::OnSignals::new(
                &controller_ref.as_handle_ref(),
                zx::Signals::CHANNEL_PEER_CLOSED,
            )
            .await
            .expect("failed waiting for channel to close");
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
        if let Some(controller) = self.controller.as_ref() {
            stop_component_internal(controller, stop_timer, kill_timer).await
        } else {
            // TODO(jmatt) Need test coverage
            Ok(ComponentStopOutcome {
                request: StopRequestSuccess::NoController,
                component_exit_status: zx::Status::OK,
            })
        }
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
    controller: &fcrunner::ComponentControllerProxy,
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
            component_exit_status: wait_for_epitaph(controller).await,
        }),
        Err(e) => Err(e),
    }
}

async fn do_runner_stop<'a>(
    controller: &fcrunner::ComponentControllerProxy,
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
        fasync::OnSignals::new(&controller.as_handle_ref(), zx::Signals::CHANNEL_PEER_CLOSED)
            .await
            .expect("failed waiting for channel to close");
    });
    // Wait for either the timer to fire or the channel to close
    match futures::future::select(stop_timer, channel_close).await {
        Either::Left(((), _channel_close)) => None,
        Either::Right((_timer, _close_result)) => Some(Ok(StopRequestSuccess::Stopped)),
    }
}

async fn do_runner_kill<'a>(
    controller: &fcrunner::ComponentControllerProxy,
    kill_timer: BoxFuture<'a, ()>,
) -> Result<StopRequestSuccess, StopComponentError> {
    match controller.kill() {
        Ok(()) => {
            // Wait for the controller to close the channel
            let channel_close = Box::pin(async move {
                fasync::OnSignals::new(
                    &controller.as_handle_ref(),
                    zx::Signals::CHANNEL_PEER_CLOSED,
                )
                .await
                .expect("error waiting for channel to close");
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

/// Watch the event stream and wait for an epitaph. A message which is not an
/// epitaph is discarded. If any error is received besides the error associated
/// with an epitaph, this function returns zx::Status::PEER_CLOSED.
async fn wait_for_epitaph(controller: &fcrunner::ComponentControllerProxy) -> zx::Status {
    loop {
        match controller.take_event_stream().next().await {
            Some(Err(fidl::Error::ClientChannelClosed { status, .. })) => return status,
            Some(Err(_)) | None => return zx::Status::PEER_CLOSED,
            Some(Ok(event)) => {
                // Some other message was received
                warn!("Received unexpected event waiting for component stop: {:?}", event);
                continue;
            }
        }
    }
}

#[cfg(test)]
pub mod tests {
    use {
        super::*,
        crate::model::{
            binding::Binder,
            events::{event::SyncMode, stream::EventStream},
            hooks::{EventErrorPayload, EventType},
            rights::READ_RIGHTS,
            testing::{
                mocks::{ControlMessage, ControllerActionResponse, MockController},
                routing_test_helpers::RoutingTest,
                test_helpers::{
                    self, component_decl_with_test_runner, ActionsTest, ComponentDeclBuilder,
                    ComponentInfo,
                },
            },
        },
        fidl::endpoints,
        fuchsia_async as fasync,
        fuchsia_zircon::{self as zx, AsHandleRef, Koid},
        futures::lock::Mutex,
        matches::assert_matches,
        std::{boxed::Box, collections::HashMap, sync::Arc, task::Poll},
    };

    #[fuchsia_async::run_singlethreaded(test)]
    /// Test scenario where we tell the controller to stop the component and
    /// the component stops immediately.
    async fn stop_component_well_behaved_component_stop() {
        // Create a mock controller which simulates immediately shutting down
        // the component.
        let stop_timeout = zx::Duration::from_millis(5);
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
        let controller = MockController::new(server, requests.clone(), server_channel_koid);
        controller.serve();

        let stop_timer = Box::pin(fasync::Timer::new(fasync::Time::after(stop_timeout)));
        let kill_timer = Box::pin(async move {
            let timer = fasync::Timer::new(fasync::Time::after(kill_timeout));
            timer.await;
        });
        let client_proxy = client.into_proxy().expect("failed to convert client to proxy");
        match stop_component_internal(&client_proxy, stop_timer, kill_timer).await {
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

    #[fuchsia_async::run_singlethreaded(test)]
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

        // Drop the server end so it closes
        drop(server);
        match stop_component_internal(&client_proxy, stop_timer, kill_timer).await {
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
        let mut exec = fasync::Executor::new_with_fake_time().unwrap();

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
        let mut stop_future =
            Box::pin(stop_component_internal(&client_proxy, stop_timer, kill_timer));

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
        let mut exec = fasync::Executor::new_with_fake_time().unwrap();

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
        let mut stop_fut = Box::pin(stop_component_internal(&client_proxy, stop_timer, kill_timer));

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
            fasync::OnSignals::new(&client_proxy.as_handle_ref(), zx::Signals::CHANNEL_PEER_CLOSED)
                .await
                .expect("failed waiting for channel to close");

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
        let mut exec = fasync::Executor::new_with_fake_time().unwrap();

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
        let mut stop_fut = Box::pin(stop_component_internal(&client_proxy, stop_timer, kill_timer));

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
        let mut exec = fasync::Executor::new_with_fake_time().unwrap();

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
        let mut stop_fut = Box::pin(stop_component_internal(&client_proxy, stop_timer, kill_timer));

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
            fasync::OnSignals::new(&client_proxy.as_handle_ref(), zx::Signals::CHANNEL_PEER_CLOSED)
                .await
                .expect("failed waiting for channel to close");

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

    // The "exposed dir" of a component is hosted by component manager on behalf of
    // a running component. This test makes sure that when a component is stopped,
    // the exposed dir is no longer being served.
    #[fasync::run_singlethreaded(test)]
    async fn stop_component_closes_exposed_dir() {
        let test = RoutingTest::new(
            "root",
            vec![(
                "root",
                ComponentDeclBuilder::new()
                    .expose(cm_rust::ExposeDecl::Protocol(cm_rust::ExposeProtocolDecl {
                        source: cm_rust::ExposeSource::Self_,
                        source_path: "/svc/foo".try_into().expect("bad cap path"),
                        target: cm_rust::ExposeTarget::Parent,
                        target_path: "/svc/foo".try_into().expect("bad cap path"),
                    }))
                    .build(),
            )],
        )
        .await;
        let realm =
            test.model.bind(&vec![].into(), &BindReason::Root).await.expect("failed to bind");
        let (node_proxy, server_end) =
            fidl::endpoints::create_proxy::<fio::NodeMarker>().expect("failed to create endpoints");
        let mut server_end = server_end.into_channel();
        realm.open_exposed(&mut server_end).await.expect("failed to open exposed dir");

        // Ensure that the directory is open to begin with.
        let proxy = DirectoryProxy::new(node_proxy.into_channel().unwrap());
        assert!(test_helpers::dir_contains(&proxy, "svc", "foo").await);

        realm.stop_instance(false).await.expect("failed to stop instance");

        // The directory should have received a PEER_CLOSED signal.
        fasync::OnSignals::new(&proxy.as_handle_ref(), zx::Signals::CHANNEL_PEER_CLOSED)
            .await
            .expect("failed waiting for channel to close");
    }

    #[fasync::run_singlethreaded(test)]
    async fn notify_capability_ready() {
        let test = RoutingTest::new(
            "root",
            vec![(
                "root",
                ComponentDeclBuilder::new()
                    .expose(cm_rust::ExposeDecl::Directory(cm_rust::ExposeDirectoryDecl {
                        source: cm_rust::ExposeSource::Self_,
                        source_path: "/diagnostics".try_into().expect("bad cap path"),
                        target: cm_rust::ExposeTarget::Framework,
                        target_path: "/diagnostics".try_into().expect("bad cap path"),
                        rights: Some(*READ_RIGHTS),
                        subdir: None,
                    }))
                    .build(),
            )],
        )
        .await;

        let mut event_source = test
            .builtin_environment
            .event_source_factory
            .create_for_debug(SyncMode::Sync)
            .await
            .expect("create event source");
        let mut event_stream = event_source
            .subscribe(vec![EventType::CapabilityReady.into()])
            .await
            .expect("subscribe to event stream");
        event_source.start_component_tree().await;

        let _realm =
            test.model.bind(&vec![].into(), &BindReason::Root).await.expect("failed to bind");
        let event =
            event_stream.wait_until(EventType::CapabilityReady, vec![].into()).await.unwrap().event;

        assert_eq!(event.target_moniker, AbsoluteMoniker::root());
        assert_matches!(event.result,
                        Err(EventError {
                            event_error_payload:
                                EventErrorPayload::CapabilityReady { path, .. }, .. }) if path == "/diagnostics");
    }

    #[fasync::run_singlethreaded(test)]
    async fn started_and_running_vent_timestamp_matches_realm() {
        let test =
            RoutingTest::new("root", vec![("root", ComponentDeclBuilder::new().build())]).await;

        let mut event_source = test
            .builtin_environment
            .event_source_factory
            .create_for_debug(SyncMode::Sync)
            .await
            .expect("create event source");
        let mut event_stream = event_source
            .subscribe(vec![
                EventType::Discovered.into(),
                EventType::Resolved.into(),
                EventType::Started.into(),
            ])
            .await
            .expect("subscribe to event stream");
        event_source.start_component_tree().await;

        let model = test.model.clone();
        let (f, bind_handle) = async move {
            model.bind(&vec![].into(), &BindReason::Root).await.expect("failed to bind")
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

        let realm = bind_handle.await;
        let realm_timestamp = realm.lock_execution().await.runtime.as_ref().unwrap().timestamp;
        assert_eq!(realm_timestamp, started_timestamp);

        let mut event_stream = event_source
            .subscribe(vec![EventType::Running.into()])
            .await
            .expect("subscribe to event stream");
        let event = event_stream.wait_until(EventType::Running, vec![].into()).await.unwrap().event;
        assert_matches!(
            event.result,
            Ok(EventPayload::Running { started_timestamp: timestamp })
            if timestamp == started_timestamp);
        assert!(event.timestamp > started_timestamp);
    }

    #[fasync::run_singlethreaded(test)]
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
            .event_source_factory
            .create_for_debug(SyncMode::Async)
            .await
            .expect("failed creating event source");
        let mut stop_event_stream = event_source
            .subscribe(vec![EventType::Stopped.into()])
            .await
            .expect("couldn't susbscribe to event stream");

        event_source.start_component_tree().await;
        let a_moniker: AbsoluteMoniker = vec!["a:0"].into();
        let b_moniker: AbsoluteMoniker = vec!["a:0", "b:0"].into();

        let realm_b = test.look_up(b_moniker.clone()).await;

        // Bind to the root so it and its eager children start
        let _root = test
            .model
            .bind(&vec![].into(), &BindReason::Root)
            .await
            .expect("failed to bind to root realm");

        // Check that the eagerly-started 'b' has a runtime, which indicates
        // it is running.
        assert!(realm_b.lock_execution().await.runtime.is_some());

        let b_info = ComponentInfo::new(realm_b.clone()).await;
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
        assert_eq!(stop_event.target_moniker, b_moniker.clone());

        // Verify that a parent of the exited component can still be stopped
        // properly.
        ActionSet::register(test.look_up(a_moniker.clone()).await, Action::Shutdown)
            .await
            .await
            .expect("Couldn't trigger shutdown");
        // Check that we get a stop even which corresponds to the parent.
        let parent_stop = stop_event_stream
            .wait_until(EventType::Stopped, a_moniker.clone())
            .await
            .unwrap()
            .event;
        assert_eq!(parent_stop.target_moniker, a_moniker.clone());
    }

    async fn wait_until_event_get_timestamp(
        event_stream: &mut EventStream,
        event_type: EventType,
    ) -> zx::Time {
        event_stream.wait_until(event_type, vec![].into()).await.unwrap().event.timestamp.clone()
    }
}

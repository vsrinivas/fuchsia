// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{
        actions::{Action, ActionSet, Notification},
        error::ModelError,
        exposed_dir::ExposedDir,
        hooks::{Event, EventPayload, Hooks},
        model::Model,
        moniker::{AbsoluteMoniker, ChildMoniker, InstanceId, PartialMoniker},
        namespace::IncomingNamespace,
        resolver::{Resolver, ResolverRegistry},
        routing,
        runner::{NullRunner, RemoteRunner, Runner},
    },
    anyhow::format_err,
    clonable_error::ClonableError,
    cm_rust::{self, ChildDecl, ComponentDecl, UseDecl, UseStorageDecl},
    fidl::endpoints::{create_endpoints, Proxy, ServerEnd},
    fidl_fuchsia_io::{self as fio, DirectoryProxy, MODE_TYPE_DIRECTORY},
    fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync,
    fuchsia_vfs_pseudo_fs_mt::{
        directory::entry::DirectoryEntry, execution_scope::ExecutionScope, path::Path,
    },
    fuchsia_zircon::{self as zx, AsHandleRef},
    futures::{
        future::{BoxFuture, Either, FutureExt},
        lock::{Mutex, MutexLockFuture},
    },
    std::convert::TryInto,
    std::iter::Iterator,
    std::{
        boxed::Box,
        clone::Clone,
        collections::{HashMap, HashSet},
        fmt, i64,
        path::PathBuf,
        sync::{Arc, Weak},
    },
};

/// A realm is a container for an individual component instance and its children.  It is provided
/// by the parent of the instance or by the component manager itself in the case of the root realm.
///
/// The realm's properties influence the runtime behavior of the subtree of component instances
/// that it contains, including component resolution, execution, and service discovery.
pub struct Realm {
    /// The registry for resolving component URLs within the realm.
    pub resolver_registry: Arc<ResolverRegistry>,
    /// The component's URL.
    pub component_url: String,
    /// The mode of startup (lazy or eager).
    pub startup: fsys::StartupMode,
    /// The parent, a.k.a. containing realm. `None` for the root realm.
    parent: Option<Weak<Realm>>,
    /// The absolute moniker of this realm.
    pub abs_moniker: AbsoluteMoniker,
    /// The hooks scoped to this realm.
    pub hooks: Hooks,

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
    pub fn new_root_realm(resolver_registry: ResolverRegistry, component_url: String) -> Self {
        Self {
            resolver_registry: Arc::new(resolver_registry),
            abs_moniker: AbsoluteMoniker::root(),
            component_url,
            // Started by main().
            startup: fsys::StartupMode::Lazy,
            parent: None,
            state: Mutex::new(None),
            execution: Mutex::new(ExecutionState::new()),
            actions: Mutex::new(ActionSet::new()),
            hooks: Hooks::new(None),
        }
    }

    /// Locks and returns the realm's mutable state.
    pub fn lock_state(&self) -> MutexLockFuture<Option<RealmState>> {
        self.state.lock()
    }

    /// Locks and returns the realm's execution state.
    pub fn lock_execution(&self) -> MutexLockFuture<ExecutionState> {
        self.execution.lock()
    }

    /// Locks and returns the realm's action set.
    pub fn lock_actions(&self) -> MutexLockFuture<ActionSet> {
        self.actions.lock()
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

    /// Resolves and populates the component declaration of this realm's Instance, if not already
    /// populated.
    pub async fn resolve_decl(realm: &Arc<Self>) -> Result<(), ModelError> {
        // Call `resolve()` outside of lock.
        let is_resolved = { realm.lock_state().await.is_some() };
        if !is_resolved {
            let component = realm.resolver_registry.resolve(&realm.component_url).await?;
            Realm::populate_decl(realm, component.decl.ok_or(ModelError::ComponentInvalid)?)
                .await?;
        }
        Ok(())
    }

    /// Populates the component declaration of this realm's Instance using the provided
    /// `component_decl  if not already populated.
    pub async fn populate_decl(
        realm: &Arc<Self>,
        component_decl: fsys::ComponentDecl,
    ) -> Result<(), ModelError> {
        let decl: ComponentDecl = component_decl
            .try_into()
            .map_err(|e| ModelError::manifest_invalid(realm.component_url.clone(), e))?;
        let new_realm_state = {
            let mut state = realm.lock_state().await;
            if state.is_none() {
                *state = Some(RealmState::new(realm, &decl).await?);
                true
            } else {
                false
            }
        };
        // Only dispatch the `ResolveInstance` event if a `RealmState` was installed
        // in this call.
        if new_realm_state {
            let event =
                Event::new(realm.abs_moniker.clone(), EventPayload::ResolveInstance { decl });
            realm.hooks.dispatch(&event).await?;
        }
        Ok(())
    }

    /// Resolves and populates this component's meta directory handle if it has not been done so
    /// already, and returns a reference to the handle. If this component does not use meta storage
    /// Ok(None) will be returned.
    pub async fn resolve_meta_dir(
        realm: &Arc<Realm>,
        model: &Model,
    ) -> Result<Option<Arc<DirectoryProxy>>, ModelError> {
        {
            // If our meta directory has already been resolved, just return the answer.
            let state = realm.lock_state().await;
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

        let (meta_client_chan, server_chan) =
            zx::Channel::create().expect("failed to create channel");

        routing::route_and_open_storage_capability(
            &model,
            &UseStorageDecl::Meta,
            MODE_TYPE_DIRECTORY,
            realm,
            server_chan,
        )
        .await?;
        let meta_dir = Arc::new(DirectoryProxy::from_channel(
            fasync::Channel::from_channel(meta_client_chan).unwrap(),
        ));

        let mut state = realm.lock_state().await;
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
        realm: &'a Arc<Realm>,
        model: &'a Model,
    ) -> BoxFuture<'a, Result<Arc<dyn Runner + Send + Sync + 'static>, ModelError>> {
        async move {
            // Fetch component declaration.
            let decl = {
                let state = realm.lock_state().await;
                state.as_ref().expect("resolve_runner: not resolved").decl().clone()
            };

            // Find any explicit "use" runner declaration, resolve that.
            let runner_decl = decl.uses.iter().find_map(|u| match u {
                UseDecl::Runner(runner) => Some(runner.clone()),
                _ => return None,
            });
            if let Some(runner_decl) = runner_decl {
                // Open up a channel to the runner.
                let (client_channel, server_channel) =
                    create_endpoints::<fsys::ComponentRunnerMarker>()
                        .map_err(|_| ModelError::InsufficientResources)?;
                routing::route_use_capability(
                    &model,
                    /*flags=*/ 0,
                    /*open_mode=*/ 0,
                    String::new(),
                    &UseDecl::Runner(runner_decl),
                    realm,
                    server_channel.into_channel(),
                )
                .await?;

                return Ok(Arc::new(RemoteRunner::new(client_channel.into_proxy().unwrap()))
                    as Arc<dyn Runner + Send + Sync>);
            }

            // Otherwise, fall back to some defaults.
            //
            // If we have a binary defined, use the ELF loader. Otherwise, just use the
            // NullRunner.
            //
            // TODO(fxb/4761): We want all runners to be routed. This should eventually be removed.
            match decl.program {
                Some(_) => Ok(model.elf_runner.clone()),
                None => Ok(Arc::new(NullRunner {}) as Arc<dyn Runner + Send + Sync>),
            }
        }
        .boxed()
    }

    /// Adds the dynamic child defined by `child_decl` to the given `collection_name`. Once
    /// added, the component instance exists but is not bound.
    pub async fn add_dynamic_child(
        realm: &Arc<Self>,
        collection_name: String,
        child_decl: &ChildDecl,
    ) -> Result<(), ModelError> {
        match child_decl.startup {
            fsys::StartupMode::Lazy => {}
            fsys::StartupMode::Eager => {
                return Err(ModelError::unsupported("Eager startup"));
            }
        }
        Realm::resolve_decl(realm).await?;
        let child_realm = {
            let mut state = realm.lock_state().await;
            let state = state.as_mut().expect("add_dynamic_child: not resolved");
            let collection_decl = state
                .decl()
                .find_collection(&collection_name)
                .ok_or_else(|| ModelError::collection_not_found(collection_name.clone()))?;
            match collection_decl.durability {
                fsys::Durability::Transient => {}
                fsys::Durability::Persistent => {
                    return Err(ModelError::unsupported("Persistent durability"));
                }
            }
            if let Some(child_realm) =
                state.add_child_realm(realm, child_decl, Some(collection_name.clone())).await
            {
                child_realm
            } else {
                let partial_moniker =
                    PartialMoniker::new(child_decl.name.clone(), Some(collection_name));
                return Err(ModelError::instance_already_exists(
                    realm.abs_moniker.clone(),
                    partial_moniker,
                ));
            }
        };
        // Call hooks outside of lock
        let event = Event::new(
            child_realm.abs_moniker.clone(),
            EventPayload::AddDynamicChild { component_url: child_realm.component_url.clone() },
        );
        realm.hooks.dispatch(&event).await?;
        Ok(())
    }

    /// Removes the dynamic child `partial_moniker`, returning the notification for the destroy
    /// action which the caller may await on.
    pub async fn remove_dynamic_child(
        model: Arc<Model>,
        realm: Arc<Realm>,
        partial_moniker: &PartialMoniker,
    ) -> Result<Notification, ModelError> {
        Realm::resolve_decl(&realm).await?;
        if let Some(child_moniker) = realm.mark_child_deleting(&partial_moniker).await? {
            let nf =
                ActionSet::register(realm.clone(), model, Action::DeleteChild(child_moniker)).await;
            Ok(nf)
        } else {
            Err(ModelError::instance_not_found_in_realm(
                realm.abs_moniker.clone(),
                partial_moniker.clone(),
            ))
        }
    }

    /// Marks a child realm deleting, and dispatches the PreDestroyInstance event. Returns the
    /// child moniker if the child was alive.
    pub async fn mark_child_deleting(
        &self,
        partial_moniker: &PartialMoniker,
    ) -> Result<Option<ChildMoniker>, ModelError> {
        let tup = {
            let mut state = self.lock_state().await;
            let state = state.as_mut().expect("remove_dynamic_child: not resolved");
            state.live_child_realms.get(&partial_moniker).map(|t| t.clone())
        };
        if let Some(tup) = tup {
            let (instance, child_realm) = tup;
            {
                let event =
                    Event::new(child_realm.abs_moniker.clone(), EventPayload::PreDestroyInstance);
                child_realm.hooks.dispatch(&event).await?;
            }
            let mut state = self.lock_state().await;
            let state = state.as_mut().expect("remove_dynamic_child: not resolved");
            state.mark_child_realm_deleting(&partial_moniker);
            let child_moniker = ChildMoniker::from_partial(partial_moniker, instance);
            Ok(Some(child_moniker))
        } else {
            Ok(None)
        }
    }

    /// Performs the stop protocol for this component instance.
    ///
    /// Returns whether the instance was already running, and notifications to wait on for
    /// transient children to be destroyed.
    ///
    /// REQUIRES: All dependents have already been stopped.
    pub async fn stop_instance(
        model: Arc<Model>,
        realm: Arc<Realm>,
        state: Option<&mut RealmState>,
        shut_down: bool,
    ) -> Result<(bool, Vec<Notification>), ModelError> {
        let (was_running, nfs) = {
            let was_running = {
                let mut execution = realm.lock_execution().await;
                let was_running = execution.runtime.is_some();

                if let Some(runtime) = &mut execution.runtime {
                    let mut runtime = runtime.lock().await;
                    let timer = Box::pin(fasync::Timer::new(fasync::Time::after(
                        // TODO(jmatt) the plan is to read this from somewhere
                        // in the component manifest, likely a field in the
                        // environment section.
                        zx::Duration::from_nanos(i64::MAX),
                    )));
                    runtime.stop_component(timer).await.map_err(|e| {
                        ModelError::RunnerCommunicationError {
                            moniker: realm.abs_moniker.clone(),
                            operation: "stop".to_string(),
                            err: ClonableError::from(anyhow::Error::from(e)),
                        }
                    })?;
                }

                execution.runtime = None;
                execution.shut_down |= shut_down;
                was_running
            };
            let nfs = if let Some(state) = state {
                // When the realm is stopped, any child instances in transient collections must be
                // destroyed.
                Self::destroy_transient_children(model.clone(), realm.clone(), state).await?
            } else {
                vec![]
            };
            (was_running, nfs)
        };
        Ok((was_running, nfs))
    }

    /// Destroys this component instance.
    /// REQUIRES: All children have already been destroyed.
    // TODO: Need to:
    // - Delete the instance's persistent marker, if it was a persistent dynamic instance
    pub async fn destroy_instance(model: Arc<Model>, realm: Arc<Realm>) -> Result<(), ModelError> {
        // Clean up isolated storage.
        let decl = {
            let state = realm.lock_state().await;
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
                routing::route_and_delete_storage(&model, &use_storage, &realm).await?;
                break;
            }
        }
        Ok(())
    }

    /// Registers actions to destroy all children of `realm` that live in transient collections.
    /// Returns a future that completes when all those children are destroyed.
    async fn destroy_transient_children(
        model: Arc<Model>,
        realm: Arc<Realm>,
        state: &mut RealmState,
    ) -> Result<Vec<Notification>, ModelError> {
        let transient_colls: HashSet<_> = state
            .decl()
            .collections
            .iter()
            .filter_map(|c| match c.durability {
                fsys::Durability::Transient => Some(c.name.clone()),
                fsys::Durability::Persistent => None,
            })
            .collect();
        let mut futures = vec![];
        let child_monikers: Vec<_> = state.all_child_realms().keys().map(|m| m.clone()).collect();
        for m in child_monikers {
            // Delete a child if its collection is in the set of transient collections created
            // above.
            if let Some(coll) = m.collection() {
                if transient_colls.contains(coll) {
                    let partial_moniker = m.to_partial();
                    state.mark_child_realm_deleting(&partial_moniker);
                    let nf =
                        ActionSet::register(realm.clone(), model.clone(), Action::DeleteChild(m))
                            .await;
                    futures.push(nf);
                }
            }
        }
        Ok(futures)
    }

    pub async fn open_outgoing(
        &self,
        flags: u32,
        open_mode: u32,
        path: PathBuf,
        server_chan: zx::Channel,
    ) -> Result<(), ModelError> {
        let server_end = ServerEnd::new(server_chan);
        let execution = self.lock_execution().await;
        if execution.runtime.is_none() {
            return Err(ModelError::capability_discovery_error(format_err!(
                "component hosting capability isn't running: {}",
                self.abs_moniker
            )));
        }
        let runtime = execution
            .runtime
            .as_ref()
            .expect("bind_instance_open_outgoing: no runtime")
            .lock()
            .await;
        let out_dir =
            &runtime.outgoing_dir.as_ref().ok_or(ModelError::capability_discovery_error(
                format_err!("component hosting capability is non-executable: {}", self.abs_moniker),
            ))?;
        let path = path.to_str().ok_or_else(|| ModelError::path_is_not_utf8(path.clone()))?;
        let path = io_util::canonicalize_path(path);
        out_dir.open(flags, open_mode, path, server_end).map_err(|e| {
            ModelError::capability_discovery_error(format_err!(
                "failed to open outgoing dir for {}: {}",
                self.abs_moniker,
                e
            ))
        })?;
        Ok(())
    }

    pub async fn open_exposed(&self, server_chan: zx::Channel) -> Result<(), ModelError> {
        let server_end = ServerEnd::new(server_chan);
        let execution = self.lock_execution().await;
        if execution.runtime.is_none() {
            return Err(ModelError::capability_discovery_error(format_err!(
                "component hosting capability isn't running: {}",
                self.abs_moniker
            )));
        }
        let exposed_dir = &execution
            .runtime
            .as_ref()
            .expect("bind_instance_open_exposed: no runtime")
            .lock()
            .await
            .exposed_dir;

        // TODO(fxb/36541): Until directory capabilities specify rights, we always open
        // directories using OPEN_FLAG_POSIX which automatically opens the new connection using
        // the same directory rights as the parent directory connection.
        let flags = fio::OPEN_RIGHT_READABLE | fio::OPEN_FLAG_POSIX;
        exposed_dir.root_dir.clone().open(
            ExecutionScope::from_executor(Box::new(fasync::EHandle::local())),
            flags,
            fio::MODE_TYPE_DIRECTORY,
            Path::empty(),
            server_end,
        );
        Ok(())
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
    // TODO: Making this Arc<Mutex> is a bit unfortunate. It's currently necessary because
    // `EventPayload` is `Clonable`, which in particular is needed for breakpoints. Instead of
    // cloning `runtime`, we could pass the BeforeStart hook the information it needs in a separate
    // struct.
    pub runtime: Option<Arc<Mutex<Runtime>>>,
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
        state.add_static_child_realms(realm, &decl).await;
        Ok(state)
    }

    /// Returns a reference to the list of live child realms.
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

    /// Adds a new child of this realm for the given `ChildDecl`. Returns the child realm,
    /// or None if it already existed.
    async fn add_child_realm(
        &mut self,
        realm: &Arc<Realm>,
        child: &ChildDecl,
        collection: Option<String>,
    ) -> Option<Arc<Realm>> {
        let instance_id = match collection {
            Some(_) => {
                let id = self.next_dynamic_instance_id;
                self.next_dynamic_instance_id += 1;
                id
            }
            None => 0,
        };
        let child_moniker = ChildMoniker::new(child.name.clone(), collection.clone(), instance_id);
        let partial_moniker = child_moniker.to_partial();
        if self.get_live_child_realm(&partial_moniker).is_none() {
            let abs_moniker = realm.abs_moniker.child(child_moniker.clone());
            let child_realm = Arc::new(Realm {
                resolver_registry: realm.resolver_registry.clone(),
                abs_moniker: abs_moniker,
                component_url: child.url.clone(),
                startup: child.startup,
                parent: Some(Arc::downgrade(realm)),
                state: Mutex::new(None),
                execution: Mutex::new(ExecutionState::new()),
                actions: Mutex::new(ActionSet::new()),
                hooks: Hooks::new(Some(&realm.hooks)),
            });
            self.child_realms.insert(child_moniker, child_realm.clone());
            self.live_child_realms.insert(partial_moniker, (instance_id, child_realm.clone()));
            Some(child_realm)
        } else {
            None
        }
    }

    async fn add_static_child_realms(&mut self, realm: &Arc<Realm>, decl: &ComponentDecl) {
        for child in decl.children.iter() {
            self.add_child_realm(realm, child, None).await;
        }
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
    /// Used to interact with the Runner to influence the component's execution
    pub controller: Option<fsys::ComponentControllerProxy>,
}

#[derive(Debug, PartialEq)]
pub enum StopComponentSuccess {
    AlreadyStopped,
    Killed,
    NoController,
    Stopped,
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
        controller: Option<fsys::ComponentControllerProxy>,
    ) -> Result<Self, ModelError> {
        Ok(Runtime {
            resolved_url: resolved_url,
            namespace,
            outgoing_dir,
            runtime_dir,
            exposed_dir,
            controller,
        })
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
    pub async fn stop_component<'a>(
        &'a mut self,
        timer: BoxFuture<'a, ()>,
    ) -> Result<StopComponentSuccess, StopComponentError> {
        // Potentially there is no controller, perhaps because the component
        // has no running code. In this case this is a no-op.
        if let Some(controller) = self.controller.as_ref() {
            stop_component_internal(controller, timer).await
        } else {
            // TODO(jmatt) Need test coverage
            Ok(StopComponentSuccess::NoController)
        }
    }
}

async fn stop_component_internal<'a>(
    controller: &fsys::ComponentControllerProxy,
    timer: BoxFuture<'a, ()>,
) -> Result<StopComponentSuccess, StopComponentError> {
    // Ask the controller to stop the component
    match controller.stop() {
        Ok(()) => {}
        Err(e) => {
            if fidl::Error::is_closed(&e) {
                // Channel was closed already, component is considered stopped
                return Ok(StopComponentSuccess::AlreadyStopped);
            } else {
                // There was some problem sending the message, perhaps a
                // protocol error, but there isn't really a way to recover.
                return Err(StopComponentError::SendStopFailed);
            }
        }
    }

    // Now wait for the control channel to close. Put this in its own block
    // so that the future, which borrows a reference to self, gets dropped
    // before we potentially need to use self again.
    let hit_timeout = {
        let channel_close = Box::pin(async move {
            fasync::OnSignals::new(&controller.as_handle_ref(), zx::Signals::CHANNEL_PEER_CLOSED)
                .await
                .expect("failed waiting for channel to close");
        });

        // Wait for either the timer to fire or the channel to close
        match futures::future::select(timer, channel_close).await {
            Either::Left(((), _channel_close)) => true,
            Either::Right((_timer, _close_result)) => false,
        }
    };

    if hit_timeout {
        // The timer fired, kill the component
        match controller.kill() {
            Ok(()) => Ok(StopComponentSuccess::Killed),
            Err(e) => {
                if fidl::Error::is_closed(&e) {
                    // Even though we hit the timeout, the channel is closed,
                    // so we assume stop succeeded and there was a race with
                    // the timeout
                    Ok(StopComponentSuccess::StoppedWithTimeoutRace)
                } else {
                    // There was some problem sending the message, perhaps a
                    // protocol error, but there isn't really a way to recover.
                    Err(StopComponentError::SendKillFailed)
                }
            }
        }
    } else {
        // The control channel closed so we consider the component stopped.
        Ok(StopComponentSuccess::Stopped)
    }
}

#[cfg(test)]
pub mod tests {
    use {
        super::*,
        crate::model::testing::mocks::{ControlMessage, ControllerActionResponse, MockController},
        fidl::endpoints,
        fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync,
        fuchsia_zircon::{self as zx, AsHandleRef, Koid},
        futures::lock::Mutex,
        std::{boxed::Box, collections::HashMap, sync::Arc, task::Poll},
    };

    #[fuchsia_async::run_singlethreaded(test)]
    /// Test scenario where we tell the controller to stop the component and
    /// the component stops immediately.
    async fn stop_component_well_behaved_component_stop() {
        // Create a mock controller which simulates immediately shutting down
        // the component.
        let stop_timeout = zx::Duration::from_millis(5);
        let (client, server) =
            endpoints::create_endpoints::<fsys::ComponentControllerMarker>().unwrap();
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

        let timer = Box::pin(fasync::Timer::new(fasync::Time::after(stop_timeout)));
        let client_proxy = client.into_proxy().expect("failed to convert client to proxy");
        match stop_component_internal(&client_proxy, timer).await {
            Ok(StopComponentSuccess::Stopped) => {}
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
        let (client, server) =
            endpoints::create_endpoints::<fsys::ComponentControllerMarker>().unwrap();

        let timer = Box::pin(fasync::Timer::new(fasync::Time::after(stop_timeout)));
        let client_proxy = client.into_proxy().expect("failed to convert client to proxy");

        // Drop the server end so it closes
        drop(server);
        match stop_component_internal(&client_proxy, timer).await {
            Ok(StopComponentSuccess::AlreadyStopped) => {}
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
        let (client, server) =
            endpoints::create_endpoints::<fsys::ComponentControllerMarker>().unwrap();
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
        let timer = Box::pin(fasync::Timer::new(fasync::Time::after(stop_timeout)));
        let client_proxy = client.into_proxy().expect("failed to convert client to proxy");
        let mut stop_future = Box::pin(stop_component_internal(&client_proxy, timer));

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
            Poll::Ready(Ok(StopComponentSuccess::Stopped)) => {}
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
    /// the `kill` message to the controller.
    fn stop_component_successful_with_kill_result() {
        let mut exec = fasync::Executor::new_with_fake_time().unwrap();

        // Create a controller which takes far longer than allowed to stop the
        // component.
        let stop_timeout = zx::Duration::from_seconds(5);
        let (client, server) =
            endpoints::create_endpoints::<fsys::ComponentControllerMarker>().unwrap();
        let server_channel_koid = server
            .as_handle_ref()
            .basic_info()
            .expect("failed to get basic info on server channel")
            .koid;

        // Create a request map which the MockController will fill with
        // requests it received related to mocked component.
        let requests: Arc<Mutex<HashMap<Koid, Vec<ControlMessage>>>> =
            Arc::new(Mutex::new(HashMap::new()));
        let resp_delay = zx::Duration::from_millis(stop_timeout.into_millis() / 10);
        let controller = MockController::new_with_responses(
            server,
            requests.clone(),
            server_channel_koid,
            // Process the stop message, but fail to close the channel. Channel
            // closure is the indication that a component stopped.
            ControllerActionResponse { close_channel: false, delay: Some(resp_delay) },
            ControllerActionResponse { close_channel: true, delay: Some(resp_delay) },
        );
        controller.serve();

        let timer = Box::pin(fasync::Timer::new(fasync::Time::after(stop_timeout)));
        let client_proxy = client.into_proxy().expect("failed to convert client to proxy");
        let mut stop_fut = Box::pin(stop_component_internal(&client_proxy, timer));

        // it should be the case we stall waiting for a response from the
        // controller
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut stop_fut));

        // Roll time passed the stop timeout.
        let mut new_time =
            fasync::Time::from_nanos(exec.now().into_nanos() + stop_timeout.into_nanos());
        exec.set_fake_time(new_time);
        exec.wake_expired_timers();

        // At this point stop_component() will have completed, but the
        // controller's future is not polled to completion, since it is not
        // required to complete the stop_component future.
        assert_eq!(
            Poll::Ready(Ok(StopComponentSuccess::Killed)),
            exec.run_until_stalled(&mut stop_fut)
        );

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

        // Roll time beyond the response delay so the controller closes the
        // channel.
        new_time = fasync::Time::from_nanos(exec.now().into_nanos() + resp_delay.into_nanos());
        exec.set_fake_time(new_time);
        exec.wake_expired_timers();

        // Now we expect the message check future to complete because the
        // controller should have closed the channel.
        assert_eq!(Poll::Ready(()), exec.run_until_stalled(&mut check_msgs));
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
        let (client, server) =
            endpoints::create_endpoints::<fsys::ComponentControllerMarker>().unwrap();
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

        let timer = Box::pin(fasync::Timer::new(fasync::Time::after(stop_timeout)));
        let client_proxy = client.into_proxy().expect("failed to convert client to proxy");
        let mut stop_fut = Box::pin(stop_component_internal(&client_proxy, timer));

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
            Poll::Ready(Ok(StopComponentSuccess::StoppedWithTimeoutRace)),
            exec.run_until_stalled(&mut stop_fut)
        );
    }
}

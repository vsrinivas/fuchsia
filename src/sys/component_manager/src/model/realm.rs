// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::*,
    cm_rust::{self, ChildDecl, ComponentDecl, UseDecl, UseRunnerDecl, UseStorageDecl},
    fidl::endpoints::Proxy,
    fidl_fuchsia_io::{DirectoryProxy, MODE_TYPE_DIRECTORY},
    fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::lock::{Mutex, MutexLockFuture},
    std::convert::TryInto,
    std::iter::Iterator,
    std::{
        collections::{HashMap, HashSet},
        sync::Arc,
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

    /// Resolves and populates the component declaration of this realm's Instance, if not already
    /// populated.
    pub async fn resolve_decl(&self) -> Result<(), ModelError> {
        // Call `resolve()` outside of lock.
        let is_resolved = { self.lock_state().await.is_some() };
        if !is_resolved {
            let component = self.resolver_registry.resolve(&self.component_url).await?;
            let mut state = self.lock_state().await;
            if state.is_none() {
                *state = Some(RealmState::new(self, component.decl).await?);
            }
        }
        Ok(())
    }

    /// Resolves and populates this component's meta directory handle if it has not been done so
    /// already, and returns a reference to the handle. If this component does not use meta storage
    /// Ok(None) will be returned.
    pub async fn resolve_meta_dir(
        &self,
        model: &Model,
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

        let (meta_client_chan, server_chan) =
            zx::Channel::create().expect("failed to create channel");

        routing::route_and_open_storage_capability(
            &model,
            &UseStorageDecl::Meta,
            MODE_TYPE_DIRECTORY,
            self.abs_moniker.clone(),
            server_chan,
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
    pub async fn resolve_runner(
        &self,
        model: &Model,
    ) -> Result<Arc<dyn Runner + Send + Sync + 'static>, ModelError> {
        // Fetch component declaration.
        let state = self.lock_state().await;
        let decl = state.as_ref().expect("resolve_runner: not resolved").decl();

        // Return an error if there are any explicit "use" runner declarations.
        //
        // TODO(fxb/4761): Resolve the runner component if we find one.
        for use_decl in decl.uses.iter() {
            if let UseDecl::Runner(UseRunnerDecl { source_name }) = use_decl {
                return Err(ModelError::unsupported(format!(
                    "attempted to use custom runner '{}', but they are not implemented yet",
                    source_name
                )));
            }
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

    /// Register an action on a realm.
    pub async fn register_action(
        realm: Arc<Realm>,
        model: Arc<Model>,
        action: Action,
    ) -> Result<Notification, ModelError> {
        let mut actions = realm.actions.lock().await;
        let (nf, needs_handle) = actions.register(action.clone());
        if needs_handle {
            action.handle(model, realm.clone());
        }
        Ok(nf)
    }

    /// Finish an action on a realm.
    pub async fn finish_action(realm: Arc<Realm>, action: &Action, res: Result<(), ModelError>) {
        let mut actions = realm.actions.lock().await;
        actions.finish(action, res).await
    }

    /// Adds the dynamic child defined by `child_decl` to the given `collection_name`. Once
    /// added, the component instance exists but is not bound.
    pub async fn add_dynamic_child(
        &self,
        collection_name: String,
        child_decl: &ChildDecl,
    ) -> Result<(), ModelError> {
        match child_decl.startup {
            fsys::StartupMode::Lazy => {}
            fsys::StartupMode::Eager => {
                return Err(ModelError::unsupported("Eager startup"));
            }
        }
        self.resolve_decl().await?;
        let child_realm = {
            let mut state = self.lock_state().await;
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
                state.add_child_realm(self, child_decl, Some(collection_name.clone())).await
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
        let event = Event::AddDynamicChild { realm: child_realm.clone() };
        self.hooks.dispatch(&event).await?;
        Ok(())
    }

    /// Removes the dynamic child `partial_moniker`.
    pub async fn remove_dynamic_child(
        model: Arc<Model>,
        realm: Arc<Realm>,
        partial_moniker: &PartialMoniker,
    ) -> Result<(), ModelError> {
        realm.resolve_decl().await?;
        let mut state = realm.lock_state().await;
        let state = state.as_mut().expect("remove_dynamic_child: not resolved");
        if let Some(tup) = state.live_child_realms.get(&partial_moniker).map(|t| t.clone()) {
            let (instance, _) = tup;

            state.mark_child_realm_deleting(&partial_moniker);
            let child_moniker = ChildMoniker::from_partial(partial_moniker, instance);
            let _ = Self::register_action(
                realm.clone(),
                model,
                Action::DeleteChild(child_moniker.clone()),
            )
            .await?;
            Ok(())
        } else {
            return Err(ModelError::instance_not_found_in_realm(
                realm.abs_moniker.clone(),
                partial_moniker.clone(),
            ));
        }
    }

    /// Performs the shutdown protocol for this component instance.
    ///
    /// Returns whether the instance was already running, and notifications to wait on for
    /// transient children to be destroyed.
    ///
    /// REQUIRES: All dependents have already been stopped.
    // TODO: This is a stub because currently the shutdown protocol is not implemented.
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
                route_and_delete_storage(&model, &use_storage, realm.abs_moniker.clone()).await?;
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
                        Self::register_action(realm.clone(), model.clone(), Action::DeleteChild(m))
                            .await?;
                    futures.push(nf);
                }
            }
        }
        Ok(futures)
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
    pub async fn new(realm: &Realm, decl: Option<fsys::ComponentDecl>) -> Result<Self, ModelError> {
        if decl.is_none() {
            return Err(ModelError::ComponentInvalid);
        }
        let decl: ComponentDecl = decl
            .unwrap()
            .try_into()
            .map_err(|e| ModelError::manifest_invalid(realm.component_url.clone(), e))?;
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

    /// Returns a live child's instance id.
    pub fn get_live_child_instance_id(&self, m: &PartialMoniker) -> Option<InstanceId> {
        self.live_child_realms.get(m).map(|(i, _)| *i)
    }

    /// Returns a reference to the list of all child realms.
    pub fn all_child_realms(&self) -> &HashMap<ChildMoniker, Arc<Realm>> {
        &self.child_realms
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
        realm: &Realm,
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

    async fn add_static_child_realms(&mut self, realm: &Realm, decl: &ComponentDecl) {
        for child in decl.children.iter() {
            self.add_child_realm(realm, child, None).await;
        }
    }
}

/// The execution state for a component instance that has started running.
// TODO: Hold the component instance's controller.
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
}

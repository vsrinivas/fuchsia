// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::*,
    cm_rust::{self, ChildDecl, ComponentDecl, UseDecl, UseStorageDecl},
    fidl::endpoints::Proxy,
    fidl_fuchsia_io::{DirectoryProxy, MODE_TYPE_DIRECTORY},
    fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::future::{join_all, BoxFuture, Future},
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
    /// The default runner (nominally runs ELF binaries) for executing components
    /// within the realm that do not explicitly specify a runner.
    pub default_runner: Arc<dyn Runner + Send + Sync + 'static>,
    /// The component's URL.
    pub component_url: String,
    /// The mode of startup (lazy or eager).
    pub startup: fsys::StartupMode,
    /// The absolute moniker of this realm.
    pub abs_moniker: AbsoluteMoniker,
    /// The component's mutable state.
    state: Mutex<RealmStateHolder>,
}

impl Realm {
    /// Instantiates a new root realm.
    pub fn new_root_realm(
        resolver_registry: ResolverRegistry,
        default_runner: Arc<dyn Runner + Send + Sync + 'static>,
        component_url: String,
    ) -> Self {
        Self {
            resolver_registry: Arc::new(resolver_registry),
            default_runner,
            abs_moniker: AbsoluteMoniker::root(),
            component_url,
            // Started by main().
            startup: fsys::StartupMode::Lazy,
            state: Mutex::new(RealmStateHolder::new()),
        }
    }

    /// Locks and returns the realm's mutable state.
    pub fn lock_state(&self) -> MutexLockFuture<RealmStateHolder> {
        self.state.lock()
    }

    /// Resolves and populates the component declaration of this realm's Instance, if not already
    /// populated.
    pub async fn resolve_decl(&self) -> Result<(), ModelError> {
        // Call `resolve()` outside of lock.
        let is_resolved = { self.lock_state().await.is_resolved() };
        if !is_resolved {
            let component = self.resolver_registry.resolve(&self.component_url).await?;
            let mut state = self.lock_state().await;
            if !state.is_resolved() {
                state.set(RealmState::new(self, component.decl)?);
            }
        }
        Ok(())
    }

    /// Resolves and populates this component's meta directory handle if it has not been done so
    /// already, and returns a reference to the handle. If this component does not use meta storage
    /// Ok(None) will be returned.
    pub async fn resolve_meta_dir<'a>(
        &'a self,
        model: &'a Model,
    ) -> Result<Option<Arc<DirectoryProxy>>, ModelError> {
        let meta_use = {
            let state = self.lock_state().await;
            let state = state.get();
            if state.meta_dir.is_some() {
                return Ok(Some(state.meta_dir.as_ref().unwrap().clone()));
            }
            let meta_use =
                state.decl().uses.iter().find(|u| u == &&UseDecl::Storage(UseStorageDecl::Meta));
            if meta_use.is_none() {
                return Ok(None);
            }

            meta_use.unwrap().clone()
            // Don't hold the state lock while performing routing for the meta storage capability,
            // as the routing logic may want to acquire the lock for this component's state.
        };

        let (meta_client_chan, server_chan) =
            zx::Channel::create().expect("failed to create channel");

        routing::route_and_open_storage_capability(
            &model,
            &meta_use,
            MODE_TYPE_DIRECTORY,
            self.abs_moniker.clone(),
            server_chan,
        )
        .await?;
        let meta_dir = Arc::new(DirectoryProxy::from_channel(
            fasync::Channel::from_channel(meta_client_chan).unwrap(),
        ));
        let mut state = self.lock_state().await;
        let state = state.get_mut();
        state.set_meta_dir(meta_dir.clone());
        Ok(Some(meta_dir))
    }

    /// Adds the dynamic child defined by `child_decl` to the given `collection_name`. Once
    /// added, the component instance exists but is not bound.
    pub async fn add_dynamic_child<'a>(
        &'a self,
        collection_name: String,
        child_decl: &'a ChildDecl,
        hooks: &'a Hooks,
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
            let state = state.get_mut();
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
                state.add_child_realm(self, child_decl, Some(collection_name.clone()))
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
        hooks.on_add_dynamic_child(child_realm.clone()).await?;
        Ok(())
    }

    /// Removes the dynamic child `partial_moniker`.
    pub async fn remove_dynamic_child<'a>(
        model: Model,
        realm: Arc<Realm>,
        partial_moniker: &'a PartialMoniker,
    ) -> Result<(), ModelError> {
        realm.resolve_decl().await?;
        let child_realm = {
            let mut state = realm.lock_state().await;
            let state = state.get_mut();
            let model = model.clone();
            if let Some(tup) = state.live_child_realms.get(&partial_moniker).map(|t| t.clone()) {
                let (instance, child_realm) = tup;
                let child_moniker = ChildMoniker::from_partial(partial_moniker, instance);
                let _ = state
                    .register_action(
                        model,
                        realm.clone(),
                        Action::DeleteChild(child_moniker.clone()),
                    )
                    .await?;
                child_realm
            } else {
                return Err(ModelError::instance_not_found_in_realm(
                    realm.abs_moniker.clone(),
                    partial_moniker.clone(),
                ));
            }
        };
        // Call hooks outside of lock
        model.hooks.on_remove_dynamic_child(child_realm.clone()).await?;
        Ok(())
    }

    /// Performs the shutdown protocol for this component instance.
    /// REQUIRES: All dependents have already been stopped.
    // TODO: This is a stub because currently the shutdown protocol is not implemented.
    pub async fn stop_instance(
        model: Model,
        realm: Arc<Realm>,
        shut_down: bool,
    ) -> Result<(), ModelError> {
        // When the realm is stopped, any child instances in transient collections must be
        // destroyed.
        let nf = {
            let mut state = realm.lock_state().await;
            let state = state.get_mut();
            state.execution = None;
            state.shut_down |= shut_down;
            Self::destroy_transient_children(model.clone(), realm.clone(), state).await?
        };
        nf.await.into_iter().fold(Ok(()), |acc, r| acc.and_then(|_| r))?;
        model.hooks.on_stop_instance(realm.clone()).await?;
        Ok(())
    }

    /// Destroys this component instance.
    /// REQUIRES: All children have already been destroyed.
    // TODO: This is a stub. Need to:
    // - Delete the instance's persistent marker, if it was a persistent dynamic instance
    // - Delete the instance's isolated storage
    pub async fn destroy_instance(_realm: Arc<Realm>) -> Result<(), ModelError> {
        Ok(())
    }

    /// Registers actions to destroy all children of `realm` that live in transient collections.
    /// Returns a future that completes when all those children are destroyed.
    async fn destroy_transient_children(
        model: Model,
        realm: Arc<Realm>,
        state: &mut RealmState,
    ) -> Result<impl Future<Output = Vec<Result<(), ModelError>>>, ModelError> {
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
                    let nf = state
                        .register_action(model.clone(), realm.clone(), Action::DeleteChild(m))
                        .await?;
                    futures.push(nf);
                }
            }
        }
        Ok(join_all(futures))
    }
}

/// The mutable state of a component.
pub struct RealmState {
    /// Execution state for the component instance or `None` if not running.
    execution: Option<Execution>,
    /// The component's validated declaration.
    decl: ComponentDecl,
    /// Realms of all child instances, indexed by instanced moniker.
    child_realms: HashMap<ChildMoniker, Arc<Realm>>,
    /// Realms of child instances that have not been deleted, indexed by child moniker.
    live_child_realms: HashMap<PartialMoniker, (InstanceId, Arc<Realm>)>,
    /// The component's meta directory. Evaluated on demand by the `resolve_meta_dir`
    /// getter.
    meta_dir: Option<Arc<DirectoryProxy>>,
    /// True if the component instance has shut down. This means that the component is stopped
    /// and cannot be restarted.
    shut_down: bool,
    /// Actions on the realm that must eventually be completed.
    actions: ActionSet,
    /// The next unique identifier for a dynamic component instance created in the realm.
    /// (Static instances receive identifier 0.)
    next_dynamic_instance_id: InstanceId,
}

impl RealmState {
    pub fn new(realm: &Realm, decl: Option<fsys::ComponentDecl>) -> Result<Self, ModelError> {
        if decl.is_none() {
            return Err(ModelError::ComponentInvalid);
        }
        let decl: ComponentDecl = decl
            .unwrap()
            .try_into()
            .map_err(|e| ModelError::manifest_invalid(realm.component_url.clone(), e))?;
        let mut state = Self {
            execution: None,
            child_realms: HashMap::new(),
            live_child_realms: HashMap::new(),
            decl: decl.clone(),
            meta_dir: None,
            shut_down: false,
            actions: ActionSet::new(),
            next_dynamic_instance_id: 1,
        };
        state.add_static_child_realms(realm, &decl);
        Ok(state)
    }

    /// Returns a reference to the instance's execution.
    pub fn execution(&self) -> Option<&Execution> {
        self.execution.as_ref()
    }

    /// Sets the `Execution`.
    pub fn set_execution(&mut self, e: Execution) {
        self.execution = Some(e);
    }

    /// Returns whether the realm has shut down.
    pub fn is_shut_down(&self) -> bool {
        self.shut_down
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

    /// Populates `meta_dir`.
    pub fn set_meta_dir(&mut self, meta_dir: Arc<DirectoryProxy>) {
        self.meta_dir = Some(meta_dir);
    }

    /// Adds a new child of this realm for the given `ChildDecl`. Returns the child realm,
    /// or None if it already existed.
    fn add_child_realm<'a>(
        &'a mut self,
        realm: &'a Realm,
        child: &'a ChildDecl,
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
                default_runner: realm.default_runner.clone(),
                abs_moniker: abs_moniker,
                component_url: child.url.clone(),
                startup: child.startup,
                state: Mutex::new(RealmStateHolder::new()),
            });
            self.child_realms.insert(child_moniker, child_realm.clone());
            self.live_child_realms.insert(partial_moniker, (instance_id, child_realm.clone()));
            Some(child_realm)
        } else {
            None
        }
    }

    fn add_static_child_realms<'a>(&'a mut self, realm: &'a Realm, decl: &'a ComponentDecl) {
        for child in decl.children.iter() {
            self.add_child_realm(realm, child, None);
        }
    }

    /// Register an action on this realm, rolling the action forward if necessary. Returns boxed
    /// future to allow recursive calls.
    ///
    /// REQUIRES: Component has been resolved.
    pub fn register_action<'a>(
        &'a mut self,
        model: Model,
        realm: Arc<Realm>,
        action: Action,
    ) -> BoxFuture<Result<Notification, ModelError>> {
        Box::pin(async move {
            let (nf, needs_handle) = self.actions.register(action.clone());
            if needs_handle {
                self.handle_action(model, realm.clone(), &action).await?;
            }
            Ok(nf)
        })
    }

    /// Finish an action on this realm, which will cause its notifications to be completed.
    pub async fn finish_action<'a>(&'a mut self, action: &'a Action, res: Result<(), ModelError>) {
        self.actions.finish(action, res).await;
    }
}

/// Holds a `RealmState` which may not be resolved.
pub struct RealmStateHolder {
    inner: Option<RealmState>,
}

impl RealmStateHolder {
    pub fn new() -> Self {
        Self { inner: None }
    }

    pub fn get(&self) -> &RealmState {
        self.inner.as_ref().expect("component instance was not resolved")
    }

    pub fn get_mut(&mut self) -> &mut RealmState {
        self.inner.as_mut().expect("component instance was not resolved")
    }

    pub fn set(&mut self, state: RealmState) {
        if self.inner.is_some() {
            panic!("Attempted to set RealmState twice");
        }
        self.inner = Some(state);
    }

    pub fn is_resolved(&self) -> bool {
        self.inner.is_some()
    }
}

/// The execution state for a component instance that has started running.
// TODO: Hold the component instance's controller.
pub struct Execution {
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
}

impl Execution {
    pub fn start_from(
        resolved_url: Option<String>,
        namespace: Option<IncomingNamespace>,
        outgoing_dir: Option<DirectoryProxy>,
        runtime_dir: Option<DirectoryProxy>,
        exposed_dir: ExposedDir,
    ) -> Result<Self, ModelError> {
        if resolved_url.is_none() {
            return Err(ModelError::ComponentInvalid);
        }
        let url = resolved_url.unwrap();
        Ok(Execution { resolved_url: url, namespace, outgoing_dir, runtime_dir, exposed_dir })
    }
}

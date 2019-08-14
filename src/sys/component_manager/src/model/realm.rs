// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::*,
    cm_rust::{self, ChildDecl, ComponentDecl, UseDecl, UseStorageDecl},
    fidl::endpoints::Proxy,
    fidl_fuchsia_io::{DirectoryProxy, MODE_TYPE_DIRECTORY},
    fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::lock::{Mutex, MutexLockFuture},
    std::convert::TryInto,
    std::{collections::HashMap, sync::Arc},
};

type ChildRealmMap = HashMap<ChildMoniker, Arc<Realm>>;

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
    /// The id for this realm's component instance. Used to distinguish multiple instances with the
    /// same moniker (of which at most one instance is live at a time).
    pub instance_id: u32,
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
            instance_id: 0,
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
            // Don't hold the state lock while performing routing for the meta storage capability, as
            // the routing logic may want to acquire the lock for this component's state.
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
                let child_moniker =
                    ChildMoniker::new(child_decl.name.clone(), Some(collection_name));
                return Err(ModelError::instance_already_exists(
                    self.abs_moniker.child(child_moniker),
                ));
            }
        };
        // Call hooks outside of lock
        for hook in hooks.iter() {
            hook.on_add_dynamic_child(child_realm.clone()).await?;
        }
        Ok(())
    }

    /// Removes the dynamic child `child_moniker`.
    pub async fn remove_dynamic_child<'a>(
        &'a self,
        child_moniker: &'a ChildMoniker,
        hooks: &'a Hooks,
    ) -> Result<(), ModelError> {
        self.resolve_decl().await?;
        let child_realm = {
            let mut state = self.lock_state().await;
            let state = state.get_mut();
            if let Some(child_realm) = state.child_realms.remove(&child_moniker) {
                // TODO: Register a `DeleteChild` action instead.
                state.mark_child_realm_deleting(&child_moniker);
                child_realm
            } else {
                return Err(ModelError::instance_not_found(
                    self.abs_moniker.child(child_moniker.clone()),
                ));
            }
        };
        // Call hooks outside of lock
        for hook in hooks.iter() {
            hook.on_remove_dynamic_child(child_realm.clone()).await?;
        }
        Ok(())
    }
}

/// The mutable state of a component.
pub struct RealmState {
    /// Execution state for the component instance or `None` if not running.
    execution: Option<Execution>,
    /// The component's validated declaration.
    decl: ComponentDecl,
    /// Realms of all child instances, indexed by child moniker.
    child_realms: ChildRealmMap,
    /// Realms of child instances that have not been deleted.
    live_child_realms: ChildRealmMap,
    /// The component's meta directory. Evaluated on demand by the `resolve_meta_dir`
    /// getter.
    meta_dir: Option<Arc<DirectoryProxy>>,
    /// The next unique identifier for a dynamic component instance created in the realm.
    /// (Static instances receive identifier 0.)
    next_dynamic_instance_id: u32,
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

    /// Returns a reference to the list of live child realms.
    pub fn decl(&self) -> &ComponentDecl {
        &self.decl
    }

    /// Returns a reference to the list of live child realms.
    pub fn live_child_realms(&self) -> &ChildRealmMap {
        &self.live_child_realms
    }

    /// Returns a reference to the list of all child realms.
    pub fn all_child_realms(&self) -> &ChildRealmMap {
        &self.child_realms
    }

    /// Returns all deleting child realms.
    pub fn deleting_child_realms(&self) -> ChildRealmMap {
        let mut deleting_realms = self.all_child_realms().clone();
        for m in self.live_child_realms.keys() {
            deleting_realms.remove(m);
        }
        deleting_realms
    }

    /// Marks a live child realm deleting. No-op if the child is already deleting.
    pub fn mark_child_realm_deleting(&mut self, child_moniker: &ChildMoniker) {
        self.live_child_realms.remove(&child_moniker);
    }

    /// Removes a child realm.
    ///
    /// REQUIRES: The realm is deleting.
    pub fn remove_child_realm(&mut self, child_moniker: &ChildMoniker) {
        if self.live_child_realms.contains_key(child_moniker) {
            panic!("cannot remove a live realm");
        }
        self.child_realms.remove(child_moniker);
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
        let child_moniker = ChildMoniker::new(child.name.clone(), collection.clone());
        if !self.child_realms.contains_key(&child_moniker) {
            let instance_id = if collection.is_some() {
                let id = self.next_dynamic_instance_id;
                self.next_dynamic_instance_id += 1;
                id
            } else {
                0
            };
            let abs_moniker = realm.abs_moniker.child(child_moniker.clone());
            let child_realm = Arc::new(Realm {
                resolver_registry: realm.resolver_registry.clone(),
                default_runner: realm.default_runner.clone(),
                abs_moniker: abs_moniker,
                component_url: child.url.clone(),
                startup: child.startup,
                instance_id,
                state: Mutex::new(RealmStateHolder::new()),
            });
            self.child_realms.insert(child_moniker.clone(), child_realm.clone());
            self.live_child_realms.insert(child_moniker, child_realm.clone());
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

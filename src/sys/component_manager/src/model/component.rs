// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::*,
    cm_rust::{self, ChildDecl, ComponentDecl, UseDecl, UseStorageDecl},
    fidl::endpoints::Proxy,
    fidl_fuchsia_io::{DirectoryProxy, MODE_TYPE_DIRECTORY},
    fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::lock::Mutex,
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
    /// The component's mutable state.
    pub state: Mutex<RealmState>,
    /// The id for this realm's component instance. Used to distinguish multiple instances with the
    /// same moniker (of which at most one instance is live at a time).
    pub instance_id: u32,
}

impl Realm {
    /// Resolves and populates the component declaration of this realm's Instance, if not already
    /// populated.
    pub async fn resolve_decl(&self) -> Result<(), ModelError> {
        let not_resolved = {
            let state = self.state.lock().await;
            state.decl.is_none()
        };
        if not_resolved {
            let component = self.resolver_registry.resolve(&self.component_url).await?;
            let mut state = self.state.lock().await;
            state.populate_decl(component.decl, &self).await?;
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
        let state = self.state.lock().await;
        if state.meta_dir.is_some() {
            return Ok(Some(state.meta_dir.as_ref().unwrap().clone()));
        }
        let meta_use = state.decl.as_ref().and_then(|decl| {
            decl.uses.iter().find(|u| u == &&UseDecl::Storage(UseStorageDecl::Meta))
        });
        if meta_use.is_none() {
            return Ok(None);
        }
        let meta_use = meta_use.unwrap().clone();

        // Don't hold the state lock while performing routing for the meta storage capability, as
        // the routing logic may want to acquire the lock for this component's state.
        drop(state);

        let (meta_client_chan, server_chan) =
            zx::Channel::create().expect("failed to create channel");

        routing::route_and_open_storage_capability(
            &model,
            &meta_use,
            MODE_TYPE_DIRECTORY,
            self.abs_moniker.clone(),
            server_chan,
        ).await?;
        let meta_dir = Some(Arc::new(DirectoryProxy::from_channel(
            fasync::Channel::from_channel(meta_client_chan).unwrap(),
        )));
        self.state.lock().await.meta_dir = meta_dir.clone();
        Ok(meta_dir)
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
            let mut state = self.state.lock().await;
            let collection_decl = state.get_decl()
                .find_collection(&collection_name)
                .ok_or_else(|| ModelError::collection_not_found(collection_name.clone()))?;
            match collection_decl.durability {
                fsys::Durability::Transient => {}
                fsys::Durability::Persistent => {
                    return Err(ModelError::unsupported("Persistent durability"));
                }
            }
            if let Some(child_realm) =
                state.add_child(self, child_decl, Some(collection_name.clone()))
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
            let mut state = self.state.lock().await;
            if let Some(child_realm) = state.child_realms.as_mut().unwrap().remove(&child_moniker) {
                state.deleting_child_realms.push(child_realm.clone());
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

impl RealmState {
    pub fn new() -> Self {
        Self {
            execution: None,
            child_realms: None,
            decl: None,
            meta_dir: None,
            deleting_child_realms: vec![],
            next_dynamic_instance_id: 1,
        }
    }

    /// Populates the component declaration of this realm's Instance with `decl`, if not already
    /// populated. `url` should be the URL for this component, and is used in error generation.
    pub async fn populate_decl<'a>(
        &'a mut self,
        decl: Option<fsys::ComponentDecl>,
        realm: &'a Realm,
    ) -> Result<(), ModelError> {
        if self.decl.is_none() {
            if decl.is_none() {
                return Err(ModelError::ComponentInvalid);
            }
            let decl = decl
                .unwrap()
                .try_into()
                .map_err(|e| ModelError::manifest_invalid(realm.component_url.clone(), e))?;
            self.add_static_child_realms(realm, &decl);
            self.decl = Some(decl);
        }
        Ok(())
    }

    /// Returns a reference to this realm's instance's component declaration, resolving it if
    /// necessary.
    /// REQUIRES: `populate_decl`() has been called
    pub fn get_decl(&self) -> &ComponentDecl {
        self.decl.as_ref().expect("declaration was never resolved")
    }

    /// Returns a reference to the list of child realms.
    /// REQUIRES: `populate_decl`() has been called
    pub fn get_child_realms(&self) -> &ChildRealmMap {
        self.child_realms.as_ref().expect("declaration was never resolved")
    }

    /// Adds a new child of this realm for the given `ChildDecl`. Returns the child realm,
    /// or None if it already existed.
    ///
    /// Assumes that `child_realms` is Some.
    fn add_child<'a>(
        &'a mut self,
        realm: &'a Realm,
        child: &'a ChildDecl,
        collection: Option<String>,
    ) -> Option<Arc<Realm>> {
        let child_realms = self.child_realms.as_mut().unwrap();
        let child_moniker = ChildMoniker::new(child.name.clone(), collection.clone());
        if !child_realms.contains_key(&child_moniker) {
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
                state: Mutex::new(RealmState::new()),
                instance_id,
            });
            child_realms.insert(child_moniker, child_realm.clone());
            Some(child_realm)
        } else {
            None
        }
    }

    fn add_static_child_realms<'a>(&'a mut self, realm: &'a Realm, decl: &'a ComponentDecl) {
        self.child_realms.get_or_insert(HashMap::new());
        for child in decl.children.iter() {
            self.add_child(realm, child, None);
        }
    }
}

/// The mutable state of a component.
pub struct RealmState {
    /// Execution state for the component instance or `None` if not running.
    pub execution: Option<Execution>,
    /// Realms of child instances, indexed by child moniker. Evaluated on demand.
    pub child_realms: Option<ChildRealmMap>,
    /// The component's validated declaration. Evaluated on demand.
    pub decl: Option<ComponentDecl>,
    /// The component's meta directory. Evaluated on demand by the `resolve_meta_dir`
    /// getter.
    pub meta_dir: Option<Arc<DirectoryProxy>>,
    /// Realms that still exist but are in the process of being deleted. These do not
    /// appear in `child_realms`.
    pub deleting_child_realms: Vec<Arc<Realm>>,
    /// The next unique identifier for a dynamic component instance created in the realm.
    /// (Static instances receive identifier 0.)
    next_dynamic_instance_id: u32,
}

/// The execution state for a component instance that has started running.
// TODO: Hold the component instance's controller.
pub struct Execution {
    pub resolved_url: String,
    pub namespace: Option<IncomingNamespace>,
    pub outgoing_dir: Option<DirectoryProxy>,
    pub runtime_dir: Option<DirectoryProxy>,
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

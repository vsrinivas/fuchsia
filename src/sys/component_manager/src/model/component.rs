// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::*,
    cm_rust::{self, ComponentDecl},
    fidl_fuchsia_io::DirectoryProxy,
    fidl_fuchsia_sys2 as fsys,
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
    pub default_runner: Arc<Box<dyn Runner + Send + Sync + 'static>>,
    /// The component's URL.
    pub component_url: String,
    /// The mode of startup (lazy or eager).
    pub startup: fsys::StartupMode,
    /// The absolute moniker of this realm.
    pub abs_moniker: AbsoluteMoniker,
    /// The component's mutable state.
    pub state: Mutex<RealmState>,
}

impl Realm {
    /// Resolves and populates the component declaration of this realm's Instance, if not already
    /// populated.
    pub async fn resolve_decl(&self) -> Result<(), ModelError> {
        let mut state = await!(self.state.lock());
        if state.decl.is_none() {
            let component = await!(self.resolver_registry.resolve(&self.component_url))?;
            state.populate_decl(component.decl, &self)?;
        }
        Ok(())
    }

    fn make_static_child_realms(&self, decl: &ComponentDecl) -> ChildRealmMap {
        let mut child_realms = HashMap::new();
        for child in decl.children.iter() {
            let moniker = ChildMoniker::new(child.name.clone(), None);
            let abs_moniker = self.abs_moniker.child(moniker.clone());
            let realm = Arc::new(Realm {
                resolver_registry: self.resolver_registry.clone(),
                default_runner: self.default_runner.clone(),
                abs_moniker: abs_moniker,
                component_url: child.url.clone(),
                startup: child.startup,
                state: Mutex::new(RealmState { execution: None, child_realms: None, decl: None }),
            });
            child_realms.insert(moniker, realm);
        }
        child_realms
    }
}

impl RealmState {
    /// Populates the component declaration of this realm's Instance with `decl`, if not already
    /// populated. `url` should be the URL for this component, and is used in error generation.
    pub fn populate_decl(
        &mut self,
        decl: Option<fsys::ComponentDecl>,
        realm: &Realm,
    ) -> Result<(), ModelError> {
        if self.decl.is_none() {
            if decl.is_none() {
                return Err(ModelError::ComponentInvalid);
            }
            let decl = decl
                .unwrap()
                .try_into()
                .map_err(|e| ModelError::manifest_invalid(realm.component_url.clone(), e))?;
            self.child_realms = Some(realm.make_static_child_realms(&decl));
            self.decl = Some(decl);
        }
        Ok(())
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
}

/// The execution state for a component instance that has started running.
// TODO: Hold the component instance's controller.
pub struct Execution {
    pub resolved_url: String,
    pub namespace: IncomingNamespace,
    pub outgoing_dir: DirectoryProxy,
}

impl Execution {
    pub fn start_from(
        resolved_url: Option<String>,
        namespace: IncomingNamespace,
        outgoing_dir: DirectoryProxy,
    ) -> Result<Self, ModelError> {
        if resolved_url.is_none() {
            return Err(ModelError::ComponentInvalid);
        }
        let url = resolved_url.unwrap();
        Ok(Execution { resolved_url: url, namespace, outgoing_dir })
    }
}

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

/// A realm is a container for an individual component instance and its children.  It is provided
/// by the parent of the instance or by the component manager itself in the case of the root realm.
///
/// The realm's properties influence the runtime behavior of the subtree of component instances
/// that it contains, including component resolution, execution, and service discovery.
type ChildRealmMap = HashMap<ChildMoniker, Arc<Mutex<Realm>>>;
pub struct Realm {
    /// The registry for resolving component URIs within the realm.
    pub resolver_registry: Arc<ResolverRegistry>,
    /// The default runner (nominally runs ELF binaries) for executing components
    /// within the realm that do not explicitly specify a runner.
    pub default_runner: Arc<Box<dyn Runner + Send + Sync + 'static>>,
    /// The component that has been instantiated within the realm.
    pub instance: Instance,
    /// The absolute moniker of this realm.
    pub abs_moniker: AbsoluteMoniker,
}

impl Realm {
    /// Resolves and populates the component declaration of this realm's Instance, if not already
    /// populated.
    pub async fn resolve_decl(&mut self) -> Result<(), ModelError> {
        if self.instance.decl.is_none() {
            let component = await!(self.resolver_registry.resolve(&self.instance.component_uri))?;
            self.populate_decl(component.decl)?;
        }
        Ok(())
    }

    /// Populates the component declaration of this realm's Instance with `decl`, if not already
    /// populated.
    pub fn populate_decl(&mut self, decl: Option<fsys::ComponentDecl>) -> Result<(), ModelError> {
        if self.instance.decl.is_none() {
            if decl.is_none() {
                return Err(ModelError::ComponentInvalid);
            }
            let decl = decl.unwrap().try_into().map_err(|e| {
                ModelError::manifest_invalid(self.instance.component_uri.clone(), e)
            })?;
            self.instance.child_realms = Some(self.make_child_realms(&decl));
            self.instance.decl = Some(decl);
        }
        Ok(())
    }

    fn make_child_realms(&self, decl: &ComponentDecl) -> ChildRealmMap {
        let mut child_realms = HashMap::new();
        for child in decl.children.iter() {
            let moniker = ChildMoniker::new(child.name.clone());
            let abs_moniker = self.abs_moniker.child(moniker.clone());
            let realm = Arc::new(Mutex::new(Realm {
                resolver_registry: self.resolver_registry.clone(),
                default_runner: self.default_runner.clone(),
                abs_moniker: abs_moniker,
                instance: Instance {
                    component_uri: child.uri.clone(),
                    execution: None,
                    child_realms: None,
                    decl: None,
                    startup: child.startup,
                },
            }));
            child_realms.insert(moniker, realm);
        }
        child_realms
    }
}

/// An instance of a component.
pub struct Instance {
    /// The component's URI.
    pub component_uri: String,
    /// Execution state for the component instance or `None` if not running.
    pub execution: Option<Execution>,
    /// Realms of child instances, indexed by child moniker (name). Evaluated on demand.
    pub child_realms: Option<ChildRealmMap>,
    /// The component's validated declaration. Evaluated on demand.
    pub decl: Option<ComponentDecl>,
    /// The mode of startup (lazy or eager).
    pub startup: fsys::StartupMode,
}

/// The execution state for a component instance that has started running.
// TODO: Hold the component instance's controller.
pub struct Execution {
    pub resolved_uri: String,
    pub namespace: IncomingNamespace,
    pub outgoing_dir: DirectoryProxy,
}

impl Execution {
    pub fn start_from(
        resolved_uri: Option<String>,
        namespace: IncomingNamespace,
        outgoing_dir: DirectoryProxy,
    ) -> Result<Self, ModelError> {
        if resolved_uri.is_none() {
            return Err(ModelError::ComponentInvalid);
        }
        let uri = resolved_uri.unwrap();
        Ok(Execution { resolved_uri: uri, namespace, outgoing_dir })
    }
}

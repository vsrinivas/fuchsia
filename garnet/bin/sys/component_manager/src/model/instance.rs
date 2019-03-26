// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::*,
    cm_rust::{self, ComponentDecl},
    fidl_fuchsia_io::DirectoryProxy,
    fidl_fuchsia_sys2 as fsys,
    futures::lock::Mutex,
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

/// An instance of a component.
pub struct Instance {
    /// The component's URI.
    pub component_uri: String,
    /// Execution state for the component instance or `None` if not running.
    pub execution: Mutex<Option<Execution>>,
    /// Realms of child instances, indexed by child moniker (name). Evaluated on demand.
    pub child_realms: Option<ChildRealmMap>,
    /// The component's validated declaration. Evaluated on demand.
    pub decl: Option<ComponentDecl>,
    /// The mode of startup (lazy or eager).
    pub startup: fsys::StartupMode,
}

impl Instance {
    pub fn make_child_realms(
        component: &fsys::Component,
        abs_moniker: &AbsoluteMoniker,
        resolver_registry: Arc<ResolverRegistry>,
        default_runner: Arc<Box<dyn Runner + Send + Sync + 'static>>,
    ) -> Result<ChildRealmMap, ModelError> {
        let mut child_realms = HashMap::new();
        if component.decl.is_none() {
            return Err(ModelError::ComponentInvalid);
        }
        if let Some(ref children_decl) = component.decl.as_ref().unwrap().children {
            for child_decl in children_decl {
                let child_name = child_decl.name.as_ref().unwrap().clone();
                let child_uri = child_decl.uri.as_ref().unwrap().clone();
                let moniker = ChildMoniker::new(child_name);
                let abs_moniker = abs_moniker.child(moniker.clone());
                let realm = Arc::new(Mutex::new(Realm {
                    resolver_registry: resolver_registry.clone(),
                    default_runner: default_runner.clone(),
                    abs_moniker: abs_moniker,
                    instance: Instance {
                        component_uri: child_uri,
                        execution: Mutex::new(None),
                        child_realms: None,
                        decl: None,
                        startup: child_decl.startup.unwrap(),
                    },
                }));
                child_realms.insert(moniker, realm);
            }
        }
        Ok(child_realms)
    }
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

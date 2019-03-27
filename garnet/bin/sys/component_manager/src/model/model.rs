// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::data,
    crate::model::*,
    fidl::endpoints::{Proxy, ServerEnd},
    fidl_fuchsia_io::DirectoryProxy,
    fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::lock::Mutex,
    std::sync::Arc,
};

/// Parameters for initializing a component model, particularly the root of the component
/// instance tree.
pub struct ModelParams {
    /// The URI of the root component.
    pub root_component_uri: String,
    /// The component resolver registry used in the root realm.
    /// In particular, it will be used to resolve the root component itself.
    pub root_resolver_registry: ResolverRegistry,
    /// The default runner used in the root realm (nominally runs ELF binaries).
    pub root_default_runner: Box<dyn Runner + Send + Sync + 'static>,
}

/// The component model holds authoritative state about a tree of component instances, including
/// each instance's identity, lifecycle, capabilities, and topological relationships.  It also
/// provides operations for instantiating, destroying, querying, and controlling component
/// instances at runtime.
///
/// To facilitate unit testing, the component model does not directly perform IPC.  Instead, it
/// delegates external interfacing concerns to other objects that implement traits such as
/// `Runner` and `Resolver`.
#[derive(Clone)]
pub struct Model {
    pub root_realm: Arc<Mutex<Realm>>,
}

impl Model {
    /// Creates a new component model and initializes its topology.
    pub fn new(params: ModelParams) -> Model {
        Model {
            root_realm: Arc::new(Mutex::new(Realm {
                resolver_registry: Arc::new(params.root_resolver_registry),
                default_runner: Arc::new(params.root_default_runner),
                abs_moniker: AbsoluteMoniker::root(),
                instance: Instance {
                    component_uri: params.root_component_uri,
                    execution: None,
                    child_realms: None,
                    decl: None,
                    // Started by main().
                    startup: fsys::StartupMode::Lazy,
                },
            })),
        }
    }

    /// Binds to the component instance with the specified moniker, causing it to start if it is
    /// not already running. Also binds to any descendant component instances that need to be
    /// eagerly started.
    pub async fn bind_instance(&self, abs_moniker: AbsoluteMoniker) -> Result<(), ModelError> {
        let realm = await!(self.look_up_realm(&abs_moniker))?;
        // We may have to bind to multiple instances if this instance has children with the
        // "eager" startup mode.
        let mut instances_to_bind = vec![realm];
        while let Some(realm) = instances_to_bind.pop() {
            instances_to_bind.append(&mut await!(self.bind_instance_in_realm(realm))?);
        }
        Ok(())
    }

    /// Looks up a realm by absolute moniker. The component instance in the realm will be resolved
    /// if that has not already happened.
    pub async fn look_up_realm<'a>(
        &'a self,
        look_up_abs_moniker: &'a AbsoluteMoniker,
    ) -> Result<Arc<Mutex<Realm>>, ModelError> {
        let mut cur_realm = self.root_realm.clone();
        for moniker in look_up_abs_moniker.path().iter() {
            cur_realm = {
                let mut realm = await!(cur_realm.lock());
                await!(realm.resolve_decl())?;
                let child_realms = realm.instance.child_realms.as_ref().unwrap();
                if !child_realms.contains_key(&moniker) {
                    return Err(ModelError::instance_not_found(look_up_abs_moniker.clone()));
                }
                child_realms[moniker].clone()
            }
        }
        {
            let mut realm = await!(cur_realm.lock());
            await!(realm.resolve_decl())?;
        }
        Ok(cur_realm)
    }

    /// Binds to the component instance in the given realm, starting it if it's not
    /// already running. Returns the list of child realms whose instances need to be eagerly started
    /// after this function returns.
    async fn bind_instance_in_realm(
        &self,
        realm_cell: Arc<Mutex<Realm>>,
    ) -> Result<Vec<Arc<Mutex<Realm>>>, ModelError> {
        // There can only be one task manipulating an instance's execution at a time.
        let mut realm = await!(realm_cell.lock());
        let mut started = false;
        match &realm.instance.execution {
            Some(_) => {}
            None => {
                let component =
                    await!(realm.resolver_registry.resolve(&realm.instance.component_uri))?;
                realm.populate_decl(component.decl)?;
                let decl = realm.instance.decl.as_ref().unwrap();
                let (outgoing_dir_client, outgoing_dir_server) =
                    zx::Channel::create().map_err(|e| ModelError::namespace_creation_failed(e))?;
                let mut namespace = IncomingNamespace::new(component.package)?;
                let ns = await!(namespace.populate(self.clone(), &realm.abs_moniker, decl))?;
                let execution = Execution::start_from(
                    component.resolved_uri,
                    namespace,
                    DirectoryProxy::from_channel(
                        fasync::Channel::from_channel(outgoing_dir_client).unwrap(),
                    ),
                )?;

                let start_info = fsys::ComponentStartInfo {
                    resolved_uri: Some(execution.resolved_uri.clone()),
                    program: data::clone_option_dictionary(&decl.program),
                    ns: Some(ns),
                    outgoing_dir: Some(ServerEnd::new(outgoing_dir_server)),
                };
                await!(realm.default_runner.start(start_info))?;
                started = true;
                realm.instance.execution = Some(execution);
            }
        }
        // Return children that need eager starting.
        let mut eager_children = vec![];
        if started {
            for child_realm in realm.instance.child_realms.as_ref().unwrap().values() {
                let startup = await!(child_realm.lock()).instance.startup;
                match startup {
                    fsys::StartupMode::Eager => {
                        eager_children.push(child_realm.clone());
                    }
                    fsys::StartupMode::Lazy => {}
                }
            }
        }
        Ok(eager_children)
    }
}

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::*,
    cm_rust::data,
    cm_rust::CapabilityPath,
    failure::format_err,
    fidl::endpoints::{Proxy, ServerEnd},
    fidl_fuchsia_io::DirectoryProxy,
    fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::{
        future::{join_all, FutureObj},
        lock::Mutex,
    },
    io_util,
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
    pub root_realm: Arc<Realm>,
}

impl Model {
    /// Creates a new component model and initializes its topology.
    pub fn new(params: ModelParams) -> Model {
        Model {
            root_realm: Arc::new(Realm {
                resolver_registry: Arc::new(params.root_resolver_registry),
                default_runner: Arc::new(params.root_default_runner),
                abs_moniker: AbsoluteMoniker::root(),
                instance: Instance {
                    component_uri: params.root_component_uri,
                    // Started by main().
                    startup: fsys::StartupMode::Lazy,
                    state: Mutex::new(InstanceState {
                        execution: None,
                        child_realms: None,
                        decl: None,
                    }),
                },
            }),
        }
    }

    /// Binds to the component instance with the specified moniker, causing it to start if it is
    /// not already running. Also binds to any descendant component instances that need to be
    /// eagerly started.
    pub async fn look_up_and_bind_instance(
        &self,
        abs_moniker: AbsoluteMoniker,
    ) -> Result<(), ModelError> {
        let realm: Arc<Realm> = await!(self.look_up_realm(&abs_moniker))?;
        let eager_children = await!(self.bind_instance(realm.clone()))?;
        await!(self.bind_eager_children_recursive(eager_children))?;
        Ok(())
    }

    /// Given a realm and path, lazily bind to the instance in the realm, open, then bind its eager
    /// children.
    pub async fn bind_instance_and_open<'a>(
        &'a self,
        realm: Arc<Realm>,
        flags: u32,
        open_mode: u32,
        path: &'a CapabilityPath,
        server_chan: zx::Channel,
    ) -> Result<(), ModelError> {
        let eager_children = {
            let eager_children = await!(self.bind_instance(realm.clone()))?;

            let server_end = ServerEnd::new(server_chan);
            let state = await!(realm.instance.state.lock());
            let out_dir = &state
                .execution
                .as_ref()
                .ok_or(ModelError::capability_discovery_error(format_err!(
                    "component hosting capability isn't running: {}",
                    realm.abs_moniker
                )))?
                .outgoing_dir;
            let path = io_util::canonicalize_path(&path.to_string());
            out_dir.open(flags, open_mode, &path, server_end).expect("failed to send open message");
            eager_children
        };
        await!(self.bind_eager_children_recursive(eager_children))?;
        Ok(())
    }

    /// Looks up a realm by absolute moniker. The component instance in the realm will be resolved
    /// if that has not already happened.
    pub async fn look_up_realm<'a>(
        &'a self,
        look_up_abs_moniker: &'a AbsoluteMoniker,
    ) -> Result<Arc<Realm>, ModelError> {
        let mut cur_realm = self.root_realm.clone();
        for moniker in look_up_abs_moniker.path().iter() {
            cur_realm = {
                await!(cur_realm.resolve_decl())?;
                let cur_state = await!(cur_realm.instance.state.lock());
                let child_realms = cur_state.child_realms.as_ref().unwrap();
                if !child_realms.contains_key(&moniker) {
                    return Err(ModelError::instance_not_found(look_up_abs_moniker.clone()));
                }
                child_realms[moniker].clone()
            }
        }
        await!(cur_realm.resolve_decl())?;
        Ok(cur_realm)
    }

    /// Binds to the component instance in the given realm, starting it if it's not
    /// already running. Returns the list of child realms whose instances need to be eagerly started
    /// after this function returns. The caller is responsible for calling
    /// bind_eager_children_recursive themselves to ensure eager children are recursively binded.
    async fn bind_instance<'a>(&'a self, realm: Arc<Realm>) -> Result<Vec<Arc<Realm>>, ModelError> {
        // There can only be one task manipulating an instance's execution at a time.
        let mut started = false;
        let mut state = await!(realm.instance.state.lock());
        match state.execution {
            Some(_) => {}
            None => {
                let component =
                    await!(realm.resolver_registry.resolve(&realm.instance.component_uri))?;
                state.populate_decl(component.decl, &*realm)?;
                let decl = state.decl.as_ref().unwrap();
                if decl.program.is_some() {
                    let (outgoing_dir_client, outgoing_dir_server) = zx::Channel::create()
                        .map_err(|e| ModelError::namespace_creation_failed(e))?;
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
                    state.execution = Some(execution);
                }
                started = true;
            }
        }
        // Return children that need eager starting.
        let mut eager_children = vec![];
        if started {
            for child_realm in state.child_realms.as_ref().unwrap().values() {
                match child_realm.instance.startup {
                    fsys::StartupMode::Eager => {
                        eager_children.push(child_realm.clone());
                    }
                    fsys::StartupMode::Lazy => {}
                }
            }
        }
        Ok(eager_children)
    }

    /// Binds to a list of instances, and any eager children they may return.
    async fn bind_eager_children_recursive<'a>(
        &'a self,
        mut instances_to_bind: Vec<Arc<Realm>>,
    ) -> Result<(), ModelError> {
        loop {
            if instances_to_bind.is_empty() {
                break;
            }
            let futures: Vec<_> = instances_to_bind
                .iter()
                .map(|realm| {
                    FutureObj::new(Box::new(
                        async move { await!(self.bind_instance(realm.clone())) },
                    ))
                })
                .collect();
            let res = await!(join_all(futures));
            instances_to_bind.clear();
            for e in res {
                instances_to_bind.append(&mut e?);
            }
        }
        Ok(())
    }
}

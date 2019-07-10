// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::*,
    cm_rust::data,
    cm_rust::CapabilityPath,
    failure::format_err,
    fidl::endpoints::{Proxy, ServerEnd},
    fidl_fuchsia_io::{DirectoryProxy, MODE_TYPE_DIRECTORY, OPEN_RIGHT_READABLE},
    fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::{
        future::{join_all, BoxFuture, FutureObj},
        lock::Mutex,
    },
    std::sync::Arc,
};

// Hooks into the component model implement this trait.
// TODO(fsamuel): It's conceivable that as we add clients and event types,
// many clients may be interested in just a small subset of events but they'd
// have to implement all the functions in this trait. Alternatively, we can
// break down each event type into a separate trait so that clients can pick
// and choose which events they'd like to monitor.
pub trait Hook {
    // Called when a component instance is bound to the given `realm`.
    fn on_bind_instance<'a>(
        &'a self,
        realm: Arc<Realm>,
        realm_state: &'a RealmState,
        routing_facade: RoutingFacade,
    ) -> BoxFuture<Result<(), ModelError>>;

    // Called when a dynamic instance is added with `realm`.
    fn on_add_dynamic_child(&self, realm: Arc<Realm>) -> BoxFuture<Result<(), ModelError>>;
}

pub type Hooks = Vec<Arc<dyn Hook + Send + Sync + 'static>>;

/// Parameters for initializing a component model, particularly the root of the component
/// instance tree.
pub struct ModelParams {
    /// The ambient environment.
    pub ambient: Box<dyn AmbientEnvironment>,
    /// The URL of the root component.
    pub root_component_url: String,
    /// The component resolver registry used in the root realm.
    /// In particular, it will be used to resolve the root component itself.
    pub root_resolver_registry: ResolverRegistry,
    /// The default runner used in the root realm (nominally runs ELF binaries).
    pub root_default_runner: Box<dyn Runner + Send + Sync + 'static>,
    /// A set of hooks into key events of the Model.
    pub hooks: Hooks,
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
    pub ambient: Arc<dyn AmbientEnvironment>,
    pub hooks: Arc<Hooks>,
}

impl Model {
    /// Creates a new component model and initializes its topology.
    pub fn new(params: ModelParams) -> Model {
        Model {
            ambient: params.ambient.into(),
            root_realm: Arc::new(Realm {
                resolver_registry: Arc::new(params.root_resolver_registry),
                default_runner: Arc::new(params.root_default_runner),
                abs_moniker: AbsoluteMoniker::root(),
                component_url: params.root_component_url,
                // Started by main().
                startup: fsys::StartupMode::Lazy,
                state: Mutex::new(RealmState { execution: None, child_realms: None, decl: None }),
            }),
            hooks: Arc::new(params.hooks),
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
        await!(self.bind_instance(realm))
    }

    /// Binds to the component instance of the specified realm, causing it to start if it is
    /// not already running. Also binds to any descendant component instances that need to be
    /// eagerly started.
    pub async fn bind_instance(&self, realm: Arc<Realm>) -> Result<(), ModelError> {
        let eager_children = await!(self.bind_single_instance(realm))?;
        await!(self.bind_eager_children_recursive(eager_children))?;
        Ok(())
    }

    /// Given a realm and path, lazily bind to the instance in the realm, open its outgoing
    /// directory at that path, then bind its eager children.
    pub async fn bind_instance_open_outgoing<'a>(
        &'a self,
        realm: Arc<Realm>,
        flags: u32,
        open_mode: u32,
        path: &'a CapabilityPath,
        server_chan: zx::Channel,
    ) -> Result<(), ModelError> {
        let eager_children = {
            let eager_children = await!(self.bind_single_instance(realm.clone()))?;
            let server_end = ServerEnd::new(server_chan);
            let state = await!(realm.state.lock());
            let out_dir = &state
                .execution
                .as_ref()
                .ok_or(ModelError::capability_discovery_error(format_err!(
                    "component hosting capability isn't running: {}",
                    realm.abs_moniker
                )))?
                .outgoing_dir
                .as_ref()
                .ok_or(ModelError::capability_discovery_error(format_err!(
                    "component hosting capability is non-executable: {}",
                    realm.abs_moniker
                )))?;
            let path = path.to_string();
            let path = io_util::canonicalize_path(&path);
            out_dir.open(flags, open_mode, path, server_end).expect("failed to send open message");
            eager_children
        };
        await!(self.bind_eager_children_recursive(eager_children))?;
        Ok(())
    }

    /// Given a realm and path, lazily bind to the instance in the realm, open its exposed
    /// directory, then bind its eager children.
    pub async fn bind_instance_open_exposed<'a>(
        &'a self,
        realm: Arc<Realm>,
        server_chan: zx::Channel,
    ) -> Result<(), ModelError> {
        let eager_children = {
            let eager_children = await!(self.bind_single_instance(realm.clone()))?;
            let server_end = ServerEnd::new(server_chan);
            let state = await!(realm.state.lock());
            let exposed_dir = &state
                .execution
                .as_ref()
                .ok_or(ModelError::capability_discovery_error(format_err!(
                    "component hosting capability isn't running: {}",
                    realm.abs_moniker
                )))?
                .exposed_dir;
            let flags = OPEN_RIGHT_READABLE;
            await!(exposed_dir.root_dir.open(flags, MODE_TYPE_DIRECTORY, vec![], server_end))
                .expect("failed to send open message");
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
                let cur_state = await!(cur_realm.state.lock());
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
    async fn bind_single_instance<'a>(
        &'a self,
        realm: Arc<Realm>,
    ) -> Result<Vec<Arc<Realm>>, ModelError> {
        let eager_children = {
            let mut state = await!(realm.state.lock());
            let eager_children = await!(self.bind_inner(&mut *state, realm.clone()))?;
            let routing_facade = RoutingFacade::new(self.clone());
            // TODO: Don't hold the lock while calling the hooks.
            for hook in self.hooks.iter() {
                await!(hook.on_bind_instance(realm.clone(), &*state, routing_facade.clone()))?;
            }
            eager_children
        };
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
                    FutureObj::new(Box::new(async move {
                        await!(self.bind_single_instance(realm.clone()))
                    }))
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

    /// Updates the Execution struct, Populates the RealmState struct and starts the component
    /// instance, returning a binding and all child realms that must be bound because they had
    /// `eager` startup.
    async fn bind_inner<'a>(
        &'a self,
        state: &'a mut RealmState,
        realm: Arc<Realm>,
    ) -> Result<Vec<Arc<Realm>>, ModelError> {
        if let Some(_) = state.execution.as_ref() {
            // TODO: Add binding to the execution once we track bindings.
            Ok(vec![])
        } else {
            // Execution does not exist yet, create it.
            let component = await!(realm.resolver_registry.resolve(&realm.component_url))?;
            state.populate_decl(component.decl, &*realm)?;
            let decl = state.decl.as_ref().expect("ComponentDecl unavailable.");
            let exposed_dir = ExposedDir::new(self, &realm.abs_moniker, state)?;
            let execution = if decl.program.is_some() {
                let (outgoing_dir_client, outgoing_dir_server) =
                    zx::Channel::create().map_err(|e| ModelError::namespace_creation_failed(e))?;
                let (runtime_dir_client, runtime_dir_server) =
                    zx::Channel::create().map_err(|e| ModelError::namespace_creation_failed(e))?;
                let mut namespace = IncomingNamespace::new(component.package)?;
                let ns = await!(namespace.populate(self.clone(), &realm.abs_moniker, decl))?;
                let execution = Execution::start_from(
                    component.resolved_url,
                    Some(namespace),
                    Some(DirectoryProxy::from_channel(
                        fasync::Channel::from_channel(outgoing_dir_client).unwrap(),
                    )),
                    Some(DirectoryProxy::from_channel(
                        fasync::Channel::from_channel(runtime_dir_client).unwrap(),
                    )),
                    exposed_dir,
                )?;
                let start_info = fsys::ComponentStartInfo {
                    resolved_url: Some(execution.resolved_url.clone()),
                    program: data::clone_option_dictionary(&decl.program),
                    ns: Some(ns),
                    outgoing_dir: Some(ServerEnd::new(outgoing_dir_server)),
                    runtime_dir: Some(ServerEnd::new(runtime_dir_server)),
                };
                await!(realm.default_runner.start(start_info))?;
                execution
            } else {
                // Although this component has no runtime environment, it is still possible to bind
                // to it, which may trigger bindings to its children. For consistency, we still
                // consider the component to be "executing" in this case.
                Execution::start_from(component.resolved_url, None, None, None, exposed_dir)?
            };
            state.execution = Some(execution);
            let eager_child_realms: Vec<_> = state
                .child_realms
                .as_ref()
                .expect("empty child realms")
                .values()
                .filter_map(|r| match r.startup {
                    fsys::StartupMode::Eager => Some(r.clone()),
                    fsys::StartupMode::Lazy => None,
                })
                .collect();
            Ok(eager_child_realms)
        }
    }
}

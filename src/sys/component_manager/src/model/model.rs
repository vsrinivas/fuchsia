// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::*,
    cm_rust::{data, CapabilityPath},
    failure::format_err,
    fidl::endpoints::{Proxy, ServerEnd},
    fidl_fuchsia_io::{
        DirectoryProxy, MODE_TYPE_DIRECTORY, OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE,
    },
    fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::future::join_all,
    std::sync::Arc,
};

/// Parameters for initializing a component model, particularly the root of the component
/// instance tree.
pub struct ModelParams {
    /// The URL of the root component.
    pub root_component_url: String,
    /// The component resolver registry used in the root realm.
    /// In particular, it will be used to resolve the root component itself.
    pub root_resolver_registry: ResolverRegistry,
    /// The default runner used in the root realm (nominally runs ELF binaries).
    pub root_default_runner: Arc<dyn Runner + Send + Sync + 'static>,
    /// Configuration options for the model.
    pub config: ModelConfig,
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
    pub config: ModelConfig,
    pub hooks: Hooks,
}

/// Holds configuration options for the model.
#[derive(Clone)]
pub struct ModelConfig {
    /// How many children, maximum, are returned by a call to `ChildIterator.next()`.
    pub list_children_batch_size: usize,
}

impl ModelConfig {
    pub fn default() -> Self {
        ModelConfig { list_children_batch_size: 1000 }
    }

    fn validate(&self) {
        assert!(self.list_children_batch_size > 0, "list_children_batch_size is 0");
    }
}

impl Model {
    /// Creates a new component model and initializes its topology.
    pub fn new(params: ModelParams) -> Model {
        params.config.validate();
        Model {
            root_realm: Arc::new(Realm::new_root_realm(
                params.root_resolver_registry,
                params.root_default_runner,
                params.root_component_url,
            )),
            hooks: Hooks::new(),
            config: params.config,
        }
    }

    /// Binds to the component instance with the specified moniker, causing it to start if it is
    /// not already running. Also binds to any descendant component instances that need to be
    /// eagerly started.
    pub async fn look_up_and_bind_instance(
        &self,
        abs_moniker: AbsoluteMoniker,
    ) -> Result<(), ModelError> {
        let realm: Arc<Realm> = self.look_up_realm(&abs_moniker).await?;
        self.bind_instance(realm).await
    }

    /// Binds to the component instance of the specified realm, causing it to start if it is
    /// not already running. Also binds to any descendant component instances that need to be
    /// eagerly started.
    pub async fn bind_instance(&self, realm: Arc<Realm>) -> Result<(), ModelError> {
        let eager_children = self.bind_single_instance(realm).await?;
        // If the bind to this realm's instance succeeded but the child is shut down, allow
        // the call to succeed. We don't want the fact that the child is shut down to cause the
        // client to think the bind to this instance failed.
        //
        // TODO: Have a more general strategy for dealing with errors from eager binding. Should
        // we ever pass along the error?
        self.bind_eager_children_recursive(eager_children).await.or_else(|e| match e {
            ModelError::InstanceShutDown { .. } => Ok(()),
            _ => Err(e),
        })?;
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
            let eager_children = self.bind_single_instance(realm.clone()).await?;
            let server_end = ServerEnd::new(server_chan);
            let state = realm.lock_state().await;
            let state = state.get();
            let out_dir = &state
                .execution()
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
        self.bind_eager_children_recursive(eager_children).await?;
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
            let eager_children = self.bind_single_instance(realm.clone()).await?;
            let server_end = ServerEnd::new(server_chan);
            let state = realm.lock_state().await;
            let state = state.get();
            let exposed_dir = &state
                .execution()
                .ok_or(ModelError::capability_discovery_error(format_err!(
                    "component hosting capability isn't running: {}",
                    realm.abs_moniker
                )))?
                .exposed_dir;
            let flags = OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE;
            exposed_dir
                .root_dir
                .open(flags, MODE_TYPE_DIRECTORY, vec![], server_end)
                .await
                .expect("failed to send open message");
            eager_children
        };
        self.bind_eager_children_recursive(eager_children).await?;
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
                cur_realm.resolve_decl().await?;
                let cur_state = cur_realm.lock_state().await;
                let cur_state = cur_state.get();
                let child_realms = cur_state.live_child_realms();
                if !child_realms.contains_key(&moniker) {
                    return Err(ModelError::instance_not_found(look_up_abs_moniker.clone()));
                }
                child_realms[moniker].clone()
            }
        }
        cur_realm.resolve_decl().await?;
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
            let eager_children = self.bind_inner(realm.clone()).await?;
            let routing_facade = RoutingFacade::new(self.clone());
            // TODO: Don't hold the lock while calling the hooks.
            let mut state = realm.lock_state().await;
            let mut state = state.get_mut();
            self.hooks.on_bind_instance(realm.clone(), &mut state, routing_facade.clone()).await?;
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
                    Box::pin(async move { self.bind_single_instance(realm.clone()).await })
                })
                .collect();
            let res = join_all(futures).await;
            instances_to_bind.clear();
            for e in res {
                instances_to_bind.append(&mut e?);
            }
        }
        Ok(())
    }

    /// Resolves the instance if necessary, starts the component instance, updates the `Execution`,
    /// and returns a binding and all child realms that must be bound because they had `eager`
    /// startup.
    async fn bind_inner<'a>(&'a self, realm: Arc<Realm>) -> Result<Vec<Arc<Realm>>, ModelError> {
        let component = realm.resolver_registry.resolve(&realm.component_url).await?;
        let (decl, exposed_dir) = {
            let mut state = realm.lock_state().await;
            if !state.is_resolved() {
                state.set(RealmState::new(&*realm, component.decl)?);
            }
            let state = state.get();
            if state.is_shut_down() {
                return Err(ModelError::instance_shut_down(realm.abs_moniker.clone()));
            }
            if state.execution().is_some() {
                // TODO: Add binding to the execution once we track bindings.
                return Ok(vec![]);
            }
            let decl = state.decl().clone();
            let exposed_dir = ExposedDir::new(self, &realm.abs_moniker, decl.clone())?;
            (decl, exposed_dir)
        };
        let execution = if decl.program.is_some() {
            let (outgoing_dir_client, outgoing_dir_server) =
                zx::Channel::create().map_err(|e| ModelError::namespace_creation_failed(e))?;
            let (runtime_dir_client, runtime_dir_server) =
                zx::Channel::create().map_err(|e| ModelError::namespace_creation_failed(e))?;
            let mut namespace = IncomingNamespace::new(component.package)?;
            let ns = namespace.populate(self.clone(), &realm.abs_moniker, &decl).await?;
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
            realm.default_runner.start(start_info).await?;
            execution
        } else {
            // Although this component has no runtime environment, it is still possible to bind
            // to it, which may trigger bindings to its children. For consistency, we still
            // consider the component to be "executing" in this case.
            Execution::start_from(component.resolved_url, None, None, None, exposed_dir)?
        };
        let mut state = realm.lock_state().await;
        let state = state.get_mut();
        state.set_execution(execution);
        let eager_child_realms: Vec<_> = state
            .live_child_realms()
            .values()
            .filter_map(|r| match r.startup {
                fsys::StartupMode::Eager => Some(r.clone()),
                fsys::StartupMode::Lazy => None,
            })
            .collect();
        Ok(eager_child_realms)
    }
}

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        framework::*,
        model::{runner::Runner, *},
        root_realm_post_destroy_notifier::*,
        startup::BuiltinRootCapabilities,
    },
    cm_rust::{data, CapabilityPath},
    failure::format_err,
    fidl::endpoints::{Proxy, ServerEnd},
    fidl_fuchsia_io::{self as fio, DirectoryProxy},
    fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::future::join_all,
    futures::lock::Mutex,
    std::sync::Arc,
};

/// Parameters for initializing a component model, particularly the root of the component
/// instance tree.
pub struct ModelParams {
    /// Builtin services that are available in the root realm.
    pub builtin_capabilities: Arc<BuiltinRootCapabilities>,
    /// The URL of the root component.
    pub root_component_url: String,
    /// The component resolver registry used in the root realm.
    /// In particular, it will be used to resolve the root component itself.
    pub root_resolver_registry: ResolverRegistry,
    /// The built-in ELF runner, used for starting components with an ELF binary.
    pub elf_runner: Arc<dyn Runner + Send + Sync + 'static>,
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
    // TODO(xbhatnag): Move this out of Model.
    pub notifier: Arc<Mutex<Option<RootRealmPostDestroyNotifier>>>,
    pub root_realm: Arc<Realm>,
    pub config: ModelConfig,
    /// Builtin services that are available in the root realm.
    pub builtin_capabilities: Arc<BuiltinRootCapabilities>,
    pub realm_capability_host: Option<RealmCapabilityHost>,

    /// The built-in ELF runner, used for starting components with an ELF binary.
    // TODO(fxb/4761): Remove. This should be a routed capability, and
    // not explicitly passed around in the model.
    pub elf_runner: Arc<dyn Runner + Send + Sync>,
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
            notifier: Arc::new(Mutex::new(Some(RootRealmPostDestroyNotifier::new()))),
            root_realm: Arc::new(Realm::new_root_realm(
                params.root_resolver_registry,
                params.root_component_url,
            )),
            config: params.config,
            builtin_capabilities: params.builtin_capabilities,
            realm_capability_host: None,
            elf_runner: params.elf_runner,
        }
    }

    pub async fn wait_for_root_realm_destroy(&self) {
        let mut notifier = self.notifier.lock().await;
        notifier
            .take()
            .expect("A root realm can only be destroyed once")
            .wait_for_root_realm_destroy()
            .await;
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
    pub async fn bind_instance_open_outgoing(
        &self,
        realm: Arc<Realm>,
        flags: u32,
        open_mode: u32,
        path: &CapabilityPath,
        server_chan: zx::Channel,
    ) -> Result<(), ModelError> {
        let eager_children = {
            let eager_children = self.bind_single_instance(realm.clone()).await?;
            let server_end = ServerEnd::new(server_chan);
            let execution = realm.lock_execution().await;
            if execution.runtime.is_none() {
                return Err(ModelError::capability_discovery_error(format_err!(
                    "component hosting capability isn't running: {}",
                    realm.abs_moniker
                )));
            }
            let out_dir = &execution
                .runtime
                .as_ref()
                .expect("bind_instance_open_outgoing: no runtime")
                .outgoing_dir
                .as_ref()
                .ok_or(ModelError::capability_discovery_error(format_err!(
                    "component hosting capability is non-executable: {}",
                    realm.abs_moniker
                )))?;
            let path = path.to_string();
            let path = io_util::canonicalize_path(&path);
            out_dir.open(flags, open_mode, path, server_end).map_err(|e| {
                ModelError::capability_discovery_error(format_err!(
                    "failed to open outgoing dir for {}: {}",
                    realm.abs_moniker,
                    e
                ))
            })?;
            eager_children
        };
        self.bind_eager_children_recursive(eager_children).await?;
        Ok(())
    }

    /// Given a realm and path, lazily bind to the instance in the realm, open its exposed
    /// directory, then bind its eager children.
    pub async fn bind_instance_open_exposed(
        &self,
        realm: Arc<Realm>,
        server_chan: zx::Channel,
    ) -> Result<(), ModelError> {
        let eager_children = {
            let eager_children = self.bind_single_instance(realm.clone()).await?;
            let server_end = ServerEnd::new(server_chan);
            let execution = realm.lock_execution().await;
            if execution.runtime.is_none() {
                return Err(ModelError::capability_discovery_error(format_err!(
                    "component hosting capability isn't running: {}",
                    realm.abs_moniker
                )));
            }
            let exposed_dir = &execution
                .runtime
                .as_ref()
                .expect("bind_instance_open_exposed: no runtime")
                .exposed_dir;

            // TODO(fxb/36541): Until directory capabilities specify rights, we always open
            // directories using OPEN_FLAG_POSIX which automatically opens the new connection using
            // the same directory rights as the parent directory connection.
            let flags = fio::OPEN_RIGHT_READABLE | fio::OPEN_FLAG_POSIX;
            exposed_dir
                .root_dir
                .open(flags, fio::MODE_TYPE_DIRECTORY, vec![], server_end)
                .await
                .map_err(|e| {
                    ModelError::capability_discovery_error(format_err!(
                        "failed to open exposed dir for {}: {}",
                        realm.abs_moniker,
                        e
                    ))
                })?;
            eager_children
        };
        self.bind_eager_children_recursive(eager_children).await?;
        Ok(())
    }

    /// Looks up a realm by absolute moniker. The component instance in the realm will be resolved
    /// if that has not already happened.
    pub async fn look_up_realm(
        &self,
        look_up_abs_moniker: &AbsoluteMoniker,
    ) -> Result<Arc<Realm>, ModelError> {
        let mut cur_realm = self.root_realm.clone();
        for moniker in look_up_abs_moniker.path().iter() {
            cur_realm = {
                cur_realm.resolve_decl().await?;
                let cur_state = cur_realm.lock_state().await;
                let cur_state = cur_state.as_ref().expect("look_up_realm: not resolved");
                if let Some(r) = cur_state.all_child_realms().get(moniker) {
                    r.clone()
                } else {
                    return Err(ModelError::instance_not_found(look_up_abs_moniker.clone()));
                }
            };
        }
        cur_realm.resolve_decl().await?;
        Ok(cur_realm)
    }

    /// Binds to the component instance in the given realm, starting it if it's not
    /// already running. Returns the list of child realms whose instances need to be eagerly started
    /// after this function returns. The caller is responsible for calling
    /// bind_eager_children_recursive themselves to ensure eager children are recursively binded.
    async fn bind_single_instance(&self, realm: Arc<Realm>) -> Result<Vec<Arc<Realm>>, ModelError> {
        let eager_children = self.bind_inner(realm.clone()).await?;
        let event = {
            let routing_facade = RoutingFacade::new(self.clone());
            let mut state = realm.lock_state().await;
            let state = state.as_mut().expect("bind_single_instance: not resolved");
            let live_child_realms = state.live_child_realms().map(|(_, r)| r.clone()).collect();
            Event::BindInstance {
                realm: realm.clone(),
                component_decl: state.decl().clone(),
                live_child_realms,
                routing_facade,
            }
        };
        realm.hooks.dispatch(&event).await?;
        Ok(eager_children)
    }

    /// Binds to a list of instances, and any eager children they may return.
    async fn bind_eager_children_recursive(
        &self,
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
    async fn bind_inner(&self, realm: Arc<Realm>) -> Result<Vec<Arc<Realm>>, ModelError> {
        // Resolve the component from its URL.
        let component = realm.resolver_registry.resolve(&realm.component_url).await?;

        // Set up the realm's state.
        let decl = {
            let mut state = realm.lock_state().await;
            if state.is_none() {
                *state = Some(RealmState::new(&*realm, component.decl).await?);
            }
            state.as_ref().unwrap().decl().clone()
        };

        // Fetch the component's runner.
        let runner = realm.resolve_runner(self).await?;

        {
            let mut execution = realm.lock_execution().await;
            if execution.is_shut_down() {
                return Err(ModelError::instance_shut_down(realm.abs_moniker.clone()));
            }
            if execution.runtime.is_some() {
                // TODO: Add binding to the execution once we track bindings.
                return Ok(vec![]);
            }
            execution.runtime = Some(
                self.init_execution_runtime(
                    &realm.abs_moniker,
                    component.resolved_url.ok_or(ModelError::ComponentInvalid)?,
                    component.package,
                    &decl,
                    runner.as_ref(),
                )
                .await?,
            );
        }

        // Return a list of children that should be eagerly bound.
        let state = realm.lock_state().await;
        let eager_child_realms: Vec<_> = state
            .as_ref()
            .expect("bind_inner: not resolved")
            .live_child_realms()
            .filter_map(|(_, r)| match r.startup {
                fsys::StartupMode::Eager => Some(r.clone()),
                fsys::StartupMode::Lazy => None,
            })
            .collect();
        Ok(eager_child_realms)
    }

    /// Return a configured Runtime for a component.
    async fn init_execution_runtime(
        &self,
        abs_moniker: &AbsoluteMoniker,
        url: String,
        package: Option<fsys::Package>,
        decl: &cm_rust::ComponentDecl,
        runner: &(dyn Runner + Send + Sync),
    ) -> Result<Runtime, ModelError> {
        // Create incoming/outgoing directories, and populate them.
        let exposed_dir = ExposedDir::new(self, abs_moniker, decl.clone())?;
        let (outgoing_dir_client, outgoing_dir_server) =
            zx::Channel::create().map_err(|e| ModelError::namespace_creation_failed(e))?;
        let (runtime_dir_client, runtime_dir_server) =
            zx::Channel::create().map_err(|e| ModelError::namespace_creation_failed(e))?;
        let mut namespace = IncomingNamespace::new(package)?;
        let ns = namespace.populate(self.clone(), abs_moniker, decl).await?;

        // Set up channels into/out of the new component.
        let runtime = Runtime::start_from(
            url,
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
            resolved_url: Some(runtime.resolved_url.clone()),
            program: data::clone_option_dictionary(&decl.program),
            ns: Some(ns),
            outgoing_dir: Some(ServerEnd::new(outgoing_dir_server)),
            runtime_dir: Some(ServerEnd::new(runtime_dir_server)),
        };

        // Ask the runner to launch the runtime.
        runner.start(start_info).await?;

        Ok(runtime)
    }
}

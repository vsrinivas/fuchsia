// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        capability::*,
        model::{
            self,
            addable_directory::{AddableDirectory, AddableDirectoryWithResult},
            error::ModelError,
            hooks::*,
            CapabilityUsageTree,
        },
    },
    cm_rust::{ComponentDecl, UseDecl},
    directory_broker,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::{DirectoryProxy, NodeMarker, CLONE_FLAG_SAME_RIGHTS, MODE_TYPE_DIRECTORY},
    fuchsia_async as fasync,
    fuchsia_vfs_pseudo_fs::{directory, file::simple::read_only},
    fuchsia_zircon as zx,
    futures::{
        future::{AbortHandle, Abortable, BoxFuture},
        lock::Mutex,
    },
    std::{
        collections::HashMap,
        sync::{Arc, Weak},
    },
};

struct HubCapabilityProvider {
    abs_moniker: model::AbsoluteMoniker,
    relative_path: Vec<String>,
    hub_inner: Arc<HubInner>,
}

impl HubCapabilityProvider {
    pub fn new(
        abs_moniker: model::AbsoluteMoniker,
        relative_path: Vec<String>,
        hub_inner: Arc<HubInner>,
    ) -> Self {
        HubCapabilityProvider { abs_moniker, relative_path, hub_inner }
    }

    async fn open_async(
        &self,
        flags: u32,
        open_mode: u32,
        relative_path: String,
        server_end: zx::Channel,
    ) -> Result<(), ModelError> {
        let mut dir_path = self.relative_path.clone();
        dir_path.append(
            &mut relative_path
                .split("/")
                .map(|s| s.to_string())
                .filter(|s| !s.is_empty())
                .collect::<Vec<String>>(),
        );

        self.hub_inner.open(&self.abs_moniker, flags, open_mode, dir_path, server_end).await?;

        Ok(())
    }
}

impl ComponentManagerCapabilityProvider for HubCapabilityProvider {
    fn open(
        &self,
        flags: u32,
        open_mode: u32,
        relative_path: String,
        server_chan: zx::Channel,
    ) -> BoxFuture<Result<(), ModelError>> {
        Box::pin(self.open_async(flags, open_mode, relative_path, server_chan))
    }
}

/// Hub state on an instance of a component.
struct Instance {
    pub abs_moniker: model::AbsoluteMoniker,
    pub component_url: String,
    pub execution: Option<Execution>,
    pub directory: directory::controlled::Controller<'static>,
    pub children_directory: directory::controlled::Controller<'static>,
    pub deleting_directory: directory::controlled::Controller<'static>,
}

/// The execution state for a component that has started running.
struct Execution {
    pub resolved_url: String,
    pub directory: directory::controlled::Controller<'static>,
    pub capability_usage_tree: CapabilityUsageTree<'static>,
}

/// The Hub is a directory tree representing the component topology. Through the Hub,
/// debugging and instrumentation tools can query information about component instances
/// on the system, such as their component URLs, execution state and so on.
///
/// Hub itself does not store any state locally other than a reference to `HubInner`
/// where all the state and business logic resides. This enables Hub to be cloneable
/// to be passed across tasks.
pub struct Hub {
    inner: Arc<HubInner>,
}

impl Hub {
    /// Create a new Hub given a `component_url` and a controller to the root directory.
    pub fn new(component_url: String) -> Result<Self, ModelError> {
        Ok(Hub { inner: Arc::new(HubInner::new(component_url)?) })
    }

    pub async fn open_root(&self, flags: u32, server_end: zx::Channel) -> Result<(), ModelError> {
        let root_moniker = model::AbsoluteMoniker::root();
        self.inner.open(&root_moniker, flags, MODE_TYPE_DIRECTORY, vec![], server_end).await?;
        Ok(())
    }

    pub fn hooks(&self) -> Vec<HooksRegistration> {
        vec![HooksRegistration {
            events: vec![
                EventType::AddDynamicChild,
                EventType::PostDestroyInstance,
                EventType::PreDestroyInstance,
                EventType::RouteFrameworkCapability,
                EventType::StartInstance,
                EventType::StopInstance,
                EventType::UseCapability,
            ],
            callback: Arc::downgrade(&self.inner) as Weak<dyn Hook>,
        }]
    }
}

pub struct HubInner {
    instances: Mutex<HashMap<model::AbsoluteMoniker, Instance>>,
    /// Called when Hub is dropped to drop pseudodirectory hosting the Hub.
    abort_handle: AbortHandle,
}

impl Drop for HubInner {
    fn drop(&mut self) {
        self.abort_handle.abort();
    }
}

impl HubInner {
    /// Create a new Hub given a |component_url| and a controller to the root directory.
    pub fn new(component_url: String) -> Result<Self, ModelError> {
        let mut instances_map = HashMap::new();
        let abs_moniker = model::AbsoluteMoniker::root();

        let root_directory =
            HubInner::add_instance_if_necessary(&abs_moniker, component_url, &mut instances_map)?
                .expect("Did not create directory.");

        // Run the hub root directory forever until the component manager is terminated.
        let (abort_handle, abort_registration) = AbortHandle::new_pair();
        let future = Abortable::new(root_directory, abort_registration);
        fasync::spawn(async move {
            let _ = future.await;
        });

        Ok(HubInner { instances: Mutex::new(instances_map), abort_handle })
    }

    pub async fn open(
        &self,
        abs_moniker: &model::AbsoluteMoniker,
        flags: u32,
        open_mode: u32,
        relative_path: Vec<String>,
        server_end: zx::Channel,
    ) -> Result<(), ModelError> {
        let instances_map = self.instances.lock().await;
        if !instances_map.contains_key(&abs_moniker) {
            let relative_path = relative_path.join("/");
            return Err(ModelError::open_directory_error(abs_moniker.clone(), relative_path));
        }
        instances_map[&abs_moniker]
            .directory
            .open_node(
                flags,
                open_mode,
                relative_path,
                ServerEnd::<NodeMarker>::new(server_end),
                &abs_moniker,
            )
            .await?;
        Ok(())
    }

    fn add_instance_if_necessary(
        abs_moniker: &model::AbsoluteMoniker,
        component_url: String,
        instance_map: &mut HashMap<model::AbsoluteMoniker, Instance>,
    ) -> Result<Option<directory::controlled::Controlled<'static>>, ModelError> {
        if instance_map.contains_key(&abs_moniker) {
            return Ok(None);
        }

        let (instance_controller, mut instance_controlled) =
            directory::controlled::controlled(directory::simple::empty());

        // Add a 'url' file.
        instance_controlled.add_node(
            "url",
            {
                let url = component_url.clone();
                read_only(move || Ok(url.clone().into_bytes()))
            },
            &abs_moniker,
        )?;

        // Add an 'id' file.
        if let Some(child_moniker) = abs_moniker.leaf() {
            instance_controlled.add_node(
                "id",
                {
                    let child_moniker = child_moniker.clone();
                    read_only(move || Ok(child_moniker.instance().to_string().into_bytes()))
                },
                &abs_moniker,
            )?;
        }

        // Add a children directory.
        let (children_controller, children_controlled) =
            directory::controlled::controlled(directory::simple::empty());
        instance_controlled.add_node("children", children_controlled, &abs_moniker)?;

        // Add a deleting directory.
        let (deleting_controller, deleting_controlled) =
            directory::controlled::controlled(directory::simple::empty());
        instance_controlled.add_node("deleting", deleting_controlled, &abs_moniker)?;

        instance_map.insert(
            abs_moniker.clone(),
            Instance {
                abs_moniker: abs_moniker.clone(),
                component_url,
                execution: None,
                directory: instance_controller,
                children_directory: children_controller,
                deleting_directory: deleting_controller,
            },
        );

        Ok(Some(instance_controlled))
    }

    async fn add_instance_to_parent_if_necessary<'a>(
        abs_moniker: &'a model::AbsoluteMoniker,
        component_url: String,
        mut instances_map: &'a mut HashMap<model::AbsoluteMoniker, Instance>,
    ) -> Result<(), ModelError> {
        if let Some(controlled) =
            HubInner::add_instance_if_necessary(&abs_moniker, component_url, &mut instances_map)?
        {
            if let (Some(leaf), Some(parent_moniker)) = (abs_moniker.leaf(), abs_moniker.parent()) {
                // In the children directory, the child's instance id is not used
                let partial_moniker = leaf.to_partial();
                instances_map[&parent_moniker]
                    .children_directory
                    .add_node(partial_moniker.as_str(), controlled, &abs_moniker)
                    .await?;
            }
        }
        Ok(())
    }

    fn add_resolved_url_file(
        execution_directory: &mut directory::controlled::Controlled<'static>,
        resolved_url: String,
        abs_moniker: &model::AbsoluteMoniker,
    ) -> Result<(), ModelError> {
        execution_directory.add_node(
            "resolved_url",
            { read_only(move || Ok(resolved_url.clone().into_bytes())) },
            &abs_moniker,
        )?;
        Ok(())
    }

    fn add_in_directory(
        execution_directory: &mut directory::controlled::Controlled<'static>,
        component_decl: ComponentDecl,
        runtime: &model::Runtime,
        routing_facade: &model::RoutingFacade,
        abs_moniker: &model::AbsoluteMoniker,
    ) -> Result<(), ModelError> {
        let tree = model::DirTree::build_from_uses(
            routing_facade.route_use_fn_factory(),
            &abs_moniker,
            component_decl,
        );
        let mut in_dir = directory::simple::empty();
        tree.install(&abs_moniker, &mut in_dir)?;
        let pkg_dir = runtime.namespace.as_ref().and_then(|n| n.package_dir.as_ref());
        if let Some(pkg_dir) = Self::clone_dir(pkg_dir) {
            in_dir.add_node(
                "pkg",
                directory_broker::DirectoryBroker::from_directory_proxy(pkg_dir),
                &abs_moniker,
            )?;
        }
        execution_directory.add_node("in", in_dir, &abs_moniker)?;
        Ok(())
    }

    fn add_expose_directory(
        execution_directory: &mut directory::controlled::Controlled<'static>,
        component_decl: ComponentDecl,
        routing_facade: &model::RoutingFacade,
        abs_moniker: &model::AbsoluteMoniker,
    ) -> Result<(), ModelError> {
        let tree = model::DirTree::build_from_exposes(
            routing_facade.route_expose_fn_factory(),
            &abs_moniker,
            component_decl,
        );
        let mut expose_dir = directory::simple::empty();
        tree.install(&abs_moniker, &mut expose_dir)?;
        execution_directory.add_node("expose", expose_dir, &abs_moniker)?;
        Ok(())
    }

    fn add_out_directory(
        execution_directory: &mut directory::controlled::Controlled<'static>,
        runtime: &model::Runtime,
        abs_moniker: &model::AbsoluteMoniker,
    ) -> Result<(), ModelError> {
        if let Some(out_dir) = Self::clone_dir(runtime.outgoing_dir.as_ref()) {
            execution_directory.add_node(
                "out",
                directory_broker::DirectoryBroker::from_directory_proxy(out_dir),
                &abs_moniker,
            )?;
        }
        Ok(())
    }

    fn add_runtime_directory(
        execution_directory: &mut directory::controlled::Controlled<'static>,
        runtime: &model::Runtime,
        abs_moniker: &model::AbsoluteMoniker,
    ) -> Result<(), ModelError> {
        if let Some(runtime_dir) = Self::clone_dir(runtime.runtime_dir.as_ref()) {
            execution_directory.add_node(
                "runtime",
                directory_broker::DirectoryBroker::from_directory_proxy(runtime_dir),
                &abs_moniker,
            )?;
        }
        Ok(())
    }

    async fn on_start_instance_async<'a>(
        &'a self,
        realm: Arc<model::Realm>,
        component_decl: &'a ComponentDecl,
        live_child_realms: &'a Vec<Arc<model::Realm>>,
        routing_facade: model::RoutingFacade,
    ) -> Result<(), ModelError> {
        let component_url = realm.component_url.clone();
        let abs_moniker = realm.abs_moniker.clone();
        let mut instances_map = self.instances.lock().await;

        Self::add_instance_to_parent_if_necessary(&abs_moniker, component_url, &mut instances_map)
            .await?;

        let instance = instances_map
            .get_mut(&abs_moniker)
            .expect(&format!("Unable to find instance {} in map.", abs_moniker));

        // If we haven't already created an execution directory, create one now.
        if instance.execution.is_none() {
            let execution = realm.lock_execution().await;
            if let Some(runtime) = execution.runtime.as_ref() {
                let (execution_controller, mut execution_controlled) =
                    directory::controlled::controlled(directory::simple::empty());

                let (used_controller, used_controlled) =
                    directory::controlled::controlled(directory::simple::empty());

                let capability_usage_tree =
                    CapabilityUsageTree::new(used_controller, routing_facade.clone());
                let exec = Execution {
                    resolved_url: runtime.resolved_url.clone(),
                    directory: execution_controller,
                    capability_usage_tree,
                };
                instance.execution = Some(exec);

                Self::add_resolved_url_file(
                    &mut execution_controlled,
                    runtime.resolved_url.clone(),
                    &abs_moniker,
                )?;

                Self::add_in_directory(
                    &mut execution_controlled,
                    component_decl.clone(),
                    &runtime,
                    &routing_facade,
                    &abs_moniker,
                )?;

                Self::add_expose_directory(
                    &mut execution_controlled,
                    component_decl.clone(),
                    &routing_facade,
                    &abs_moniker,
                )?;

                execution_controlled.add_node("used", used_controlled, &abs_moniker)?;

                Self::add_out_directory(&mut execution_controlled, runtime, &abs_moniker)?;

                Self::add_runtime_directory(&mut execution_controlled, runtime, &abs_moniker)?;

                instance.directory.add_node("exec", execution_controlled, &abs_moniker).await?;
            }
        }

        // TODO: Loop over deleting realms also?
        for child_realm in live_child_realms {
            Self::add_instance_to_parent_if_necessary(
                &child_realm.abs_moniker,
                child_realm.component_url.clone(),
                &mut instances_map,
            )
            .await?;
        }

        Ok(())
    }

    async fn on_add_dynamic_child_async(&self, realm: Arc<model::Realm>) -> Result<(), ModelError> {
        let mut instances_map = self.instances.lock().await;
        Self::add_instance_to_parent_if_necessary(
            &realm.abs_moniker,
            realm.component_url.clone(),
            &mut instances_map,
        )
        .await?;
        Ok(())
    }

    async fn on_post_destroy_instance_async(
        &self,
        realm: Arc<model::Realm>,
    ) -> Result<(), ModelError> {
        let mut instance_map = self.instances.lock().await;

        // TODO(xbhatnag): Investigate error handling scenarios here.
        //                 Can these errors arise from faulty components or from
        //                 a bug in ComponentManager?
        let parent_moniker =
            realm.abs_moniker.parent().expect("a root component cannot be dynamic");
        let leaf = realm.abs_moniker.leaf().expect("a root component cannot be dynamic");

        instance_map[&parent_moniker].deleting_directory.remove_node(leaf.as_str()).await?;
        instance_map
            .remove(&realm.abs_moniker)
            .expect("the dynamic component must exist in the instance map");
        Ok(())
    }

    async fn on_stop_instance_async(&self, realm: Arc<model::Realm>) -> Result<(), ModelError> {
        let mut instance_map = self.instances.lock().await;
        instance_map[&realm.abs_moniker].directory.remove_node("exec").await?;
        instance_map.get_mut(&realm.abs_moniker).expect("instance must exist").execution = None;
        Ok(())
    }

    async fn on_pre_destroy_instance_async(
        &self,
        realm: Arc<model::Realm>,
    ) -> Result<(), ModelError> {
        let instance_map = self.instances.lock().await;

        let parent_moniker =
            realm.abs_moniker.parent().expect("A root component cannot be destroyed");
        let leaf = realm.abs_moniker.leaf().expect("A root component cannot be destroyed");

        // In the children directory, the child's instance id is not used
        let partial_moniker = leaf.to_partial();
        let directory = instance_map[&parent_moniker]
            .children_directory
            .remove_node(partial_moniker.as_str())
            .await?
            .ok_or(ModelError::remove_entry_error(leaf.as_str()))?;

        // In the deleting directory, the child's instance id is used
        instance_map[&parent_moniker]
            .deleting_directory
            .add_node(leaf.as_str(), directory, &realm.abs_moniker)
            .await?;

        Ok(())
    }

    async fn on_route_framework_capability_async<'a>(
        self: Arc<Self>,
        realm: Arc<model::Realm>,
        capability: &'a ComponentManagerCapability,
        capability_provider: Option<Box<dyn ComponentManagerCapabilityProvider>>,
    ) -> Result<Option<Box<dyn ComponentManagerCapabilityProvider>>, ModelError> {
        // If this capability is not a directory, then it's not a hub capability.
        let mut relative_path = match (&capability_provider, capability) {
            (None, ComponentManagerCapability::Directory(source_path)) => source_path.split(),
            _ => return Ok(capability_provider),
        };

        // If this capability's source path doesn't begin with 'hub', then it's
        // not a hub capability.
        if relative_path.is_empty() || relative_path.remove(0) != "hub" {
            return Ok(capability_provider);
        }

        Ok(Some(Box::new(HubCapabilityProvider::new(
            realm.abs_moniker.clone(),
            relative_path,
            self.clone(),
        ))))
    }

    async fn on_capability_use_async(
        &self,
        realm: Arc<model::Realm>,
        use_: UseDecl,
    ) -> Result<(), ModelError> {
        let mut instance_map = self.instances.lock().await;
        let execution = instance_map
            .get_mut(&realm.abs_moniker)
            .expect("A component that is using a service must exist in the instance map")
            .execution
            .as_mut()
            .expect("A component that is using a service must have an execution.");

        execution.capability_usage_tree.mark_capability_used(&realm.abs_moniker, use_).await?;

        Ok(())
    }

    // TODO(fsamuel): We should probably preserve the original error messages
    // instead of dropping them.
    fn clone_dir(dir: Option<&DirectoryProxy>) -> Option<DirectoryProxy> {
        dir.and_then(|d| io_util::clone_directory(d, CLONE_FLAG_SAME_RIGHTS).ok())
    }
}

impl model::Hook for HubInner {
    fn on<'a>(self: Arc<Self>, event: &'a Event) -> BoxFuture<'a, Result<(), ModelError>> {
        Box::pin(async move {
            match &event.payload {
                EventPayload::StartInstance {
                    component_decl,
                    live_child_realms,
                    routing_facade,
                } => {
                    self.on_start_instance_async(
                        event.target_realm.clone(),
                        &component_decl,
                        &live_child_realms,
                        routing_facade.clone(),
                    )
                    .await?;
                }
                EventPayload::AddDynamicChild => {
                    self.on_add_dynamic_child_async(event.target_realm.clone()).await?;
                }
                EventPayload::PreDestroyInstance => {
                    self.on_pre_destroy_instance_async(event.target_realm.clone()).await?;
                }
                EventPayload::StopInstance => {
                    self.on_stop_instance_async(event.target_realm.clone()).await?;
                }
                EventPayload::PostDestroyInstance => {
                    self.on_post_destroy_instance_async(event.target_realm.clone()).await?;
                }
                EventPayload::RouteFrameworkCapability { capability, capability_provider } => {
                    let mut capability_provider = capability_provider.lock().await;
                    *capability_provider = self
                        .on_route_framework_capability_async(
                            event.target_realm.clone(),
                            &capability,
                            capability_provider.take(),
                        )
                        .await?;
                }
                EventPayload::UseCapability { use_ } => {
                    self.on_capability_use_async(event.target_realm.clone(), use_.clone()).await?;
                }
                _ => {}
            };
            Ok(())
        })
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::model::{
            self,
            testing::mocks,
            testing::{
                test_helpers::*,
                test_helpers::{dir_contains, list_directory, list_directory_recursive, read_file},
                test_hook::HubInjectionTestHook,
            },
            BuiltinEnvironment, ComponentManagerConfig,
        },
        crate::startup,
        cm_rust::{
            self, CapabilityPath, ChildDecl, ComponentDecl, ExposeDecl, ExposeDirectoryDecl,
            ExposeLegacyServiceDecl, ExposeSource, ExposeTarget, UseDecl, UseDirectoryDecl,
            UseLegacyServiceDecl, UseSource,
        },
        fidl::endpoints::ServerEnd,
        fidl_fuchsia_io::{
            DirectoryMarker, DirectoryProxy, MODE_TYPE_DIRECTORY, OPEN_RIGHT_READABLE,
            OPEN_RIGHT_WRITABLE,
        },
        fidl_fuchsia_io2 as fio2, fidl_fuchsia_sys2 as fsys,
        fuchsia_vfs_pseudo_fs::directory::entry::DirectoryEntry,
        std::{convert::TryFrom, iter, path::Path},
    };

    /// Hosts an out directory with a 'foo' file.
    fn foo_out_dir_fn() -> Box<dyn Fn(ServerEnd<DirectoryMarker>) + Send + Sync> {
        Box::new(move |server_end: ServerEnd<DirectoryMarker>| {
            let mut out_dir = directory::simple::empty();
            // Add a 'foo' file.
            out_dir
                .add_entry("foo", { read_only(move || Ok(b"bar".to_vec())) })
                .map_err(|(s, _)| s)
                .expect("Failed to add 'foo' entry");

            out_dir.open(
                OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
                MODE_TYPE_DIRECTORY,
                &mut iter::empty(),
                ServerEnd::new(server_end.into_channel()),
            );

            let mut test_dir = directory::simple::empty();
            test_dir
                .add_entry("aaa", { read_only(move || Ok(b"bbb".to_vec())) })
                .map_err(|(s, _)| s)
                .expect("Failed to add 'aaa' entry");
            out_dir
                .add_entry("test", test_dir)
                .map_err(|(s, _)| s)
                .expect("Failed to add 'test' directory.");

            fasync::spawn(async move {
                let _ = out_dir.await;
            });
        })
    }

    /// Hosts a runtime directory with a 'bleep' file.
    fn bleep_runtime_dir_fn() -> Box<dyn Fn(ServerEnd<DirectoryMarker>) + Send + Sync> {
        Box::new(move |server_end: ServerEnd<DirectoryMarker>| {
            let mut pseudo_dir = directory::simple::empty();
            // Add a 'bleep' file.
            pseudo_dir
                .add_entry("bleep", { read_only(move || Ok(b"blah".to_vec())) })
                .map_err(|(s, _)| s)
                .expect("Failed to add 'bleep' entry");

            pseudo_dir.open(
                OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
                MODE_TYPE_DIRECTORY,
                &mut iter::empty(),
                ServerEnd::new(server_end.into_channel()),
            );
            fasync::spawn(async move {
                let _ = pseudo_dir.await;
            });
        })
    }

    type DirectoryCallback = Box<dyn Fn(ServerEnd<DirectoryMarker>) + Send + Sync>;

    struct ComponentDescriptor {
        pub name: String,
        pub decl: ComponentDecl,
        pub host_fn: Option<DirectoryCallback>,
        pub runtime_host_fn: Option<DirectoryCallback>,
    }

    async fn start_component_manager_with_hub(
        root_component_url: String,
        components: Vec<ComponentDescriptor>,
    ) -> (Arc<model::Model>, BuiltinEnvironment, DirectoryProxy) {
        start_component_manager_with_hub_and_hooks(root_component_url, components, vec![]).await
    }

    async fn start_component_manager_with_hub_and_hooks(
        root_component_url: String,
        components: Vec<ComponentDescriptor>,
        additional_hooks: Vec<HooksRegistration>,
    ) -> (Arc<model::Model>, BuiltinEnvironment, DirectoryProxy) {
        let resolved_root_component_url = format!("{}_resolved", root_component_url);
        let mut resolver = model::ResolverRegistry::new();
        let mut runner = mocks::MockRunner::new();
        let mut mock_resolver = mocks::MockResolver::new();
        for component in components.into_iter() {
            mock_resolver.add_component(&component.name, component.decl);
            if let Some(host_fn) = component.host_fn {
                runner.host_fns.insert(resolved_root_component_url.clone(), host_fn);
            }

            if let Some(runtime_host_fn) = component.runtime_host_fn {
                runner
                    .runtime_host_fns
                    .insert(resolved_root_component_url.clone(), runtime_host_fn);
            }
        }
        resolver.register("test".to_string(), Box::new(mock_resolver));

        let startup_args = startup::Arguments {
            use_builtin_process_launcher: false,
            use_builtin_vmex: false,
            root_component_url: root_component_url.clone(),
            debug: false,
        };
        let model = Arc::new(model::Model::new(model::ModelParams {
            root_component_url,
            root_resolver_registry: resolver,
            elf_runner: Arc::new(runner),
        }));
        let builtin_environment = startup::builtin_environment_setup(
            &startup_args,
            &model,
            ComponentManagerConfig::default(),
        )
        .await
        .expect("failed to set up builtin environment");
        let hub_proxy = builtin_environment
            .bind_service_fs_for_hub(&model)
            .await
            .expect("unable to bind service_fs");

        model.root_realm.hooks.install(additional_hooks).await;

        let root_moniker = model::AbsoluteMoniker::root();
        let res = model.bind(&root_moniker).await;
        let expected_res: Result<(), model::ModelError> = Ok(());
        assert_eq!(format!("{:?}", res), format!("{:?}", expected_res));

        (model, builtin_environment, hub_proxy)
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn hub_basic() {
        let root_component_url = "test:///root".to_string();
        let (_model, _builtin_environment, hub_proxy) = start_component_manager_with_hub(
            root_component_url.clone(),
            vec![
                ComponentDescriptor {
                    name: "root".to_string(),
                    decl: ComponentDecl {
                        children: vec![ChildDecl {
                            name: "a".to_string(),
                            url: "test:///a".to_string(),
                            startup: fsys::StartupMode::Lazy,
                        }],
                        ..default_component_decl()
                    },
                    host_fn: None,
                    runtime_host_fn: None,
                },
                ComponentDescriptor {
                    name: "a".to_string(),
                    decl: ComponentDecl { children: vec![], ..default_component_decl() },
                    host_fn: None,
                    runtime_host_fn: None,
                },
            ],
        )
        .await;

        assert_eq!(root_component_url, read_file(&hub_proxy, "url").await);
        assert_eq!(
            format!("{}_resolved", root_component_url),
            read_file(&hub_proxy, "exec/resolved_url").await
        );
        assert_eq!("test:///a", read_file(&hub_proxy, "children/a/url").await);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn hub_out_directory() {
        let root_component_url = "test:///root".to_string();
        let (_model, _builtin_environment, hub_proxy) = start_component_manager_with_hub(
            root_component_url.clone(),
            vec![ComponentDescriptor {
                name: "root".to_string(),
                decl: ComponentDecl {
                    children: vec![ChildDecl {
                        name: "a".to_string(),
                        url: "test:///a".to_string(),
                        startup: fsys::StartupMode::Lazy,
                    }],
                    ..default_component_decl()
                },
                host_fn: Some(foo_out_dir_fn()),
                runtime_host_fn: None,
            }],
        )
        .await;

        assert!(dir_contains(&hub_proxy, "exec", "out").await);
        assert!(dir_contains(&hub_proxy, "exec/out", "foo").await);
        assert!(dir_contains(&hub_proxy, "exec/out/test", "aaa").await);
        assert_eq!("bar", read_file(&hub_proxy, "exec/out/foo").await);
        assert_eq!("bbb", read_file(&hub_proxy, "exec/out/test/aaa").await);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn hub_runtime_directory() {
        let root_component_url = "test:///root".to_string();
        let (_model, _builtin_environment, hub_proxy) = start_component_manager_with_hub(
            root_component_url.clone(),
            vec![ComponentDescriptor {
                name: "root".to_string(),
                decl: ComponentDecl {
                    children: vec![ChildDecl {
                        name: "a".to_string(),
                        url: "test:///a".to_string(),
                        startup: fsys::StartupMode::Lazy,
                    }],
                    ..default_component_decl()
                },
                host_fn: None,
                runtime_host_fn: Some(bleep_runtime_dir_fn()),
            }],
        )
        .await;

        assert_eq!("blah", read_file(&hub_proxy, "exec/runtime/bleep").await);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn hub_test_hook_interception() {
        let root_component_url = "test:///root".to_string();
        let hub_injection_test_hook = Arc::new(HubInjectionTestHook::new());
        let (_model, _builtin_environment, hub_proxy) = start_component_manager_with_hub_and_hooks(
            root_component_url.clone(),
            vec![ComponentDescriptor {
                name: "root".to_string(),
                decl: ComponentDecl {
                    children: vec![ChildDecl {
                        name: "a".to_string(),
                        url: "test:///a".to_string(),
                        startup: fsys::StartupMode::Lazy,
                    }],
                    uses: vec![UseDecl::Directory(UseDirectoryDecl {
                        source: UseSource::Framework,
                        source_path: CapabilityPath::try_from("/hub").unwrap(),
                        target_path: CapabilityPath::try_from("/hub").unwrap(),
                        rights: fio2::Operations::Connect,
                    })],
                    ..default_component_decl()
                },
                host_fn: None,
                runtime_host_fn: None,
            }],
            vec![HooksRegistration {
                events: vec![EventType::RouteFrameworkCapability],
                callback: Arc::downgrade(&hub_injection_test_hook) as Weak<dyn Hook>,
            }],
        )
        .await;

        let in_dir = io_util::open_directory(
            &hub_proxy,
            &Path::new("exec/in"),
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
        )
        .expect("Failed to open directory");
        assert_eq!(vec!["hub"], list_directory(&in_dir).await);

        let scoped_hub_dir_proxy = io_util::open_directory(
            &hub_proxy,
            &Path::new("exec/in/hub"),
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
        )
        .expect("Failed to open directory");
        // There are no out or runtime directories because there is no program running.
        assert_eq!(vec!["old_hub"], list_directory(&scoped_hub_dir_proxy).await);

        let old_hub_dir_proxy = io_util::open_directory(
            &hub_proxy,
            &Path::new("exec/in/hub/old_hub/exec"),
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
        )
        .expect("Failed to open directory");
        // There are no out or runtime directories because there is no program running.
        assert_eq!(
            vec!["expose", "in", "resolved_url", "used"],
            list_directory(&old_hub_dir_proxy).await
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn hub_in_directory() {
        let root_component_url = "test:///root".to_string();
        let (_model, _builtin_environment, hub_proxy) = start_component_manager_with_hub(
            root_component_url.clone(),
            vec![ComponentDescriptor {
                name: "root".to_string(),
                decl: ComponentDecl {
                    children: vec![ChildDecl {
                        name: "a".to_string(),
                        url: "test:///a".to_string(),
                        startup: fsys::StartupMode::Lazy,
                    }],
                    uses: vec![
                        UseDecl::Directory(UseDirectoryDecl {
                            source: UseSource::Framework,
                            source_path: CapabilityPath::try_from("/hub/exec").unwrap(),
                            target_path: CapabilityPath::try_from("/hub").unwrap(),
                            rights: fio2::Operations::Connect,
                        }),
                        UseDecl::LegacyService(UseLegacyServiceDecl {
                            source: UseSource::Realm,
                            source_path: CapabilityPath::try_from("/svc/baz").unwrap(),
                            target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                        }),
                        UseDecl::Directory(UseDirectoryDecl {
                            source: UseSource::Realm,
                            source_path: CapabilityPath::try_from("/data/foo").unwrap(),
                            target_path: CapabilityPath::try_from("/data/bar").unwrap(),
                            rights: fio2::Operations::Connect,
                        }),
                    ],
                    ..default_component_decl()
                },
                host_fn: None,
                runtime_host_fn: None,
            }],
        )
        .await;

        let in_dir = io_util::open_directory(
            &hub_proxy,
            &Path::new("exec/in"),
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
        )
        .expect("Failed to open directory");
        assert_eq!(vec!["data", "hub", "svc"], list_directory(&in_dir).await);

        let scoped_hub_dir_proxy = io_util::open_directory(
            &hub_proxy,
            &Path::new("exec/in/hub"),
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
        )
        .expect("Failed to open directory");
        // There are no out or runtime directories because there is no program running.
        assert_eq!(
            vec!["expose", "in", "resolved_url", "used"],
            list_directory(&scoped_hub_dir_proxy).await
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn hub_expose_directory() {
        let root_component_url = "test:///root".to_string();
        let (_model, _builtin_environment, hub_proxy) = start_component_manager_with_hub(
            root_component_url.clone(),
            vec![ComponentDescriptor {
                name: "root".to_string(),
                decl: ComponentDecl {
                    children: vec![ChildDecl {
                        name: "a".to_string(),
                        url: "test:///a".to_string(),
                        startup: fsys::StartupMode::Lazy,
                    }],
                    exposes: vec![
                        ExposeDecl::LegacyService(ExposeLegacyServiceDecl {
                            source: ExposeSource::Self_,
                            source_path: CapabilityPath::try_from("/svc/foo").unwrap(),
                            target_path: CapabilityPath::try_from("/svc/bar").unwrap(),
                            target: ExposeTarget::Realm,
                        }),
                        ExposeDecl::Directory(ExposeDirectoryDecl {
                            source: ExposeSource::Self_,
                            source_path: CapabilityPath::try_from("/data/baz").unwrap(),
                            target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                            target: ExposeTarget::Realm,
                            rights: Some(fio2::Operations::Connect),
                        }),
                    ],
                    ..default_component_decl()
                },
                host_fn: None,
                runtime_host_fn: None,
            }],
        )
        .await;

        let expose_dir = io_util::open_directory(
            &hub_proxy,
            &Path::new("exec/expose"),
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
        )
        .expect("Failed to open directory");
        assert_eq!(vec!["data/hippo", "svc/bar"], list_directory_recursive(&expose_dir).await);
    }
}

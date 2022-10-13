// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        capability::{CapabilityProvider, CapabilitySource},
        model::{
            addable_directory::AddableDirectoryWithResult,
            component::{StartReason, WeakComponentInstance},
            dir_tree::DirTree,
            error::ModelError,
            hooks::{Event, EventPayload, EventType, Hook, HooksRegistration, RuntimeInfo},
            routing_fns::{route_expose_fn, route_use_fn},
        },
    },
    ::routing::capability_source::InternalCapability,
    async_trait::async_trait,
    cm_moniker::IncarnationId,
    cm_rust::ComponentDecl,
    cm_task_scope::TaskScope,
    cm_util::{channel, io::clone_dir},
    config_encoder::ConfigFields,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io as fio, fuchsia_zircon as zx,
    futures::lock::Mutex,
    moniker::{AbsoluteMoniker, AbsoluteMonikerBase, ChildMonikerBase},
    rand::Rng,
    std::{
        collections::hash_map::HashMap,
        path::PathBuf,
        sync::{Arc, Weak},
    },
    tracing::{info, warn},
    vfs::{
        directory::entry::DirectoryEntry, directory::immutable::simple as pfs,
        execution_scope::ExecutionScope, file::vmo::asynchronous::read_only_static,
        path::Path as pfsPath, remote::remote_dir,
    },
};

// Declare simple directory type for brevity
type Directory = Arc<pfs::Simple>;

struct HubCapabilityProvider {
    moniker: AbsoluteMoniker,
    hub: Arc<Hub>,
}

impl HubCapabilityProvider {
    pub fn new(moniker: AbsoluteMoniker, hub: Arc<Hub>) -> Self {
        HubCapabilityProvider { moniker, hub }
    }
}

#[async_trait]
impl CapabilityProvider for HubCapabilityProvider {
    async fn open(
        self: Box<Self>,
        _task_scope: TaskScope,
        flags: fio::OpenFlags,
        open_mode: u32,
        relative_path: PathBuf,
        server_end: &mut zx::Channel,
    ) -> Result<(), ModelError> {
        info!(path=%relative_path.display(), "Opening the /hub capability provider");
        let mut relative_path = relative_path
            .to_str()
            .ok_or_else(|| ModelError::path_is_not_utf8(relative_path.clone()))?
            .to_string();
        relative_path.push('/');
        let dir_path = pfsPath::validate_and_split(relative_path.clone())
            .map_err(|_| ModelError::open_directory_error(self.moniker.clone(), relative_path))?;
        self.hub.open(&self.moniker, flags, open_mode, dir_path, server_end).await?;
        Ok(())
    }
}

/// Hub state on an instance of a component.
struct Instance {
    /// Whether the `directory` has a subdirectory named `exec`
    pub has_execution_directory: bool,
    /// Whether the `directory` has a subdirectory named `resolved`
    pub has_resolved_directory: bool,
    pub directory: Directory,
    pub children_directory: Directory,
    pub uuid: u128,
}

/// The Hub is a directory tree representing the component topology. Through the Hub,
/// debugging and instrumentation tools can query information about component instances
/// on the system, such as their component URLs, execution state and so on.
pub struct Hub {
    instances: Mutex<HashMap<AbsoluteMoniker, Instance>>,
    scope: ExecutionScope,
}

impl Hub {
    /// Create a new Hub given a `component_url` for the root component
    pub fn new(component_url: String) -> Result<Self, ModelError> {
        let mut instance_map = HashMap::new();
        let moniker = AbsoluteMoniker::root();

        Hub::add_instance_if_necessary(&moniker, component_url, 0, &mut instance_map)?
            .expect("Did not create directory.");

        Ok(Hub { instances: Mutex::new(instance_map), scope: ExecutionScope::new() })
    }

    pub async fn open_root(
        &self,
        flags: fio::OpenFlags,
        mut server_end: zx::Channel,
    ) -> Result<(), ModelError> {
        let root_moniker = AbsoluteMoniker::root();
        self.open(&root_moniker, flags, fio::MODE_TYPE_DIRECTORY, pfsPath::dot(), &mut server_end)
            .await?;
        Ok(())
    }

    pub fn hooks(self: &Arc<Self>) -> Vec<HooksRegistration> {
        vec![HooksRegistration::new(
            "Hub",
            vec![
                EventType::CapabilityRouted,
                EventType::Discovered,
                EventType::Destroyed,
                EventType::Started,
                EventType::Resolved,
                EventType::Unresolved,
                EventType::Stopped,
            ],
            Arc::downgrade(self) as Weak<dyn Hook>,
        )]
    }

    pub async fn open(
        &self,
        moniker: &AbsoluteMoniker,
        flags: fio::OpenFlags,
        open_mode: u32,
        relative_path: pfsPath,
        server_end: &mut zx::Channel,
    ) -> Result<(), ModelError> {
        let instance_map = self.instances.lock().await;
        let instance = instance_map.get(moniker).ok_or(ModelError::open_directory_error(
            moniker.clone(),
            relative_path.clone().into_string(),
        ))?;
        let server_end = channel::take_channel(server_end);
        instance.directory.clone().open(
            self.scope.clone(),
            flags,
            open_mode,
            relative_path,
            ServerEnd::<fio::NodeMarker>::new(server_end),
        );
        Ok(())
    }

    fn add_instance_if_necessary(
        moniker: &AbsoluteMoniker,
        component_url: String,
        incarnation: IncarnationId,
        instance_map: &mut HashMap<AbsoluteMoniker, Instance>,
    ) -> Result<Option<(u128, Directory)>, ModelError> {
        if instance_map.contains_key(&moniker) {
            return Ok(None);
        }

        let instance = pfs::simple();

        // Add a 'moniker' file.
        // The child moniker is stored in the `moniker` file in case it gets truncated when used as
        // the directory name. Root has an empty moniker file because it doesn't have a child moniker.
        let moniker_s = if let Some(instanced) = moniker.leaf() {
            instanced.to_string()
        } else {
            "".to_string()
        };
        instance.add_node("moniker", read_only_static(moniker_s.into_bytes()), moniker)?;

        // Add a 'url' file.
        instance.add_node("url", read_only_static(component_url.clone().into_bytes()), moniker)?;

        // Add an 'id' file.
        let component_type = if incarnation > 0 { "dynamic" } else { "static" };
        instance.add_node("id", read_only_static(incarnation.to_string().into_bytes()), moniker)?;

        // Add a 'component_type' file.
        instance.add_node(
            "component_type",
            read_only_static(component_type.to_string().into_bytes()),
            moniker,
        )?;

        // Add a children directory.
        let children = pfs::simple();
        instance.add_node("children", children.clone(), moniker)?;

        let mut rng = rand::thread_rng();
        let instance_uuid: u128 = rng.gen();

        instance_map.insert(
            moniker.clone(),
            Instance {
                has_execution_directory: false,
                has_resolved_directory: false,
                directory: instance.clone(),
                children_directory: children.clone(),
                uuid: instance_uuid.clone(),
            },
        );

        Ok(Some((instance_uuid, instance)))
    }

    // Child directory names created from monikers include both collection and child names
    // (ie "<coll>:<name>"). It's possible for the total moniker length to exceed the
    // `fidl_fuchsia_io::MAX_NAME_LENGTH` limit. In that case, the moniker is truncated and the
    // instance uuid is appended to produce a directory name within the `MAX_NAME_LENGTH` limit.
    fn child_dir_name(moniker: &str, uuid: u128) -> String {
        let max_name_len = fidl_fuchsia_io::MAX_NAME_LENGTH as usize;
        if moniker.len() > max_name_len {
            let encoded_uuid = format!("{:x}", uuid);
            let new_len = max_name_len - 1 - encoded_uuid.len();
            format!("{}#{}", moniker.get(..new_len).unwrap(), encoded_uuid)
        } else {
            moniker.to_string()
        }
    }

    async fn add_instance_to_parent_if_necessary<'a>(
        moniker: &'a AbsoluteMoniker,
        component_url: String,
        incarnation: IncarnationId,
        mut instance_map: &'a mut HashMap<AbsoluteMoniker, Instance>,
    ) -> Result<(), ModelError> {
        let (uuid, controlled) = match Hub::add_instance_if_necessary(
            moniker,
            component_url,
            incarnation,
            &mut instance_map,
        )? {
            Some(c) => c,
            None => return Ok(()),
        };

        if let (Some(child_moniker), Some(parent_moniker)) = (moniker.leaf(), moniker.parent()) {
            match instance_map.get_mut(&parent_moniker) {
                Some(instance) => {
                    instance.children_directory.add_node(
                        &Hub::child_dir_name(child_moniker.as_str(), uuid),
                        controlled.clone(),
                        moniker,
                    )?;
                }
                None => {
                    // TODO(fxbug.dev/89503): Investigate event ordering between
                    // parent and child, so that we can guarantee the parent is
                    // in the instance_map.
                    warn!(
                        "Parent {} not found: could not add {} to children directory.",
                        parent_moniker, moniker
                    );
                }
            };
        }
        Ok(())
    }

    fn add_resolved_url_file(
        directory: Directory,
        resolved_url: String,
        moniker: &AbsoluteMoniker,
    ) -> Result<(), ModelError> {
        directory.add_node("resolved_url", read_only_static(resolved_url.into_bytes()), moniker)?;
        Ok(())
    }

    fn add_config(
        directory: Directory,
        config: &ConfigFields,
        moniker: &AbsoluteMoniker,
    ) -> Result<(), ModelError> {
        let config_dir = pfs::simple();
        for field in &config.fields {
            let value = format!("{}", field.value);
            config_dir.add_node(&field.key, read_only_static(value.into_bytes()), moniker)?;
        }
        directory.add_node("config", config_dir, moniker)?;
        Ok(())
    }

    fn add_use_directory(
        directory: Directory,
        component_decl: ComponentDecl,
        target_moniker: &AbsoluteMoniker,
        target: WeakComponentInstance,
        pkg_dir: Option<fio::DirectoryProxy>,
    ) -> Result<(), ModelError> {
        let tree = DirTree::build_from_uses(route_use_fn, target, component_decl);
        let mut use_dir = pfs::simple();
        tree.install(target_moniker, &mut use_dir)?;

        if let Some(pkg_dir) = pkg_dir {
            use_dir.add_node("pkg", remote_dir(pkg_dir), target_moniker)?;
        }

        directory.add_node("use", use_dir, target_moniker)?;

        Ok(())
    }

    fn add_in_directory(
        execution_directory: Directory,
        component_decl: ComponentDecl,
        package_dir: Option<fio::DirectoryProxy>,
        target_moniker: &AbsoluteMoniker,
        target: WeakComponentInstance,
    ) -> Result<(), ModelError> {
        let tree = DirTree::build_from_uses(route_use_fn, target, component_decl);
        let mut in_dir = pfs::simple();
        tree.install(target_moniker, &mut in_dir)?;
        if let Some(pkg_dir) = package_dir {
            in_dir.add_node("pkg", remote_dir(pkg_dir), target_moniker)?;
        }
        execution_directory.add_node("in", in_dir, target_moniker)?;
        Ok(())
    }

    fn add_expose_directory(
        directory: Directory,
        component_decl: ComponentDecl,
        target_moniker: &AbsoluteMoniker,
        target: WeakComponentInstance,
    ) -> Result<(), ModelError> {
        let tree = DirTree::build_from_exposes(route_expose_fn, target, component_decl);
        let mut expose_dir = pfs::simple();
        tree.install(target_moniker, &mut expose_dir)?;
        directory.add_node("expose", expose_dir, target_moniker)?;
        Ok(())
    }

    fn add_out_directory(
        execution_directory: Directory,
        outgoing_dir: Option<fio::DirectoryProxy>,
        target_moniker: &AbsoluteMoniker,
    ) -> Result<(), ModelError> {
        if let Some(out_dir) = outgoing_dir {
            execution_directory.add_node("out", remote_dir(out_dir), target_moniker)?;
        }
        Ok(())
    }

    fn add_runtime_directory(
        execution_directory: Directory,
        runtime_dir: Option<fio::DirectoryProxy>,
        moniker: &AbsoluteMoniker,
    ) -> Result<(), ModelError> {
        if let Some(runtime_dir) = runtime_dir {
            execution_directory.add_node("runtime", remote_dir(runtime_dir), moniker)?;
        }
        Ok(())
    }

    fn add_start_reason_file(
        execution_directory: Directory,
        start_reason: &StartReason,
        instanced_moniker: &AbsoluteMoniker,
    ) -> Result<(), ModelError> {
        let start_reason = format!("{}", start_reason);
        execution_directory.add_node(
            "start_reason",
            read_only_static(start_reason.into_bytes()),
            instanced_moniker,
        )?;
        Ok(())
    }

    fn add_instance_id_file(
        directory: Directory,
        target_moniker: &AbsoluteMoniker,
        target: WeakComponentInstance,
    ) -> Result<(), ModelError> {
        if let Some(instance_id) = target.upgrade()?.instance_id() {
            directory.add_node(
                "instance_id",
                read_only_static(instance_id.to_string().into_bytes()),
                &target_moniker,
            )?;
        };
        Ok(())
    }

    async fn on_resolved_async<'a>(
        &self,
        target_moniker: &AbsoluteMoniker,
        target: &WeakComponentInstance,
        resolved_url: String,
        component_decl: &'a ComponentDecl,
        config: &Option<ConfigFields>,
        pkg_dir: Option<&fio::DirectoryProxy>,
    ) -> Result<(), ModelError> {
        let mut instance_map = self.instances.lock().await;

        let instance = instance_map
            .get_mut(target_moniker)
            .ok_or(ModelError::instance_not_found(target_moniker.clone()))?;

        // If the resolved directory already exists, report error.
        assert!(!instance.has_resolved_directory);
        let resolved_directory = pfs::simple();

        Self::add_resolved_url_file(
            resolved_directory.clone(),
            resolved_url.clone(),
            target_moniker,
        )?;

        Self::add_use_directory(
            resolved_directory.clone(),
            component_decl.clone(),
            target_moniker,
            target.clone(),
            clone_dir(pkg_dir),
        )?;

        Self::add_expose_directory(
            resolved_directory.clone(),
            component_decl.clone(),
            target_moniker,
            target.clone(),
        )?;

        Self::add_instance_id_file(resolved_directory.clone(), target_moniker, target.clone())?;

        if let Some(config) = config {
            Self::add_config(resolved_directory.clone(), config, target_moniker)?;
        }

        instance.directory.add_node("resolved", resolved_directory, &target_moniker)?;
        instance.has_resolved_directory = true;

        Ok(())
    }

    async fn on_started_async<'a>(
        &'a self,
        target_moniker: &AbsoluteMoniker,
        target: &WeakComponentInstance,
        runtime: &RuntimeInfo,
        component_decl: &'a ComponentDecl,
        start_reason: &StartReason,
    ) -> Result<(), ModelError> {
        let mut instance_map = self.instances.lock().await;

        let instance = instance_map
            .get_mut(target_moniker)
            .ok_or(ModelError::instance_not_found(target_moniker.clone()))?;

        // Don't create an execution directory if it already exists
        if instance.has_execution_directory {
            return Ok(());
        }

        let execution_directory = pfs::simple();

        Self::add_resolved_url_file(
            execution_directory.clone(),
            runtime.resolved_url.clone(),
            target_moniker,
        )?;

        Self::add_in_directory(
            execution_directory.clone(),
            component_decl.clone(),
            clone_dir(runtime.package_dir.as_ref()),
            target_moniker,
            target.clone(),
        )?;

        Self::add_expose_directory(
            execution_directory.clone(),
            component_decl.clone(),
            target_moniker,
            target.clone(),
        )?;

        Self::add_out_directory(
            execution_directory.clone(),
            clone_dir(runtime.outgoing_dir.as_ref()),
            target_moniker,
        )?;

        Self::add_runtime_directory(
            execution_directory.clone(),
            clone_dir(runtime.runtime_dir.as_ref()),
            &target_moniker,
        )?;

        Self::add_start_reason_file(execution_directory.clone(), start_reason, &target_moniker)?;

        instance.directory.add_node("exec", execution_directory, &target_moniker)?;
        instance.has_execution_directory = true;

        Ok(())
    }

    async fn on_discovered_async(
        &self,
        target_moniker: &AbsoluteMoniker,
        component_url: String,
        incarnation: IncarnationId,
    ) -> Result<(), ModelError> {
        let mut instance_map = self.instances.lock().await;

        Self::add_instance_to_parent_if_necessary(
            target_moniker,
            component_url,
            incarnation,
            &mut instance_map,
        )
        .await?;
        Ok(())
    }

    async fn on_unresolved_async(
        &self,
        target_moniker: &AbsoluteMoniker,
        _component_url: String,
    ) -> Result<(), ModelError> {
        let mut instance_map = self.instances.lock().await;

        // The component has been unresolved. Remove the resolved directory from the hub.
        let res = instance_map.get_mut(target_moniker);
        if let Some(instance) = res {
            if instance.has_resolved_directory {
                instance.directory.remove_node("resolved")?;
                instance.has_resolved_directory = false;
            }
        }
        Ok(())
    }

    async fn on_stopped_async(&self, target_moniker: &AbsoluteMoniker) -> Result<(), ModelError> {
        let mut instance_map = self.instances.lock().await;
        let mut instance = instance_map
            .get_mut(target_moniker)
            .ok_or(ModelError::instance_not_found(target_moniker.clone()))?;

        instance.directory.remove_node("exec")?.ok_or_else(|| {
            warn!(instance=%target_moniker, "exec directory was already removed");
            ModelError::remove_entry_error("exec")
        })?;
        instance.has_execution_directory = false;
        Ok(())
    }

    async fn on_destroyed_async(&self, target_moniker: &AbsoluteMoniker) -> Result<(), ModelError> {
        let parent_moniker = target_moniker.parent().expect("A root component cannot be destroyed");
        let mut instance_map = self.instances.lock().await;

        let instance = instance_map
            .get(&target_moniker)
            .ok_or(ModelError::instance_not_found(target_moniker.clone()))?;

        let child = target_moniker.leaf().expect("A root component cannot be destroyed");
        // In the children directory, the child's instance id is not used
        let child_name = child.to_string();
        let child_entry = Hub::child_dir_name(&child_name, instance.uuid);

        let parent_instance = match instance_map.get_mut(&parent_moniker) {
            Some(i) => i,
            // TODO(fxbug.dev/89503): This failsafe was originally introduce to protect against a
            // duplicate dispatch of Destroyed, which should no longer be possible since it is now
            // wrapped in an action. However, other races may be possible, such as this Destroyed
            // arriving before the parent's Discovered. Ideally we should fix all the races and
            // fail instead.
            None => return Ok(()),
        };

        parent_instance.children_directory.remove_node(&child_entry)?.ok_or_else(|| {
            warn!(
                "child directory {} in parent instance {} was already removed or never added",
                child_entry, parent_moniker
            );
            ModelError::remove_entry_error(child_entry)
        })?;
        instance_map
            .remove(&target_moniker)
            .ok_or(ModelError::instance_not_found(target_moniker.clone()))?;

        Ok(())
    }

    /// Given a `CapabilitySource`, determine if it is a framework-provided
    /// hub capability. If so, update the given `capability_provider` to
    /// provide a hub directory.
    async fn on_capability_routed_async(
        self: Arc<Self>,
        source: CapabilitySource,
        capability_provider: Arc<Mutex<Option<Box<dyn CapabilityProvider>>>>,
    ) -> Result<(), ModelError> {
        // If this is a scoped framework directory capability, then check the source path
        if let CapabilitySource::Framework {
            capability: InternalCapability::Directory(source_name),
            component,
        } = source
        {
            if source_name.str() != "hub" {
                return Ok(());
            }

            // Set the capability provider, if not already set.
            let mut capability_provider = capability_provider.lock().await;
            if capability_provider.is_none() {
                *capability_provider = Some(Box::new(HubCapabilityProvider::new(
                    component.abs_moniker.clone(),
                    self.clone(),
                )))
            }
        }
        Ok(())
    }
}

#[async_trait]
impl Hook for Hub {
    async fn on(self: Arc<Self>, event: &Event) -> Result<(), ModelError> {
        let target_moniker = event
            .target_moniker
            .unwrap_instance_moniker_or(ModelError::UnexpectedComponentManagerMoniker)?;
        match &event.result {
            Ok(EventPayload::CapabilityRouted { source, capability_provider }) => {
                self.on_capability_routed_async(source.clone(), capability_provider.clone())
                    .await?;
            }
            Ok(EventPayload::Discovered { instance_id }) => {
                self.on_discovered_async(
                    target_moniker,
                    event.component_url.to_string(),
                    *instance_id,
                )
                .await?;
            }
            Ok(EventPayload::Unresolved) => {
                self.on_unresolved_async(target_moniker, event.component_url.to_string()).await?;
            }
            Ok(EventPayload::Destroyed) => {
                self.on_destroyed_async(target_moniker).await?;
            }
            Ok(EventPayload::Started { component, runtime, component_decl, start_reason }) => {
                self.on_started_async(
                    target_moniker,
                    component,
                    &runtime,
                    &component_decl,
                    start_reason,
                )
                .await?;
            }
            Ok(EventPayload::Resolved { component, resolved_url, decl, config, package_dir }) => {
                self.on_resolved_async(
                    target_moniker,
                    component,
                    resolved_url.clone(),
                    &decl,
                    &config,
                    package_dir.as_ref(),
                )
                .await?;
            }
            Ok(EventPayload::Stopped { .. }) => {
                self.on_stopped_async(target_moniker).await?;
            }
            _ => {}
        };
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{
            builtin_environment::BuiltinEnvironment,
            model::{
                component::StartReason,
                model::Model,
                starter::Starter,
                testing::{
                    test_helpers::{
                        component_decl_with_test_runner, dir_contains, list_directory,
                        list_directory_recursive, list_sub_directory, read_file,
                        TestEnvironmentBuilder, TestModelResult,
                    },
                    test_hook::HubInjectionTestHook,
                },
            },
        },
        assert_matches::assert_matches,
        cm_rust::{
            self, Availability, CapabilityName, CapabilityPath, ComponentDecl, ConfigChecksum,
            ConfigDecl, ConfigField, ConfigNestedValueType, ConfigValueSource, ConfigValueType,
            DependencyType, DirectoryDecl, EventSubscription, ExposeDecl, ExposeDirectoryDecl,
            ExposeProtocolDecl, ExposeSource, ExposeTarget, ProtocolDecl, SingleValue, UseDecl,
            UseDirectoryDecl, UseEventDecl, UseEventStreamDeprecatedDecl, UseProtocolDecl,
            UseSource, Value, ValueSpec, ValuesData, VectorValue,
        },
        cm_rust_testing::ComponentDeclBuilder,
        fidl::endpoints::ServerEnd,
        fidl_fuchsia_io as fio,
        moniker::AbsoluteMoniker,
        routing_test_helpers::component_id_index::make_index_file,
        std::{convert::TryFrom, path::Path},
        vfs::{
            directory::entry::DirectoryEntry, execution_scope::ExecutionScope,
            file::vmo::asynchronous::read_only_static, path::Path as pfsPath, pseudo_directory,
        },
    };

    /// Hosts an out directory with a 'foo' file.
    fn foo_out_dir_fn() -> Box<dyn Fn(ServerEnd<fio::DirectoryMarker>) + Send + Sync> {
        Box::new(move |server_end: ServerEnd<fio::DirectoryMarker>| {
            let out_dir = pseudo_directory!(
                "foo" => read_only_static(b"bar"),
                "test" => pseudo_directory!(
                    "aaa" => read_only_static(b"bbb"),
                ),
            );

            out_dir.clone().open(
                ExecutionScope::new(),
                fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
                fio::MODE_TYPE_DIRECTORY,
                pfsPath::dot(),
                ServerEnd::new(server_end.into_channel()),
            );
        })
    }

    /// Hosts a runtime directory with a 'bleep' file.
    fn bleep_runtime_dir_fn() -> Box<dyn Fn(ServerEnd<fio::DirectoryMarker>) + Send + Sync> {
        Box::new(move |server_end: ServerEnd<fio::DirectoryMarker>| {
            let pseudo_dir = pseudo_directory!(
                "bleep" => read_only_static(b"blah"),
            );

            pseudo_dir.clone().open(
                ExecutionScope::new(),
                fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
                fio::MODE_TYPE_DIRECTORY,
                pfsPath::dot(),
                ServerEnd::new(server_end.into_channel()),
            );
        })
    }

    type DirectoryCallback = Box<dyn Fn(ServerEnd<fio::DirectoryMarker>) + Send + Sync>;

    struct ComponentDescriptor {
        pub name: &'static str,
        pub decl: ComponentDecl,
        pub config: Option<(&'static str, ValuesData)>,
        pub host_fn: Option<DirectoryCallback>,
        pub runtime_host_fn: Option<DirectoryCallback>,
    }

    async fn start_component_manager_with_hub(
        root_component_url: String,
        components: Vec<ComponentDescriptor>,
    ) -> (Arc<Model>, Arc<Mutex<BuiltinEnvironment>>, fio::DirectoryProxy) {
        start_component_manager_with_options(root_component_url, components, vec![], None).await
    }

    async fn start_component_manager_with_options(
        root_component_url: String,
        components: Vec<ComponentDescriptor>,
        additional_hooks: Vec<HooksRegistration>,
        index_file_path: Option<String>,
    ) -> (Arc<Model>, Arc<Mutex<BuiltinEnvironment>>, fio::DirectoryProxy) {
        let resolved_root_component_url = format!("{}_resolved", root_component_url);
        let decls = components.iter().map(|c| (c.name, c.decl.clone())).collect();
        let configs = components.iter().filter_map(|c| c.config.clone()).collect();

        let TestModelResult { model, builtin_environment, mock_runner, .. } =
            TestEnvironmentBuilder::new()
                .set_components(decls)
                .set_config_values(configs)
                .set_component_id_index_path(index_file_path)
                .build()
                .await;

        for component in components.into_iter() {
            if let Some(host_fn) = component.host_fn {
                mock_runner.add_host_fn(&resolved_root_component_url, host_fn);
            }

            if let Some(runtime_host_fn) = component.runtime_host_fn {
                mock_runner.add_runtime_host_fn(&resolved_root_component_url, runtime_host_fn);
            }
        }

        let (hub_proxy, server_end) =
            fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
        let server_end = server_end.into_channel();

        builtin_environment
            .lock()
            .await
            .hub
            .as_ref()
            .unwrap()
            .open_root(fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE, server_end)
            .await
            .unwrap();

        model.root().hooks.install(additional_hooks).await;

        let root_moniker = AbsoluteMoniker::root();
        model.start_instance(&root_moniker, &StartReason::Root).await.unwrap();

        (model, builtin_environment, hub_proxy)
    }

    #[fuchsia::test]
    async fn hub_basic() {
        let root_component_url = "test:///root".to_string();
        let (_model, _builtin_environment, hub_proxy) = start_component_manager_with_hub(
            root_component_url.clone(),
            vec![
                ComponentDescriptor {
                    name: "root",
                    decl: ComponentDeclBuilder::new().add_lazy_child("a").build(),
                    config: None,
                    host_fn: None,
                    runtime_host_fn: None,
                },
                ComponentDescriptor {
                    name: "a",
                    decl: component_decl_with_test_runner(),
                    config: None,
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

        assert_eq!(
            format!("{}", StartReason::Root),
            read_file(&hub_proxy, "exec/start_reason").await
        );

        // Verify IDs
        assert_eq!("0", read_file(&hub_proxy, "id").await);
        assert_eq!("0", read_file(&hub_proxy, "children/a/id").await);

        // Verify Component Type
        assert_eq!("static", read_file(&hub_proxy, "component_type").await);
        assert_eq!("static", read_file(&hub_proxy, "children/a/component_type").await);

        // Verify Moniker Files
        // AbsoluteMoniker::root() has an empty moniker file.
        assert_eq!("", read_file(&hub_proxy, "moniker").await);
        assert_eq!("a", read_file(&hub_proxy, "children/a/moniker").await);

        assert_eq!("test:///a", read_file(&hub_proxy, "children/a/url").await);
    }

    #[fuchsia::test]
    async fn hub_child_dir_name() {
        // the maximum length for an instance name
        let max_dir_len = usize::try_from(fidl_fuchsia_io::MAX_NAME_LENGTH).unwrap();
        // the maximum length for a moniker (without the collection name)
        let max_name_len = max_dir_len - "coll:".len();

        // test long moniker that exceeds the MAX_NAME_LENGTH is truncated
        let long_moniker = format!("coll:{}", "a".repeat(max_name_len + 1));
        let first_entry = Hub::child_dir_name(&long_moniker, 12345);
        assert_eq!(first_entry.len(), max_dir_len);
        assert_eq!(
            first_entry,
            "coll:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\
            aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\
            aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\
            aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\
            aaaaaaaaaaaaaaa#3039"
        );

        // test long monikers with slight variations generate unique dir names
        let different = format!("{}b", long_moniker);
        let second_entry = Hub::child_dir_name(&different, 67890);
        assert_eq!(second_entry.len(), max_dir_len);
        assert_eq!(
            second_entry,
            "coll:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\
            aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\
            aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\
            aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\
            aaaaaaaaaaaaaa#10932"
        );

        // test moniker meets the MAX_NAME_LENGTH is not truncated
        let meets_max = format!("coll:{}", "a".repeat(max_name_len));
        let entry_name = Hub::child_dir_name(&meets_max, 0);
        assert_eq!(entry_name.len(), max_dir_len);
        assert_eq!(entry_name, meets_max);

        // test moniker less than the MAX_NAME_LENGTH is not truncated
        let less_than_max = format!("coll:{}", "a".repeat(max_name_len - 1));
        let entry_name = Hub::child_dir_name(&less_than_max, 0);
        assert!(entry_name.len() < max_dir_len);
        assert_eq!(entry_name, less_than_max);
    }

    #[fuchsia::test]
    async fn hub_out_directory() {
        let root_component_url = "test:///root".to_string();
        let (_model, _builtin_environment, hub_proxy) = start_component_manager_with_hub(
            root_component_url.clone(),
            vec![ComponentDescriptor {
                name: "root",
                decl: ComponentDeclBuilder::new().add_lazy_child("a").build(),
                config: None,
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

    #[fuchsia::test]
    async fn hub_runtime_directory() {
        let root_component_url = "test:///root".to_string();
        let (_model, _builtin_environment, hub_proxy) = start_component_manager_with_hub(
            root_component_url.clone(),
            vec![ComponentDescriptor {
                name: "root",
                decl: ComponentDeclBuilder::new().add_lazy_child("a").build(),
                config: None,
                host_fn: None,
                runtime_host_fn: Some(bleep_runtime_dir_fn()),
            }],
        )
        .await;

        assert_eq!("blah", read_file(&hub_proxy, "exec/runtime/bleep").await);
    }

    #[fuchsia::test]
    async fn hub_test_hook_interception() {
        let root_component_url = "test:///root".to_string();
        let hub_injection_test_hook = Arc::new(HubInjectionTestHook::new());
        let (_model, _builtin_environment, hub_proxy) = start_component_manager_with_options(
            root_component_url.clone(),
            vec![ComponentDescriptor {
                name: "root",
                decl: ComponentDeclBuilder::new()
                    .add_lazy_child("a")
                    .use_(UseDecl::Directory(UseDirectoryDecl {
                        dependency_type: DependencyType::Strong,
                        source: UseSource::Framework,
                        source_name: "hub".into(),
                        target_path: CapabilityPath::try_from("/hub").unwrap(),
                        rights: *routing::rights::READ_RIGHTS,
                        subdir: None,
                        availability: Availability::Required,
                    }))
                    .build(),
                config: None,
                host_fn: None,
                runtime_host_fn: None,
            }],
            hub_injection_test_hook.hooks(),
            None,
        )
        .await;

        assert_eq!(vec!["hub", "pkg"], list_sub_directory(&hub_proxy, "exec/in").await);

        assert_eq!(vec!["fake_file"], list_sub_directory(&hub_proxy, "exec/in/pkg").await);

        assert_eq!(vec!["old_hub"], list_sub_directory(&hub_proxy, "exec/in/hub").await);

        assert_eq!(
            vec!["expose", "in", "out", "resolved_url", "runtime", "start_reason"],
            list_sub_directory(&hub_proxy, "exec/in/hub/old_hub/exec").await
        );
    }

    #[fuchsia::test]
    async fn hub_config_dir_in_resolved() {
        let root_component_url = "test:///root".to_string();
        let checksum = ConfigChecksum::Sha256([
            0x07, 0xA8, 0xE6, 0x85, 0xC8, 0x79, 0xA9, 0x79, 0xC3, 0x26, 0x17, 0xDC, 0x4E, 0x74,
            0x65, 0x7F, 0xF1, 0xF7, 0x73, 0xE7, 0x12, 0xEE, 0x51, 0xFD, 0xF6, 0x57, 0x43, 0x07,
            0xA7, 0xAF, 0x2E, 0x64,
        ]);

        let (_model, _builtin_environment, hub_proxy) = start_component_manager_with_hub(
            root_component_url.clone(),
            vec![ComponentDescriptor {
                name: "root",
                decl: ComponentDeclBuilder::new()
                    .add_config(ConfigDecl {
                        fields: vec![
                            ConfigField {
                                key: "logging".to_string(),
                                type_: ConfigValueType::Bool,
                            },
                            ConfigField {
                                key: "verbosity".to_string(),
                                type_: ConfigValueType::String { max_size: 10 },
                            },
                            ConfigField {
                                key: "tags".to_string(),
                                type_: ConfigValueType::Vector {
                                    max_count: 10,
                                    nested_type: ConfigNestedValueType::String { max_size: 20 },
                                },
                            },
                        ],
                        checksum: checksum.clone(),
                        value_source: ConfigValueSource::PackagePath("meta/root.cvf".into()),
                    })
                    .build(),
                config: Some((
                    "meta/root.cvf",
                    ValuesData {
                        values: vec![
                            ValueSpec { value: Value::Single(SingleValue::Bool(true)) },
                            ValueSpec {
                                value: Value::Single(SingleValue::String("DEBUG".to_string())),
                            },
                            ValueSpec {
                                value: Value::Vector(VectorValue::StringVector(vec![
                                    "foo".into(),
                                    "bar".into(),
                                ])),
                            },
                        ],
                        checksum,
                    },
                )),
                host_fn: None,
                runtime_host_fn: None,
            }],
        )
        .await;

        let config_dir = fuchsia_fs::open_directory(
            &hub_proxy,
            &Path::new("resolved/config"),
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        )
        .expect("Failed to open directory");
        assert_eq!(vec!["logging", "tags", "verbosity"], list_directory(&config_dir).await);
        assert_eq!("true", read_file(&config_dir, "logging").await);
        assert_eq!("\"DEBUG\"", read_file(&config_dir, "verbosity").await);
        assert_eq!("[\"foo\", \"bar\"]", read_file(&config_dir, "tags").await);
    }

    #[fuchsia::test]
    async fn hub_resolved_directory() {
        let root_component_url = "test:///root".to_string();
        let (_model, _builtin_environment, hub_proxy) = start_component_manager_with_hub(
            root_component_url.clone(),
            vec![ComponentDescriptor {
                name: "root",
                decl: ComponentDeclBuilder::new()
                    .add_lazy_child("a")
                    .use_(UseDecl::Directory(UseDirectoryDecl {
                        dependency_type: DependencyType::Strong,
                        source: UseSource::Framework,
                        source_name: "hub".into(),
                        target_path: CapabilityPath::try_from("/hub").unwrap(),
                        rights: *routing::rights::READ_RIGHTS,
                        subdir: Some("resolved".into()),
                        availability: Availability::Required,
                    }))
                    .build(),
                config: None,
                host_fn: None,
                runtime_host_fn: None,
            }],
        )
        .await;

        let resolved_dir = fuchsia_fs::open_directory(
            &hub_proxy,
            &Path::new("resolved"),
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        )
        .expect("Failed to open directory");
        assert_eq!(vec!["expose", "resolved_url", "use"], list_directory(&resolved_dir).await);
        assert_eq!(
            format!("{}_resolved", root_component_url),
            read_file(&hub_proxy, "resolved/resolved_url").await
        );
    }
    #[fuchsia::test]
    #[should_panic]
    async fn hub_resolved_directory_exists() {
        let root_component_url = "test:///root".to_string();
        let (_model, builtin_environment, hub_proxy) = start_component_manager_with_hub(
            root_component_url.clone(),
            vec![ComponentDescriptor {
                name: "root",
                decl: ComponentDeclBuilder::new()
                    .add_lazy_child("a")
                    .use_(UseDecl::Directory(UseDirectoryDecl {
                        dependency_type: DependencyType::Strong,
                        source: UseSource::Framework,
                        source_name: "hub".into(),
                        target_path: CapabilityPath::try_from("/hub").unwrap(),
                        rights: *routing::rights::READ_RIGHTS,
                        subdir: Some("resolved".into()),
                        availability: Availability::Required,
                    }))
                    .build(),
                config: None,
                host_fn: None,
                runtime_host_fn: None,
            }],
        )
        .await;

        let resolved_dir = fuchsia_fs::open_directory(
            &hub_proxy,
            &Path::new("resolved"),
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        )
        .expect("Failed to open directory");
        assert_eq!(vec!["expose", "resolved_url", "use"], list_directory(&resolved_dir).await);
        assert_eq!(
            format!("{}_resolved", root_component_url),
            read_file(&hub_proxy, "resolved/resolved_url").await
        );

        // Call on_unresolved_async() as if the component was unresolved and reset to
        // DiscoveredState.
        let guard = &builtin_environment.lock().await;
        let hub = guard.hub.as_ref().unwrap();
        let new_url = "test:///foo".to_string();
        let moniker = AbsoluteMoniker::parse_str("/").unwrap();
        assert_matches!(hub.on_unresolved_async(&moniker, new_url).await, Ok(()));

        // Confirm that the resolved directory was deleted.
        let resolved_dir2 = fuchsia_fs::open_directory(
            &hub_proxy,
            &Path::new("resolved"),
            fio::OpenFlags::RIGHT_READABLE,
        )
        .expect("Failed to open directory");
        // The directory existed when on_discovered_async() was called, so was deleted. Listing
        // it will induce a panic which this test expects.
        list_directory(&resolved_dir2).await;
    }

    #[fuchsia::test]
    async fn hub_use_directory_in_resolved() {
        let root_component_url = "test:///root".to_string();
        let (_model, _builtin_environment, hub_proxy) = start_component_manager_with_hub(
            root_component_url.clone(),
            vec![ComponentDescriptor {
                name: "root",
                decl: ComponentDeclBuilder::new()
                    .add_lazy_child("a")
                    .use_(UseDecl::Directory(UseDirectoryDecl {
                        dependency_type: DependencyType::Strong,
                        source: UseSource::Framework,
                        source_name: "hub".into(),
                        target_path: CapabilityPath::try_from("/hub").unwrap(),
                        rights: *routing::rights::READ_RIGHTS,
                        subdir: None,
                        availability: Availability::Required,
                    }))
                    .build(),
                config: None,
                host_fn: None,
                runtime_host_fn: None,
            }],
        )
        .await;

        let use_dir = fuchsia_fs::open_directory(
            &hub_proxy,
            &Path::new("resolved/use"),
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        )
        .expect("Failed to open directory");

        assert_eq!(vec!["hub", "pkg"], list_directory(&use_dir).await);

        assert_eq!(vec!["fake_file"], list_sub_directory(&use_dir, "pkg").await);

        assert_eq!(
            vec!["children", "component_type", "exec", "id", "moniker", "resolved", "url"],
            list_sub_directory(&use_dir, "hub").await
        );
    }

    #[fuchsia::test]
    async fn hub_instance_id_in_resolved() {
        // Create index.
        let iid = format!("1234{}", "5".repeat(60));
        let index_file = make_index_file(component_id_index::Index {
            instances: vec![component_id_index::InstanceIdEntry {
                instance_id: Some(iid.clone()),
                appmgr_moniker: None,
                moniker: Some(AbsoluteMoniker::parse_str("/a").unwrap()),
            }],
            ..component_id_index::Index::default()
        })
        .unwrap();

        let root_component_url = "test:///root".to_string();
        let (model, _builtin_environment, hub_proxy) = start_component_manager_with_options(
            root_component_url.clone(),
            vec![
                ComponentDescriptor {
                    name: "root",
                    decl: ComponentDeclBuilder::new()
                        .add_lazy_child("a")
                        .use_(UseDecl::Directory(UseDirectoryDecl {
                            dependency_type: DependencyType::Strong,
                            source: UseSource::Framework,
                            source_name: "hub".into(),
                            target_path: CapabilityPath::try_from("/hub").unwrap(),
                            rights: *routing::rights::READ_RIGHTS,
                            subdir: Some("resolved".into()),
                            availability: Availability::Required,
                        }))
                        .build(),
                    config: None,
                    host_fn: None,
                    runtime_host_fn: None,
                },
                ComponentDescriptor {
                    name: "a",
                    decl: component_decl_with_test_runner(),
                    config: None,
                    host_fn: None,
                    runtime_host_fn: None,
                },
            ],
            vec![],
            index_file.path().to_str().map(str::to_string),
        )
        .await;

        // Starting will resolve the component and cause the instance id to be written.
        model
            .start_instance(&AbsoluteMoniker::parse_str("/a").unwrap(), &StartReason::Debug)
            .await
            .unwrap();
        let resolved_dir = fuchsia_fs::open_directory(
            &hub_proxy,
            &Path::new("children/a/resolved"),
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        )
        .expect("Failed to open directory");

        // Confirm that the instance_id is read and written to file in resolved directory.
        assert_eq!(iid, read_file(&resolved_dir, "instance_id").await);
    }

    #[fuchsia::test]
    // TODO(b/65870): change function name to hub_expose_directory after the expose directory
    // is removed from exec and the original hub_expose_directory test is deleted.
    async fn hub_expose_directory_in_resolved() {
        let root_component_url = "test:///root".to_string();
        let (_model, _builtin_environment, hub_proxy) = start_component_manager_with_hub(
            root_component_url.clone(),
            vec![ComponentDescriptor {
                name: "root",
                decl: ComponentDeclBuilder::new()
                    .add_lazy_child("a")
                    .protocol(ProtocolDecl {
                        name: "foo".into(),
                        source_path: Some("/svc/foo".parse().unwrap()),
                    })
                    .directory(DirectoryDecl {
                        name: "baz".into(),
                        source_path: Some("/data".parse().unwrap()),
                        rights: *routing::rights::READ_RIGHTS,
                    })
                    .expose(ExposeDecl::Protocol(ExposeProtocolDecl {
                        source: ExposeSource::Self_,
                        source_name: "foo".into(),
                        target_name: "bar".into(),
                        target: ExposeTarget::Parent,
                    }))
                    .expose(ExposeDecl::Directory(ExposeDirectoryDecl {
                        source: ExposeSource::Self_,
                        source_name: "baz".into(),
                        target_name: "hippo".into(),
                        target: ExposeTarget::Parent,
                        rights: None,
                        subdir: None,
                    }))
                    .build(),
                config: None,
                host_fn: None,
                runtime_host_fn: None,
            }],
        )
        .await;

        let expose_dir = fuchsia_fs::open_directory(
            &hub_proxy,
            &Path::new("resolved/expose"),
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        )
        .expect("Failed to open directory");
        assert_eq!(vec!["bar", "hippo"], list_directory_recursive(&expose_dir).await);
    }

    #[fuchsia::test]
    async fn hub_in_directory() {
        let root_component_url = "test:///root".to_string();
        let (_model, _builtin_environment, hub_proxy) = start_component_manager_with_hub(
            root_component_url.clone(),
            vec![ComponentDescriptor {
                name: "root",
                decl: ComponentDeclBuilder::new()
                    .add_lazy_child("a")
                    .use_(UseDecl::Directory(UseDirectoryDecl {
                        source: UseSource::Framework,
                        source_name: "hub".into(),
                        target_path: CapabilityPath::try_from("/hub").unwrap(),
                        rights: *routing::rights::READ_RIGHTS,
                        subdir: Some("exec".into()),
                        dependency_type: DependencyType::Strong,
                        availability: Availability::Required,
                    }))
                    .use_(UseDecl::Protocol(UseProtocolDecl {
                        source: UseSource::Parent,
                        source_name: "baz-svc".into(),
                        target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                        dependency_type: DependencyType::Strong,
                        availability: Availability::Required,
                    }))
                    .use_(UseDecl::Directory(UseDirectoryDecl {
                        source: UseSource::Parent,
                        source_name: "foo-dir".into(),
                        target_path: CapabilityPath::try_from("/data/bar").unwrap(),
                        rights: *routing::rights::READ_RIGHTS | *routing::rights::WRITE_RIGHTS,
                        subdir: None,
                        dependency_type: DependencyType::Strong,
                        availability: Availability::Required,
                    }))
                    .build(),
                config: None,
                host_fn: None,
                runtime_host_fn: None,
            }],
        )
        .await;

        assert_eq!(
            vec!["data", "hub", "pkg", "svc"],
            list_sub_directory(&hub_proxy, "exec/in").await
        );

        assert_eq!(vec!["fake_file"], list_sub_directory(&hub_proxy, "exec/in/pkg").await);

        assert_eq!(
            vec!["expose", "in", "out", "resolved_url", "runtime", "start_reason"],
            list_sub_directory(&hub_proxy, "exec/in/hub").await
        );
    }

    #[fuchsia::test]
    async fn hub_no_event_stream_in_incoming_directory() {
        let root_component_url = "test:///root".to_string();
        let (_model, _builtin_environment, hub_proxy) = start_component_manager_with_hub(
            root_component_url.clone(),
            vec![ComponentDescriptor {
                name: "root",
                decl: ComponentDeclBuilder::new()
                    .add_lazy_child("a")
                    .use_(UseDecl::Event(UseEventDecl {
                        dependency_type: DependencyType::Strong,
                        source: UseSource::Framework,
                        source_name: "started".into(),
                        target_name: "started".into(),
                        filter: None,
                        availability: Availability::Required,
                    }))
                    .use_(UseDecl::EventStreamDeprecated(UseEventStreamDeprecatedDecl {
                        name: CapabilityName::try_from("EventStream").unwrap(),
                        subscriptions: vec![EventSubscription {
                            event_name: "started".to_string(),
                        }],
                        availability: Availability::Required,
                    }))
                    .build(),
                config: None,
                host_fn: None,
                runtime_host_fn: None,
            }],
        )
        .await;

        let in_dir = fuchsia_fs::open_directory(
            &hub_proxy,
            &Path::new("exec/in"),
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        )
        .expect("Failed to open directory");
        assert_eq!(vec!["pkg"], list_directory(&in_dir).await);

        assert_eq!(vec!["fake_file"], list_sub_directory(&&in_dir, "pkg").await);
    }

    #[fuchsia::test]
    async fn hub_expose_directory() {
        let root_component_url = "test:///root".to_string();
        let (_model, _builtin_environment, hub_proxy) = start_component_manager_with_hub(
            root_component_url.clone(),
            vec![ComponentDescriptor {
                name: "root",
                decl: ComponentDeclBuilder::new()
                    .add_lazy_child("a")
                    .protocol(ProtocolDecl {
                        name: "foo".into(),
                        source_path: Some("/svc/foo".parse().unwrap()),
                    })
                    .directory(DirectoryDecl {
                        name: "baz".into(),
                        source_path: Some("/data".parse().unwrap()),
                        rights: *routing::rights::READ_RIGHTS,
                    })
                    .expose(ExposeDecl::Protocol(ExposeProtocolDecl {
                        source: ExposeSource::Self_,
                        source_name: "foo".into(),
                        target_name: "bar".into(),
                        target: ExposeTarget::Parent,
                    }))
                    .expose(ExposeDecl::Directory(ExposeDirectoryDecl {
                        source: ExposeSource::Self_,
                        source_name: "baz".into(),
                        target_name: "hippo".into(),
                        target: ExposeTarget::Parent,
                        rights: None,
                        subdir: None,
                    }))
                    .build(),
                config: None,
                host_fn: None,
                runtime_host_fn: None,
            }],
        )
        .await;

        let expose_dir = fuchsia_fs::open_directory(
            &hub_proxy,
            &Path::new("exec/expose"),
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        )
        .expect("Failed to open directory");
        assert_eq!(vec!["bar", "hippo"], list_directory_recursive(&expose_dir).await);
    }
}

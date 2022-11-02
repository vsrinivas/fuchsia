// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        capability::{CapabilityProvider, CapabilitySource},
        model::{
            addable_directory::AddableDirectoryWithResult,
            component::WeakComponentInstance,
            dir_tree::DirTree,
            error::ModelError,
            hooks::{Event, EventPayload, EventType, Hook, HooksRegistration, RuntimeInfo},
            routing_fns::{route_expose_fn, route_use_fn},
        },
    },
    ::routing::capability_source::InternalCapability,
    async_trait::async_trait,
    cm_rust::ComponentDecl,
    cm_task_scope::TaskScope,
    cm_util::{channel, io::clone_dir},
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
        execution_scope::ExecutionScope, path::Path as pfsPath, remote::remote_dir,
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
    pub is_running: bool,
    pub is_resolved: bool,
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
    pub fn new() -> Result<Self, ModelError> {
        let mut instance_map = HashMap::new();
        let moniker = AbsoluteMoniker::root();

        Hub::add_instance_if_necessary(&moniker, &mut instance_map)?
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
        instance_map: &mut HashMap<AbsoluteMoniker, Instance>,
    ) -> Result<Option<(u128, Directory)>, ModelError> {
        if instance_map.contains_key(&moniker) {
            return Ok(None);
        }

        let instance = pfs::simple();

        // Add a children directory.
        let children = pfs::simple();
        instance.add_node("children", children.clone(), moniker)?;

        let mut rng = rand::thread_rng();
        let instance_uuid: u128 = rng.gen();

        instance_map.insert(
            moniker.clone(),
            Instance {
                is_running: false,
                is_resolved: false,
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
        mut instance_map: &'a mut HashMap<AbsoluteMoniker, Instance>,
    ) -> Result<(), ModelError> {
        let (uuid, controlled) = match Hub::add_instance_if_necessary(moniker, &mut instance_map)? {
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

    async fn on_resolved_async<'a>(
        &self,
        target_moniker: &AbsoluteMoniker,
        target: &WeakComponentInstance,
        component_decl: &'a ComponentDecl,
        pkg_dir: Option<&fio::DirectoryProxy>,
    ) -> Result<(), ModelError> {
        let mut instance_map = self.instances.lock().await;

        let instance = instance_map
            .get_mut(target_moniker)
            .ok_or(ModelError::instance_not_found(target_moniker.clone()))?;

        // The component must not be resolved already.
        assert!(!instance.is_resolved);

        // Add the `namespace` directory
        let tree = DirTree::build_from_uses(route_use_fn, target.clone(), &component_decl);
        let mut ns_dir = pfs::simple();
        tree.install(target_moniker, &mut ns_dir)?;
        if let Some(pkg_dir) = clone_dir(pkg_dir) {
            ns_dir.add_node("pkg", remote_dir(pkg_dir), target_moniker)?;
        }
        instance.directory.add_node("ns", ns_dir, target_moniker)?;

        // Add the `exposed` directory
        let tree = DirTree::build_from_exposes(route_expose_fn, target.clone(), &component_decl);
        let mut expose_dir = pfs::simple();
        tree.install(target_moniker, &mut expose_dir)?;
        instance.directory.add_node("exposed", expose_dir, target_moniker)?;

        instance.is_resolved = true;

        Ok(())
    }

    async fn on_started_async<'a>(
        &'a self,
        target_moniker: &AbsoluteMoniker,
        runtime: &RuntimeInfo,
    ) -> Result<(), ModelError> {
        let mut instance_map = self.instances.lock().await;

        let instance = instance_map
            .get_mut(target_moniker)
            .ok_or(ModelError::instance_not_found(target_moniker.clone()))?;

        if instance.is_running {
            return Ok(());
        }

        // Add the `out` dir, if it exists
        if let Some(out_dir) = clone_dir(runtime.outgoing_dir.as_ref()) {
            instance.directory.add_node("out", remote_dir(out_dir), target_moniker)?;
        }

        // Add the `runtime` dir, if it exists
        if let Some(runtime_dir) = clone_dir(runtime.runtime_dir.as_ref()) {
            instance.directory.add_node("runtime", remote_dir(runtime_dir), target_moniker)?;
        }

        instance.is_running = true;

        Ok(())
    }

    async fn on_discovered_async(
        &self,
        target_moniker: &AbsoluteMoniker,
    ) -> Result<(), ModelError> {
        let mut instance_map = self.instances.lock().await;

        Self::add_instance_to_parent_if_necessary(target_moniker, &mut instance_map).await?;
        Ok(())
    }

    async fn on_unresolved_async(
        &self,
        target_moniker: &AbsoluteMoniker,
        _component_url: String,
    ) -> Result<(), ModelError> {
        let mut instance_map = self.instances.lock().await;

        // The component has been unresolved. Remove the corresponding directories from the hub.
        let res = instance_map.get_mut(target_moniker);
        if let Some(instance) = res {
            if instance.is_resolved {
                // Remove `exposed` and `ns`.
                // `out` and `runtime` would have already been removed on stop.
                instance.directory.remove_node("exposed")?;
                instance.directory.remove_node("ns")?;
                instance.is_resolved = false;
            }
        }
        Ok(())
    }

    async fn on_stopped_async(&self, target_moniker: &AbsoluteMoniker) -> Result<(), ModelError> {
        let mut instance_map = self.instances.lock().await;
        let mut instance = instance_map
            .get_mut(target_moniker)
            .ok_or(ModelError::instance_not_found(target_moniker.clone()))?;

        // Remove `out` and `runtime` if they exist.
        instance.directory.remove_node("out")?;
        instance.directory.remove_node("runtime")?;
        instance.is_running = false;
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
            Ok(EventPayload::Discovered { .. }) => {
                self.on_discovered_async(target_moniker).await?;
            }
            Ok(EventPayload::Unresolved) => {
                self.on_unresolved_async(target_moniker, event.component_url.to_string()).await?;
            }
            Ok(EventPayload::Destroyed) => {
                self.on_destroyed_async(target_moniker).await?;
            }
            Ok(EventPayload::Started { runtime, .. }) => {
                self.on_started_async(target_moniker, &runtime).await?;
            }
            Ok(EventPayload::Resolved { component, decl, package_dir, .. }) => {
                self.on_resolved_async(target_moniker, component, &decl, package_dir.as_ref())
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
                testing::test_helpers::{
                    dir_contains, list_directory, list_directory_recursive, list_sub_directory,
                    read_file, TestEnvironmentBuilder, TestModelResult,
                },
            },
        },
        cm_rust::{
            self, Availability, CapabilityName, CapabilityPath, ComponentDecl, DependencyType,
            DirectoryDecl, EventSubscription, ExposeDecl, ExposeDirectoryDecl, ExposeProtocolDecl,
            ExposeSource, ExposeTarget, ProtocolDecl, UseDecl, UseDirectoryDecl, UseEventDecl,
            UseEventStreamDeprecatedDecl, UseSource, ValuesData,
        },
        cm_rust_testing::ComponentDeclBuilder,
        fidl::endpoints::ServerEnd,
        fidl_fuchsia_io as fio,
        moniker::AbsoluteMoniker,
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
            vec![ComponentDescriptor {
                name: "root",
                decl: ComponentDeclBuilder::new()
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
        assert_eq!(
            vec!["children", "exposed", "ns", "out", "runtime"],
            list_directory(&hub_proxy).await
        );
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

        assert!(dir_contains(&hub_proxy, "out", "foo").await);
        assert!(dir_contains(&hub_proxy, "out/test", "aaa").await);
        assert_eq!("bar", read_file(&hub_proxy, "out/foo").await);
        assert_eq!("bbb", read_file(&hub_proxy, "out/test/aaa").await);
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

        assert_eq!("blah", read_file(&hub_proxy, "runtime/bleep").await);
    }

    #[fuchsia::test]
    async fn hub_ns_directory() {
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

        let ns_dir = fuchsia_fs::open_directory(
            &hub_proxy,
            &Path::new("ns"),
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        )
        .expect("Failed to open directory");

        assert_eq!(vec!["hub", "pkg"], list_directory(&ns_dir).await);

        assert_eq!(vec!["fake_file"], list_sub_directory(&ns_dir, "pkg").await);

        assert_eq!(
            vec!["children", "exposed", "ns", "out", "runtime"],
            list_sub_directory(&ns_dir, "hub").await
        );
    }

    #[fuchsia::test]
    async fn hub_exposed_directory() {
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
            &Path::new("exposed"),
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        )
        .expect("Failed to open directory");
        assert_eq!(vec!["bar", "hippo"], list_directory_recursive(&expose_dir).await);
    }

    #[fuchsia::test]
    async fn hub_no_event_stream_in_ns_directory() {
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

        let ns_dir = fuchsia_fs::open_directory(
            &hub_proxy,
            &Path::new("ns"),
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        )
        .expect("Failed to open directory");
        assert_eq!(vec!["pkg"], list_directory(&ns_dir).await);

        assert_eq!(vec!["fake_file"], list_sub_directory(&ns_dir, "pkg").await);
    }
}

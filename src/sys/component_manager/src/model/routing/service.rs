// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        capability::CapabilityProvider,
        model::{
            component::{ComponentInstance, WeakComponentInstance},
            error::ModelError,
            routing::{open_capability_at_source, OpenRequest},
        },
    },
    ::routing::{
        capability_source::AggregateCapabilityProvider,
        component_instance::ComponentInstanceInterface,
    },
    async_trait::async_trait,
    cm_task_scope::TaskScope,
    cm_util::channel,
    fidl::{endpoints::ServerEnd, epitaph::ChannelEpitaphExt},
    fidl_fuchsia_io as fio, fuchsia_zircon as zx,
    moniker::AbsoluteMoniker,
    std::{collections::HashMap, collections::HashSet, path::PathBuf, sync::Arc},
    tracing::{error, warn},
    vfs::{
        common::send_on_open_with_error,
        directory::{
            connection::io1::DerivedConnection,
            dirents_sink,
            entry::{DirectoryEntry, EntryInfo},
            entry_container::{Directory, DirectoryWatcher},
            immutable::connection::io1::ImmutableConnection,
            immutable::lazy,
            traversal_position::TraversalPosition,
        },
        execution_scope::ExecutionScope,
    },
};

/// Provides a Service capability where the target component only has access to a subset of the
/// Service instances exposed by the source component.
pub struct FilteredServiceProvider {
    /// Execution scope for requests to `dir`. This is the same scope
    /// as the one in `collection_component`'s resolved state.
    execution_scope: ExecutionScope,

    /// Set of service instance names that are available to the target component.
    source_instance_filter: HashSet<String>,

    /// Mapping of service instance names in the source component to new names in the target.
    instance_name_source_to_target: HashMap<String, Vec<String>>,

    /// The underlying un-filtered service capability provider.
    source_service_provider: Box<dyn CapabilityProvider>,
}

impl FilteredServiceProvider {
    pub async fn new(
        source_component: &Arc<ComponentInstance>,
        source_instances: Vec<String>,
        instance_name_source_to_target: HashMap<String, Vec<String>>,
        source_service_provider: Box<dyn CapabilityProvider>,
    ) -> Result<Self, ModelError> {
        let execution_scope =
            source_component.lock_resolved_state().await?.execution_scope().clone();
        Ok(FilteredServiceProvider {
            execution_scope,
            source_instance_filter: source_instances.into_iter().collect(),
            instance_name_source_to_target,
            source_service_provider,
        })
    }
}

/// The virtual directory implementation used by the FilteredServiceProvider to host the
/// filtered set of service instances available to the target component.
pub struct FilteredServiceDirectory {
    source_instance_filter: HashSet<String>,
    source_dir_proxy: fio::DirectoryProxy,
    instance_name_source_to_target: HashMap<String, Vec<String>>,
    instance_name_target_to_source: HashMap<String, String>,
}

impl FilteredServiceDirectory {
    /// Returns true if the requested path matches an allowed instance.
    pub fn path_matches_allowed_instance(self: &Self, target_path: &String) -> bool {
        if target_path.is_empty() {
            return false;
        }
        if self.source_instance_filter.is_empty() {
            // If an instance was renamed the original name shouldn't be usable.
            !self.instance_name_source_to_target.contains_key(target_path)
        } else {
            self.source_instance_filter.contains(target_path)
        }
    }
}

#[async_trait]
impl DirectoryEntry for FilteredServiceDirectory {
    fn open(
        self: Arc<Self>,
        scope: ExecutionScope,
        flags: fio::OpenFlags,
        mode: u32,
        path: vfs::path::Path,
        server_end: ServerEnd<fio::NodeMarker>,
    ) {
        if path.is_empty() {
            // If path is empty just connect to itself as a directory.
            ImmutableConnection::create_connection(scope, self, flags, server_end);
            return;
        }
        let input_path_string = path.clone().into_string();
        let service_instance_name =
            path.peek().map(ToString::to_string).expect("path should not be empty");
        if self.path_matches_allowed_instance(&service_instance_name) {
            let source_path = self
                .instance_name_target_to_source
                .get(&service_instance_name)
                .map_or(input_path_string, |source| {
                    // Valid paths are "<instance_name>" or "<instance_name>/<protocol_name>".
                    // If the incoming path is just a service instance name return the source component instance name.
                    if path.is_single_component() {
                        return source.to_string();
                    }
                    let mut mut_path = path.clone();
                    // If the incoming path is "<instance_name>/<protocol_name>" replace <instance_name> with the source component instance name.
                    mut_path.next();
                    // Since we check above if the path is a single component it's safe to unwrap here.
                    let protocol_name = mut_path.next().unwrap();
                    format!("{}/{}", source, protocol_name).to_string()
                });

            if let Err(e) = self.source_dir_proxy.open(flags, mode, &source_path, server_end) {
                error!(
                    error = %e,
                    path = source_path.as_str(),
                    "Error opening instance in FilteredServiceDirectory"
                );
            }
            return;
        } else {
            send_on_open_with_error(flags, server_end, zx::Status::NOT_FOUND);
        }
    }

    fn entry_info(&self) -> EntryInfo {
        EntryInfo::new(fio::INO_UNKNOWN, fio::DirentType::Directory)
    }
}

#[async_trait]
impl Directory for FilteredServiceDirectory {
    async fn read_dirents<'a>(
        &'a self,
        pos: &'a TraversalPosition,
        mut sink: Box<dyn dirents_sink::Sink>,
    ) -> Result<(TraversalPosition, Box<dyn dirents_sink::Sealed>), zx::Status> {
        let next_entry = match pos {
            TraversalPosition::End => {
                // Bail out early when there is no work to do.
                // This method is always called at least once with TraversalPosition::End.
                return Ok((TraversalPosition::End, sink.seal()));
            }
            TraversalPosition::Start => None,
            TraversalPosition::Name(entry) => Some(entry),
            TraversalPosition::Index(_) => panic!("TraversalPosition::Index is never used"),
        };
        match fuchsia_fs::directory::readdir(&self.source_dir_proxy).await {
            Ok(dirent_vec) => {
                for dirent in dirent_vec {
                    let entry_name = dirent.name;
                    // If entry_name is the same as one of the target values in an instance rename, skip it.
                    // we prefer target renames over original source instance names in the case of a conflict.
                    if let Some(source_name) = self.instance_name_target_to_source.get(&entry_name)
                    {
                        // If the source and target name are the same, then there is no conflict to resolve,
                        // and we allow the no-op source to target name translation to happen below.
                        if source_name != &entry_name {
                            continue;
                        }
                    }

                    let target_entry_name_vec = self
                        .instance_name_source_to_target
                        .get(&entry_name)
                        .map_or(vec![entry_name.clone()], |t_names| t_names.clone());

                    for target_entry_name in target_entry_name_vec {
                        // Only reveal allowed source instances,
                        if !self.path_matches_allowed_instance(&target_entry_name) {
                            continue;
                        }
                        if let Some(next_entry_name) = next_entry {
                            if target_entry_name < next_entry_name.to_string() {
                                continue;
                            }
                        }
                        sink = match sink.append(
                            &EntryInfo::new(fio::INO_UNKNOWN, fio::DirentType::Directory),
                            &target_entry_name,
                        ) {
                            dirents_sink::AppendResult::Ok(sink) => sink,
                            dirents_sink::AppendResult::Sealed(sealed) => {
                                // There is not enough space to return this entry. Record it as the next
                                // entry to start at for subsequent calls.
                                return Ok((
                                    TraversalPosition::Name(target_entry_name.clone()),
                                    sealed,
                                ));
                            }
                        }
                    }
                }
            }
            Err(error) => {
                warn!(%error, "Error reading the source components service directory");
                return Err(zx::Status::INTERNAL);
            }
        }
        return Ok((TraversalPosition::End, sink.seal()));
    }

    fn register_watcher(
        self: Arc<Self>,
        _scope: ExecutionScope,
        _mask: fio::WatchMask,
        _channel: DirectoryWatcher,
    ) -> Result<(), zx::Status> {
        // TODO(fxb/96023) implement watcher behavior.
        Err(zx::Status::NOT_SUPPORTED)
    }

    fn unregister_watcher(self: Arc<Self>, _key: usize) {
        // TODO(fxb/96023) implement watcher behavior.
    }

    /// Get this directory's attributes.
    /// The "mode" field will be filled in by the connection.
    async fn get_attrs(&self) -> Result<fio::NodeAttributes, zx::Status> {
        Ok(fio::NodeAttributes {
            mode: fio::MODE_TYPE_DIRECTORY,
            id: fio::INO_UNKNOWN,
            content_size: 0,
            storage_size: 0,
            link_count: 1,
            creation_time: 0,
            modification_time: 0,
        })
    }

    /// Called when the directory is closed.
    fn close(&self) -> Result<(), zx::Status> {
        Ok(())
    }
}

#[async_trait]
impl CapabilityProvider for FilteredServiceProvider {
    async fn open(
        mut self: Box<Self>,
        task_scope: TaskScope,
        flags: fio::OpenFlags,
        open_mode: u32,
        relative_path: PathBuf,
        server_end: &mut zx::Channel,
    ) -> Result<(), ModelError> {
        let relative_path_utf8 = relative_path
            .to_str()
            .ok_or_else(|| ModelError::path_is_not_utf8(relative_path.clone()))?;
        let relative_path_vfs = if relative_path_utf8.is_empty() {
            vfs::path::Path::dot()
        } else {
            vfs::path::Path::validate_and_split(relative_path_utf8)
                .map_err(|_| ModelError::path_invalid(relative_path_utf8))?
        };

        // create a remote directory referring to the unfiltered source service directory.
        let (source_service_proxy, source_service_server_end) =
            fidl::endpoints::create_proxy::<fio::DirectoryMarker>()
                .map_err(|e| ModelError::stream_creation_error(e))?;
        self.source_service_provider
            .open(
                task_scope,
                flags,
                open_mode,
                PathBuf::new(), //relative_path,
                &mut (source_service_server_end.into_channel()),
            )
            .await?;

        let instance_name_target_to_source = {
            let mut m = HashMap::new();
            // We want to ensure that there is a one-to-many mapping from
            // source to target names, with no target names being used for multiple source names.
            // This is validated by cm_fidl_validator, so we can safely panic and not proceed
            // if that is violated here.
            for (k, v) in self.instance_name_source_to_target.iter() {
                for name in v {
                    if m.insert(name.clone(), k.clone()).is_some() {
                        panic!(
                            "duplicate target name found in instance_name_source_to_target, name: {:?}",
                            v
                        );
                    }
                }
            }
            m
        };
        // Arc to FilteredServiceDirectory will stay in scope and will usable by the caller
        // because the underlying implementation of Arc<FilteredServiceDirectory>.open
        // does one of 3 things depending on the path argument.
        // 1. We open an actual instance, which forwards the open call to the directory entry in the source (original unfiltered) service providing instance.
        // 2. The path is unknown/ not in the directory and we error.
        // 3. we call ImmutableConnection::create_connection with self as one of the arguments.
        // In case 3 the directory will stay in scope until the server_end channel is closed.
        Arc::new(FilteredServiceDirectory {
            source_instance_filter: self.source_instance_filter,
            source_dir_proxy: source_service_proxy,
            instance_name_target_to_source,
            instance_name_source_to_target: self.instance_name_source_to_target,
        })
        .open(
            self.execution_scope.clone(),
            flags,
            open_mode,
            relative_path_vfs,
            ServerEnd::new(channel::take_channel(server_end)),
        );
        Ok(())
    }
}

/// Serves a Service directory that allows clients to list instances resulting from an aggregation of service offers
/// and to open instances.
///
pub struct AggregateServiceDirectoryProvider {
    /// Execution scope for requests to `dir`. This is the same scope
    /// as the one in `collection_component`'s resolved state.
    execution_scope: ExecutionScope,

    /// The directory that contains entries for all service instances
    /// across all of the aggregated source services.
    dir: Arc<lazy::Lazy<AggregateServiceDirectory>>,
}

impl AggregateServiceDirectoryProvider {
    pub async fn create(
        target: WeakComponentInstance,
        collection_component: &Arc<ComponentInstance>,
        provider: Box<dyn AggregateCapabilityProvider<ComponentInstance>>,
    ) -> Result<AggregateServiceDirectoryProvider, ModelError> {
        let execution_scope =
            collection_component.lock_resolved_state().await?.execution_scope().clone();
        let dir = lazy::lazy(AggregateServiceDirectory {
            target,
            collection_component: collection_component.abs_moniker().clone(),
            provider,
        });
        Ok(AggregateServiceDirectoryProvider { execution_scope, dir })
    }
}

#[async_trait]
impl CapabilityProvider for AggregateServiceDirectoryProvider {
    async fn open(
        self: Box<Self>,
        _task_scope: TaskScope,
        flags: fio::OpenFlags,
        open_mode: u32,
        relative_path: PathBuf,
        server_end: &mut zx::Channel,
    ) -> Result<(), ModelError> {
        let relative_path_utf8 = relative_path
            .to_str()
            .ok_or_else(|| ModelError::path_is_not_utf8(relative_path.clone()))?;
        let relative_path = if relative_path_utf8.is_empty() {
            vfs::path::Path::dot()
        } else {
            vfs::path::Path::validate_and_split(relative_path_utf8)
                .map_err(|_| ModelError::path_invalid(relative_path_utf8))?
        };
        self.dir.open(
            self.execution_scope.clone(),
            flags,
            open_mode,
            relative_path,
            ServerEnd::new(channel::take_channel(server_end)),
        );
        Ok(())
    }
}

/// A directory entry representing a service with multiple services as its source.
/// This directory is hosted by component_manager on behalf of the component which offered multiple sources of
/// the same service capability.
///
/// This directory can be accessed by components by opening `/svc/my.service/` in their
/// incoming namespace when they have a `use my.service` declaration in their manifest, and the
/// source of `my.service` is multiple services.
struct AggregateServiceDirectory {
    /// The original target of the capability route (the component that opened this directory).
    target: WeakComponentInstance,
    /// The moniker of the component hosting the collection.
    collection_component: AbsoluteMoniker,
    /// The provider that lists collection instances and performs routing to an instance.
    provider: Box<dyn AggregateCapabilityProvider<ComponentInstance>>,
}

#[async_trait]
impl lazy::LazyDirectory for AggregateServiceDirectory {
    async fn get_entry(&self, name: &str) -> Result<Arc<dyn DirectoryEntry>, zx::Status> {
        // Parse the entry name into its (component,instance) parts.
        // In the case of non-comma separated entries, treat the component and
        // instance name as the same.
        let (component, instance) = name.split_once(',').unwrap_or((name, name));
        Ok(Arc::new(ServiceInstanceDirectoryEntry {
            component: component.to_string(),
            instance: instance.to_string(),
            target: self.target.clone(),
            intermediate_component: self.collection_component.clone(),
            provider: self.provider.clone(),
        }))
    }

    async fn read_dirents<'a>(
        &'a self,
        pos: &'a TraversalPosition,
        mut sink: Box<dyn dirents_sink::Sink>,
    ) -> Result<(TraversalPosition, Box<dyn dirents_sink::Sealed>), zx::Status> {
        let next_entry = match pos {
            TraversalPosition::End => {
                // Bail out early when there is no work to do.
                // This method is always called at least once with TraversalPosition::End.
                return Ok((TraversalPosition::End, sink.seal()));
            }
            TraversalPosition::Start => None,
            TraversalPosition::Name(entry) => {
                // All generated filenames are guaranteed to have the ',' separator.
                entry.split_once(',').or(Some((entry.as_str(), entry.as_str())))
            }
            TraversalPosition::Index(_) => panic!("TraversalPosition::Index is never used"),
        };

        let target = self.target.upgrade().map_err(|e| e.as_zx_status())?;
        let mut instances =
            self.provider.list_instances().await.map_err(|_| zx::Status::INTERNAL)?;
        if instances.is_empty() {
            return Ok((TraversalPosition::End, sink.seal()));
        }

        // Sort to guarantee a stable iteration order.
        instances.sort();

        let (instances, mut next_instance) =
            if let Some((next_component, next_instance)) = next_entry {
                // Skip to the next entry. If the exact component is found, start there.
                // Otherwise start at the next component and clear any assumptions about
                // the next instance within that component.
                match instances.binary_search_by(|i| i.as_str().cmp(next_component)) {
                    Ok(idx) => (&instances[idx..], Some(next_instance)),
                    Err(idx) => (&instances[idx..], None),
                }
            } else {
                (&instances[0..], None)
            };

        for instance in instances {
            if let Ok(source) = self.provider.route_instance(&instance).await {
                let (proxy, server) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>()
                    .map_err(|_| zx::Status::INTERNAL)?;
                if let Ok(()) = open_capability_at_source(OpenRequest {
                    flags: fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
                    open_mode: fio::MODE_TYPE_DIRECTORY,
                    relative_path: PathBuf::new(),
                    source,
                    target: &target,
                    server_chan: &mut server.into_channel(),
                })
                .await
                {
                    if let Ok(mut dirents) = fuchsia_fs::directory::readdir(&proxy).await {
                        // Sort to guarantee a stable iteration order.
                        dirents.sort();

                        let dirents = if let Some(next_instance) = next_instance.take() {
                            // Skip to the next entry. If the exact instance is found, start there.
                            // Otherwise start at the next instance, assuming the missing one was removed.
                            match dirents.binary_search_by(|e| e.name.as_str().cmp(next_instance)) {
                                Ok(idx) | Err(idx) => &dirents[idx..],
                            }
                        } else {
                            &dirents[0..]
                        };

                        for dirent in dirents {
                            // Encode the (component,instance) tuple so that it can be represented in a single
                            // path segment. If the component and instance name are identical ignore comma separation.
                            // TODO(fxbug.dev/100985) Remove this entry name parsing scheme. Supporting component instance name
                            // prefixes is no longer necessary.
                            let entry_name = {
                                if instance == &dirent.name {
                                    instance.clone()
                                } else {
                                    format!("{},{}", &instance, &dirent.name)
                                }
                            };
                            sink = match sink.append(
                                &EntryInfo::new(fio::INO_UNKNOWN, fio::DirentType::Directory),
                                &entry_name,
                            ) {
                                dirents_sink::AppendResult::Ok(sink) => sink,
                                dirents_sink::AppendResult::Sealed(sealed) => {
                                    // There is not enough space to return this entry. Record it as the next
                                    // entry to start at for subsequent calls.
                                    return Ok((TraversalPosition::Name(entry_name), sealed));
                                }
                            }
                        }
                    }
                }
            }
        }
        Ok((TraversalPosition::End, sink.seal()))
    }
}

/// Serves a Service directory that allows clients to list instances in a collection
/// and to open instances.
///
/// TODO(fxbug.dev/73153): Cache this collection directory and re-use it for requests from the
/// same target.
pub struct CollectionServiceDirectoryProvider {
    /// Execution scope for requests to `dir`. This is the same scope
    /// as the one in `collection_component`'s resolved state.
    execution_scope: ExecutionScope,

    /// The directory that contains entries for all service instances
    /// in the collection.
    dir: Arc<lazy::Lazy<CollectionServiceDirectory>>,
}

impl CollectionServiceDirectoryProvider {
    pub async fn create(
        target: WeakComponentInstance,
        collection_component: &Arc<ComponentInstance>,
        provider: Box<dyn AggregateCapabilityProvider<ComponentInstance>>,
    ) -> Result<CollectionServiceDirectoryProvider, ModelError> {
        let execution_scope =
            collection_component.lock_resolved_state().await?.execution_scope().clone();
        let dir = lazy::lazy(CollectionServiceDirectory {
            target,
            collection_component: collection_component.abs_moniker().clone(),
            provider,
        });
        Ok(CollectionServiceDirectoryProvider { execution_scope, dir })
    }
}

#[async_trait]
impl CapabilityProvider for CollectionServiceDirectoryProvider {
    async fn open(
        self: Box<Self>,
        _task_scope: TaskScope,
        flags: fio::OpenFlags,
        open_mode: u32,
        relative_path: PathBuf,
        server_end: &mut zx::Channel,
    ) -> Result<(), ModelError> {
        let relative_path_utf8 = relative_path
            .to_str()
            .ok_or_else(|| ModelError::path_is_not_utf8(relative_path.clone()))?;
        let relative_path = if relative_path_utf8.is_empty() {
            vfs::path::Path::dot()
        } else {
            vfs::path::Path::validate_and_split(relative_path_utf8)
                .map_err(|_| ModelError::path_invalid(relative_path_utf8))?
        };
        self.dir.open(
            self.execution_scope.clone(),
            flags,
            open_mode,
            relative_path,
            ServerEnd::new(channel::take_channel(server_end)),
        );
        Ok(())
    }
}

/// A directory entry representing an instance of a service.
/// Upon opening, performs capability routing and opens the instance at its source.
struct ServiceInstanceDirectoryEntry {
    /// The name of the component instance to route.
    component: String,
    /// The name of the instance directory to open at the source.
    instance: String,
    /// The original target of the capability route (the component that opened this directory).
    target: WeakComponentInstance,
    /// The moniker of the component at which the instance was aggregated.
    intermediate_component: AbsoluteMoniker,
    /// The provider that lists collection instances and performs routing to an instance.
    provider: Box<dyn AggregateCapabilityProvider<ComponentInstance>>,
}

impl DirectoryEntry for ServiceInstanceDirectoryEntry {
    fn open(
        self: Arc<Self>,
        scope: ExecutionScope,
        flags: fio::OpenFlags,
        mode: u32,
        path: vfs::path::Path,
        server_end: ServerEnd<fio::NodeMarker>,
    ) {
        let mut server_end = server_end.into_channel();
        scope.spawn(async move {
            let target = match self.target.upgrade() {
                Ok(target) => target,
                Err(_) => {
                    warn!(moniker=%self.target.abs_moniker, "target of service routing is gone");
                    return;
                }
            };

            let source = match self.provider.route_instance(&self.component).await {
                Ok(source) => source,
                Err(error) => {
                    server_end
                        .close_with_epitaph(error.as_zx_status())
                        .unwrap_or_else(|error| warn!(%error, "failed to close server end"));
                    target
                        .with_logger_as_default(|| {
                            warn!(
                                component=%self.component, from=%self.intermediate_component, %error,
                                "Failed to route",
                            );
                        })
                        .await;
                    return;
                }
            };

            let mut relative_path = PathBuf::from(&self.instance);

            // Path::join with an empty string adds a trailing slash, which some VFS implementations don't like.
            if !path.is_empty() {
                relative_path = relative_path.join(path.into_string());
            }

            if let Err(err) = open_capability_at_source(OpenRequest {
                flags,
                open_mode: mode,
                relative_path,
                source,
                target: &target,
                server_chan: &mut server_end,
            })
            .await
            {
                server_end
                    .close_with_epitaph(err.as_zx_status())
                    .unwrap_or_else(|error| warn!(%error, "failed to close server end"));

                target
                    .with_logger_as_default(|| {
                        error!(
                            instance=%self.instance,
                            source=%self.intermediate_component,
                            error=%err,
                            "Failed to open instance from intermediate component",
                        );
                    })
                    .await;
            }
        });
    }

    fn entry_info(&self) -> EntryInfo {
        EntryInfo::new(fio::INO_UNKNOWN, fio::DirentType::Directory)
    }
}

/// A directory entry representing a service with a collection as its source.
/// This directory is hosted by component_manager on behalf of the collection's owner.
/// Components use this directory to list instances in the collection that match the routed
/// service, and can open instances, performing capability routing to a source within the
/// collection.
///
/// This directory can be accessed by components by opening `/svc/my.service/` in their
/// incoming namespace when they have a `use my.service` declaration in their manifest, and the
/// source of `my.service` is a collection.
struct CollectionServiceDirectory {
    /// The original target of the capability route (the component that opened this directory).
    target: WeakComponentInstance,
    /// The moniker of the component hosting the collection.
    collection_component: AbsoluteMoniker,
    /// The provider that lists collection instances and performs routing to an instance.
    provider: Box<dyn AggregateCapabilityProvider<ComponentInstance>>,
}

#[async_trait]
impl lazy::LazyDirectory for CollectionServiceDirectory {
    async fn get_entry(&self, name: &str) -> Result<Arc<dyn DirectoryEntry>, zx::Status> {
        // Parse the entry name into its (component,instance) parts.
        // In the case of non-comma separated entries, treat the component and
        // instance name as the same.
        let (component, instance) = name.split_once(',').unwrap_or((name, name));
        Ok(Arc::new(ServiceInstanceDirectoryEntry {
            component: component.to_string(),
            instance: instance.to_string(),
            target: self.target.clone(),
            intermediate_component: self.collection_component.clone(),
            provider: self.provider.clone(),
        }))
    }

    async fn read_dirents<'a>(
        &'a self,
        pos: &'a TraversalPosition,
        mut sink: Box<dyn dirents_sink::Sink>,
    ) -> Result<(TraversalPosition, Box<dyn dirents_sink::Sealed>), zx::Status> {
        let next_entry = match pos {
            TraversalPosition::End => {
                // Bail out early when there is no work to do.
                // This method is always called at least once with TraversalPosition::End.
                return Ok((TraversalPosition::End, sink.seal()));
            }
            TraversalPosition::Start => None,
            TraversalPosition::Name(entry) => {
                // All generated filenames are guaranteed to have the ',' separator.
                entry.split_once(',').or(Some((entry.as_str(), entry.as_str())))
            }
            TraversalPosition::Index(_) => panic!("TraversalPosition::Index is never used"),
        };

        let target = self.target.upgrade().map_err(|e| e.as_zx_status())?;
        let mut instances =
            self.provider.list_instances().await.map_err(|_| zx::Status::INTERNAL)?;
        if instances.is_empty() {
            return Ok((TraversalPosition::End, sink.seal()));
        }

        // Sort to guarantee a stable iteration order.
        instances.sort();

        let (instances, mut next_instance) =
            if let Some((next_component, next_instance)) = next_entry {
                // Skip to the next entry. If the exact component is found, start there.
                // Otherwise start at the next component and clear any assumptions about
                // the next instance within that component.
                match instances.binary_search_by(|i| i.as_str().cmp(next_component)) {
                    Ok(idx) => (&instances[idx..], Some(next_instance)),
                    Err(idx) => (&instances[idx..], None),
                }
            } else {
                (&instances[0..], None)
            };

        for instance in instances {
            if let Ok(source) = self.provider.route_instance(&instance).await {
                let (proxy, server) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>()
                    .map_err(|_| zx::Status::INTERNAL)?;
                if let Ok(()) = open_capability_at_source(OpenRequest {
                    flags: fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
                    open_mode: fio::MODE_TYPE_DIRECTORY,
                    relative_path: PathBuf::new(),
                    source,
                    target: &target,
                    server_chan: &mut server.into_channel(),
                })
                .await
                {
                    if let Ok(mut dirents) = fuchsia_fs::directory::readdir(&proxy).await {
                        // Sort to guarantee a stable iteration order.
                        dirents.sort();

                        let dirents = if let Some(next_instance) = next_instance.take() {
                            // Skip to the next entry. If the exact instance is found, start there.
                            // Otherwise start at the next instance, assuming the missing one was removed.
                            match dirents.binary_search_by(|e| e.name.as_str().cmp(next_instance)) {
                                Ok(idx) | Err(idx) => &dirents[idx..],
                            }
                        } else {
                            &dirents[0..]
                        };

                        for dirent in dirents {
                            // Encode the (component,instance) tuple so that it can be represented in a single
                            // path segment. If the component and instance name are identical ignore comma separation.
                            let entry_name = {
                                if instance == &dirent.name {
                                    instance.clone()
                                } else {
                                    format!("{},{}", &instance, &dirent.name)
                                }
                            };
                            sink = match sink.append(
                                &EntryInfo::new(fio::INO_UNKNOWN, fio::DirentType::Directory),
                                &entry_name,
                            ) {
                                dirents_sink::AppendResult::Ok(sink) => sink,
                                dirents_sink::AppendResult::Sealed(sealed) => {
                                    // There is not enough space to return this entry. Record it as the next
                                    // entry to start at for subsequent calls.
                                    return Ok((TraversalPosition::Name(entry_name), sealed));
                                }
                            }
                        }
                    }
                }
            }
        }
        Ok((TraversalPosition::End, sink.seal()))
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{
            capability::CapabilitySource,
            model::{
                routing::providers::DirectoryEntryCapabilityProvider,
                testing::routing_test_helpers::RoutingTestBuilder,
            },
        },
        ::routing::{
            capability_source::ComponentCapability, component_instance::ComponentInstanceInterface,
            error::RoutingError,
        },
        assert_matches::assert_matches,
        cm_rust::*,
        cm_rust_testing::{ChildDeclBuilder, CollectionDeclBuilder, ComponentDeclBuilder},
        futures::StreamExt,
        moniker::{AbsoluteMoniker, AbsoluteMonikerBase, ChildMoniker},
        std::{
            collections::{HashMap, HashSet},
            convert::TryInto,
        },
        vfs::pseudo_directory,
    };

    #[derive(Clone)]
    struct MockAggregateCapabilityProvider {
        instances: HashMap<String, WeakComponentInstance>,
    }

    #[async_trait]
    impl AggregateCapabilityProvider<ComponentInstance> for MockAggregateCapabilityProvider {
        async fn route_instance(&self, instance: &str) -> Result<CapabilitySource, RoutingError> {
            Ok(CapabilitySource::Component {
                capability: ComponentCapability::Service(ServiceDecl {
                    name: "my.service.Service".into(),
                    source_path: Some("/svc/my.service.Service".try_into().unwrap()),
                }),
                component: self
                    .instances
                    .get(instance)
                    .ok_or_else(|| RoutingError::OfferFromChildInstanceNotFound {
                        capability_id: "my.service.Service".to_string(),
                        child_moniker: ChildMoniker::new(instance, None),
                        moniker: AbsoluteMoniker::root(),
                    })?
                    .clone(),
            })
        }

        async fn list_instances(&self) -> Result<Vec<String>, RoutingError> {
            Ok(self.instances.keys().cloned().collect())
        }

        fn clone_boxed(&self) -> Box<dyn AggregateCapabilityProvider<ComponentInstance>> {
            Box::new(self.clone())
        }
    }

    fn create_test_component_decls() -> Vec<(&'static str, ComponentDecl)> {
        vec![
            (
                "root",
                ComponentDeclBuilder::new()
                    .use_(UseDecl::Protocol(UseProtocolDecl {
                        source: UseSource::Framework,
                        source_name: "fuchsia.component.Realm".into(),
                        target_path: "/svc/fuchsia.component.Realm".try_into().unwrap(),
                        dependency_type: DependencyType::Strong,
                        availability: Availability::Required,
                    }))
                    .expose(ExposeDecl::Service(ExposeServiceDecl {
                        source: ExposeSource::Collection("coll".to_string()),
                        source_name: "my.service.Service".into(),
                        target_name: "my.service.Service".into(),
                        target: ExposeTarget::Parent,
                    }))
                    .add_collection(CollectionDeclBuilder::new_transient_collection("coll"))
                    .build(),
            ),
            (
                "foo",
                ComponentDeclBuilder::new()
                    .expose(ExposeDecl::Service(ExposeServiceDecl {
                        source: ExposeSource::Self_,
                        source_name: "my.service.Service".into(),
                        target_name: "my.service.Service".into(),
                        target: ExposeTarget::Parent,
                    }))
                    .service(ServiceDecl {
                        name: "my.service.Service".into(),
                        source_path: Some("/svc/my.service.Service".try_into().unwrap()),
                    })
                    .build(),
            ),
            (
                "bar",
                ComponentDeclBuilder::new()
                    .expose(ExposeDecl::Service(ExposeServiceDecl {
                        source: ExposeSource::Self_,
                        source_name: "my.service.Service".into(),
                        target_name: "my.service.Service".into(),
                        target: ExposeTarget::Parent,
                    }))
                    .service(ServiceDecl {
                        name: "my.service.Service".into(),
                        source_path: Some("/svc/my.service.Service".try_into().unwrap()),
                    })
                    .build(),
            ),
        ]
    }

    #[fuchsia::test]
    async fn service_collection_host_test() {
        let components = create_test_component_decls();

        let mock_instance = pseudo_directory! {
            "default" => pseudo_directory! {
                "member" => pseudo_directory! {}
            }
        };

        let test = RoutingTestBuilder::new("root", components)
            .add_outgoing_path(
                "foo",
                "/svc/my.service.Service".try_into().unwrap(),
                mock_instance.clone(),
            )
            .add_outgoing_path("bar", "/svc/my.service.Service".try_into().unwrap(), mock_instance)
            .build()
            .await;

        test.create_dynamic_child(
            AbsoluteMoniker::root(),
            "coll",
            ChildDeclBuilder::new_lazy_child("foo"),
        )
        .await;
        test.create_dynamic_child(
            AbsoluteMoniker::root(),
            "coll",
            ChildDeclBuilder::new_lazy_child("bar"),
        )
        .await;
        let foo_component = test
            .model
            .look_up(&vec!["coll:foo"].into())
            .await
            .expect("failed to find foo instance");
        let bar_component = test
            .model
            .look_up(&vec!["coll:bar"].into())
            .await
            .expect("failed to find bar instance");

        let provider = MockAggregateCapabilityProvider {
            instances: {
                let mut instances = HashMap::new();
                instances.insert("foo".to_string(), foo_component.as_weak());
                instances.insert("bar".to_string(), bar_component.as_weak());
                instances
            },
        };

        let (service_proxy, server_end) =
            fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
        let mut server_end = server_end.into_channel();

        let host = Box::new(
            CollectionServiceDirectoryProvider::create(
                test.model.root().as_weak(),
                &test.model.root(),
                Box::new(provider),
            )
            .await
            .expect("failed to create CollectionServiceDirectoryProvider"),
        );
        let task_scope = TaskScope::new();
        host.open(
            task_scope.clone(),
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
            fio::MODE_TYPE_DIRECTORY,
            PathBuf::new(),
            &mut server_end,
        )
        .await
        .expect("failed to serve");

        // List the entries of the directory served by `open`.
        let entries = fuchsia_fs::directory::readdir(&service_proxy)
            .await
            .expect("failed to read directory entries");
        let instance_names: HashSet<String> = entries.into_iter().map(|d| d.name).collect();
        assert_eq!(instance_names.len(), 2);

        // Open one of the entries.
        let collection_dir = fuchsia_fs::directory::open_directory(
            &service_proxy,
            instance_names.iter().next().expect("failed to get instance name"),
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        )
        .await
        .expect("failed to open collection dir");

        // Make sure we're reading the expected directory.
        let entries = fuchsia_fs::directory::readdir(&collection_dir)
            .await
            .expect("failed to read instances of collection dir");
        assert!(entries.into_iter().find(|d| d.name == "member").is_some());
    }

    #[fuchsia::test]
    async fn test_collection_readdir() {
        let components = create_test_component_decls();

        let mock_instance_foo = pseudo_directory! {
            "default" => pseudo_directory! {}
        };

        let mock_instance_bar = pseudo_directory! {
            "default" => pseudo_directory! {},
            "one" => pseudo_directory! {},
        };

        let test = RoutingTestBuilder::new("root", components)
            .add_outgoing_path(
                "foo",
                "/svc/my.service.Service".try_into().unwrap(),
                mock_instance_foo,
            )
            .add_outgoing_path(
                "bar",
                "/svc/my.service.Service".try_into().unwrap(),
                mock_instance_bar,
            )
            .build()
            .await;

        test.create_dynamic_child(
            AbsoluteMoniker::root(),
            "coll",
            ChildDeclBuilder::new_lazy_child("foo"),
        )
        .await;
        test.create_dynamic_child(
            AbsoluteMoniker::root(),
            "coll",
            ChildDeclBuilder::new_lazy_child("bar"),
        )
        .await;
        let foo_component = test
            .model
            .look_up(&vec!["coll:foo"].into())
            .await
            .expect("failed to find foo instance");
        let bar_component = test
            .model
            .look_up(&vec!["coll:bar"].into())
            .await
            .expect("failed to find bar instance");

        let provider = MockAggregateCapabilityProvider {
            instances: {
                let mut instances = HashMap::new();
                instances.insert("foo".to_string(), foo_component.as_weak());
                instances.insert("bar".to_string(), bar_component.as_weak());
                instances
            },
        };

        let (service_proxy, server_end) =
            fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
        let mut server_end = server_end.into_channel();

        let host = Box::new(
            CollectionServiceDirectoryProvider::create(
                test.model.root().as_weak(),
                &test.model.root(),
                Box::new(provider),
            )
            .await
            .expect("failed to create CollectionServiceDirectoryProvider"),
        );
        let task_scope = TaskScope::new();
        host.open(
            task_scope.clone(),
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
            fio::MODE_TYPE_DIRECTORY,
            PathBuf::new(),
            &mut server_end,
        )
        .await
        .expect("failed to serve");

        // Choose a value such that there is only room for a single entry.
        const MAX_BYTES: u64 = 30;

        let (status, buf) = service_proxy.read_dirents(MAX_BYTES).await.expect("read_dirents");
        assert_eq!(zx::Status::from_raw(status), zx::Status::OK);
        let entries = fuchsia_fs::directory::parse_dir_entries(&buf);
        assert_eq!(entries.len(), 1);
        assert_eq!(entries[0].as_ref().expect("complete entry").name, "bar,default");

        let (status, buf) = service_proxy.read_dirents(MAX_BYTES).await.expect("read_dirents");
        assert_eq!(zx::Status::from_raw(status), zx::Status::OK);
        let entries = fuchsia_fs::directory::parse_dir_entries(&buf);
        assert_eq!(entries.len(), 1);
        assert_eq!(entries[0].as_ref().expect("complete entry").name, "bar,one");

        let (status, buf) = service_proxy.read_dirents(MAX_BYTES).await.expect("read_dirents");
        assert_eq!(zx::Status::from_raw(status), zx::Status::OK);
        let entries = fuchsia_fs::directory::parse_dir_entries(&buf);
        assert_eq!(entries.len(), 1);
        assert_eq!(entries[0].as_ref().expect("complete entry").name, "foo,default");
    }

    #[fuchsia::test]
    async fn test_filtered_service() {
        let components = create_test_component_decls();

        let mock_instance_foo = pseudo_directory! {
            "default" => pseudo_directory! {},
            "one" => pseudo_directory! {},
            "two" => pseudo_directory! {},
        };

        let test = RoutingTestBuilder::new("root", components)
            .add_outgoing_path(
                "foo",
                "/svc/my.service.Service".try_into().unwrap(),
                mock_instance_foo.clone(),
            )
            .build()
            .await;

        test.create_dynamic_child(
            AbsoluteMoniker::root(),
            "coll",
            ChildDeclBuilder::new_lazy_child("foo"),
        )
        .await;
        let foo_component = test
            .model
            .look_up(&vec!["coll:foo"].into())
            .await
            .expect("failed to find foo instance");

        let execution_scope = foo_component
            .lock_resolved_state()
            .await
            .expect("failed to get execution scope")
            .execution_scope()
            .clone();
        let source_provider =
            DirectoryEntryCapabilityProvider { execution_scope, entry: mock_instance_foo.clone() };

        let (service_proxy, server_end) =
            fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
        let mut server_end = server_end.into_channel();

        let host = Box::new(
            FilteredServiceProvider::new(
                &test.model.root(),
                vec!["default".to_string(), "two".to_string()],
                HashMap::new(),
                Box::new(source_provider),
            )
            .await
            .expect("failed to create FilteredServiceProvider"),
        );
        let task_scope = TaskScope::new();
        host.open(
            task_scope.clone(),
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
            fio::MODE_TYPE_DIRECTORY,
            PathBuf::new(),
            &mut server_end,
        )
        .await
        .expect("failed to serve");

        // Choose a value such that there is only room for a single entry.
        const MAX_BYTES: u64 = 20;
        // Make sure expected instances are found
        for n in ["default", "two"] {
            let (status, buf) = service_proxy.read_dirents(MAX_BYTES).await.expect("read_dirents");
            assert_eq!(zx::Status::from_raw(status), zx::Status::OK);
            let entries = fuchsia_fs::directory::parse_dir_entries(&buf);
            assert_eq!(entries.len(), 1);
            assert_eq!(entries[0].as_ref().expect("complete entry").name, n);
        }

        // Confirm no more entries found after allow listed instances.
        let (status, buf) = service_proxy.read_dirents(MAX_BYTES).await.expect("read_dirents");
        assert_eq!(zx::Status::from_raw(status), zx::Status::OK);
        let entries = fuchsia_fs::directory::parse_dir_entries(&buf);
        assert_eq!(entries.len(), 0);
    }

    #[fuchsia::test]
    async fn test_filtered_service_renaming() {
        let components = create_test_component_decls();

        let mock_instance_foo = pseudo_directory! {
            "default" => pseudo_directory! {},
            "one" => pseudo_directory! {},
            "two" => pseudo_directory! {},
        };

        let test = RoutingTestBuilder::new("root", components)
            .add_outgoing_path(
                "foo",
                "/svc/my.service.Service".try_into().unwrap(),
                mock_instance_foo.clone(),
            )
            .build()
            .await;

        test.create_dynamic_child(
            AbsoluteMoniker::root(),
            "coll",
            ChildDeclBuilder::new_lazy_child("foo"),
        )
        .await;
        let foo_component = test
            .model
            .look_up(&vec!["coll:foo"].into())
            .await
            .expect("failed to find foo instance");

        let execution_scope = foo_component
            .lock_resolved_state()
            .await
            .expect("failed to get execution scope")
            .execution_scope()
            .clone();
        let source_provider =
            DirectoryEntryCapabilityProvider { execution_scope, entry: mock_instance_foo.clone() };

        let (service_proxy, server_end) =
            fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
        let mut server_end = server_end.into_channel();

        let host = Box::new(
            FilteredServiceProvider::new(
                &test.model.root(),
                vec![],
                HashMap::from([
                    ("default".to_string(), vec!["aaaaaaa".to_string(), "bbbbbbb".to_string()]),
                    ("one".to_string(), vec!["one_a".to_string()]),
                ]),
                Box::new(source_provider),
            )
            .await
            .expect("failed to create FilteredServiceProvider"),
        );

        let task_scope = TaskScope::new();
        host.open(
            task_scope.clone(),
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
            fio::MODE_TYPE_DIRECTORY,
            PathBuf::new(),
            &mut server_end,
        )
        .await
        .expect("failed to open path in filtered service directory.");

        // Choose a value such that there is only room for a single entry.
        const MAX_BYTES: u64 = 20;
        // Make sure expected instances are found
        for n in ["aaaaaaa", "bbbbbbb", "one_a", "two"] {
            let (status, buf) = service_proxy.read_dirents(MAX_BYTES).await.expect("read_dirents");
            assert_eq!(zx::Status::from_raw(status), zx::Status::OK);
            let entries = fuchsia_fs::directory::parse_dir_entries(&buf);
            assert_eq!(entries.len(), 1);
            assert_eq!(entries[0].as_ref().expect("complete entry").name, n);
        }

        // Confirm no more entries found after allow listed instances.
        let (status, buf) = service_proxy.read_dirents(MAX_BYTES).await.expect("read_dirents");
        assert_eq!(zx::Status::from_raw(status), zx::Status::OK);
        let entries = fuchsia_fs::directory::parse_dir_entries(&buf);
        assert_eq!(entries.len(), 0);
    }

    #[fuchsia::test]
    async fn test_filtered_service_error_cases() {
        let components = create_test_component_decls();

        let mock_instance_foo = pseudo_directory! {
            "default" => pseudo_directory! {},
            "one" => pseudo_directory! {},
            "two" => pseudo_directory! {},
        };

        let test = RoutingTestBuilder::new("root", components)
            .add_outgoing_path(
                "foo",
                "/svc/my.service.Service".try_into().unwrap(),
                mock_instance_foo.clone(),
            )
            .build()
            .await;

        test.create_dynamic_child(
            AbsoluteMoniker::root(),
            "coll",
            ChildDeclBuilder::new_lazy_child("foo"),
        )
        .await;
        let foo_component = test
            .model
            .look_up(&vec!["coll:foo"].into())
            .await
            .expect("failed to find foo instance");

        let execution_scope = foo_component
            .lock_resolved_state()
            .await
            .expect("failed to get execution scope")
            .execution_scope()
            .clone();
        let source_provider =
            DirectoryEntryCapabilityProvider { execution_scope, entry: mock_instance_foo.clone() };

        let (service_proxy, server_end) =
            fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
        let mut server_end = server_end.into_channel();

        let host = Box::new(
            FilteredServiceProvider::new(
                &test.model.root(),
                vec!["default".to_string(), "two".to_string()],
                HashMap::new(),
                Box::new(source_provider),
            )
            .await
            .expect("failed to create FilteredServiceProvider"),
        );

        let task_scope = TaskScope::new();
        // expect that opening an instance that is filtered out
        let mut path_buf = PathBuf::new();
        path_buf.push("one");
        host.open(
            task_scope.clone(),
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
            fio::MODE_TYPE_DIRECTORY,
            path_buf,
            &mut server_end,
        )
        .await
        .expect("failed to open path in filtered service directory.");
        assert_matches!(
            service_proxy.take_event_stream().next().await.unwrap(),
            Err(fidl::Error::ClientChannelClosed { status: zx::Status::NOT_FOUND, .. })
        );
    }
}

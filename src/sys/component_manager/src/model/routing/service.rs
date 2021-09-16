// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        channel,
        model::{
            component::{ComponentInstance, WeakComponentInstance},
            error::ModelError,
            routing::{open_capability_at_source, OpenRequest},
        },
    },
    ::routing::capability_source::CollectionCapabilityProvider,
    async_trait::async_trait,
    fidl::{endpoints::ServerEnd, epitaph::ChannelEpitaphExt},
    fidl_fuchsia_io as fio,
    fuchsia_zircon::{Channel, Status},
    log::*,
    moniker::AbsoluteMoniker,
    std::{path::PathBuf, sync::Arc},
    vfs::{
        directory::{
            dirents_sink,
            entry::{DirectoryEntry, EntryInfo},
            immutable::lazy,
            traversal_position::TraversalPosition,
        },
        execution_scope::ExecutionScope,
    },
};

/// Serve a Service directory that allows clients to list instances in a collection and to open
/// instances, triggering capability routing.
/// TODO(fxbug.dev/73153): Cache this collection directory and re-use it for requests from the
/// same target.
pub async fn serve_collection<'a>(
    target: WeakComponentInstance,
    collection_component: &'a Arc<ComponentInstance>,
    provider: Box<dyn CollectionCapabilityProvider<ComponentInstance>>,
    flags: u32,
    open_mode: u32,
    path: PathBuf,
    server_chan: &'a mut Channel,
) -> Result<(), ModelError> {
    let path_utf8 = path.to_str().ok_or_else(|| ModelError::path_is_not_utf8(path.clone()))?;
    let path = if path_utf8.is_empty() {
        vfs::path::Path::dot()
    } else {
        vfs::path::Path::validate_and_split(path_utf8)
            .map_err(|_| ModelError::path_invalid(path_utf8))?
    };
    let execution_scope =
        collection_component.lock_resolved_state().await?.execution_scope().clone();
    let dir = lazy::lazy(ServiceCollectionDirectory {
        target,
        collection_component: collection_component.abs_moniker.clone(),
        provider,
    });
    dir.open(
        execution_scope,
        flags,
        open_mode,
        path,
        ServerEnd::new(channel::take_channel(server_chan)),
    );
    Ok(())
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
    provider: Box<dyn CollectionCapabilityProvider<ComponentInstance>>,
}

impl DirectoryEntry for ServiceInstanceDirectoryEntry {
    fn open(
        self: Arc<Self>,
        scope: ExecutionScope,
        flags: u32,
        mode: u32,
        mut path: vfs::path::Path,
        server_end: ServerEnd<fio::NodeMarker>,
    ) {
        let mut server_end = server_end.into_channel();
        scope.spawn(async move {
            let target = match self.target.upgrade() {
                Ok(target) => target,
                Err(_) => {
                    warn!("target of service routing is gone: {}", &self.target.moniker);
                    return;
                }
            };

            let source = match self.provider.route_instance(&self.component).await {
                Ok(source) => source,
                Err(err) => {
                    let _ = server_end.close_with_epitaph(err.as_zx_status());
                    target
                        .log(
                            log::Level::Warn,
                            format!(
                                "failed to route component instance `{}` from intermediate component {}: {}",
                                &self.component, &self.intermediate_component, err
                            ),
                        )
                        .await;
                    return;
                }
            };

            // Consume the next path segment, which is the "component,instance" portion we've already extracted.
            let _ = path.next();

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
                let _ = server_end.close_with_epitaph(err.as_zx_status());
                target
                    .log(
                        log::Level::Error,
                        format!(
                            "failed to open instance `{}` from intermediate component {}: {}",
                            &self.instance, &self.intermediate_component, err
                        ),
                    )
                    .await;
            }
        });
    }

    fn entry_info(&self) -> EntryInfo {
        EntryInfo::new(fio::INO_UNKNOWN, fio::DIRENT_TYPE_DIRECTORY)
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
struct ServiceCollectionDirectory {
    /// The original target of the capability route (the component that opened this directory).
    target: WeakComponentInstance,
    /// The moniker of the component hosting the collection.
    collection_component: AbsoluteMoniker,
    /// The provider that lists collection instances and performs routing to an instance.
    provider: Box<dyn CollectionCapabilityProvider<ComponentInstance>>,
}

#[async_trait]
impl lazy::LazyDirectory for ServiceCollectionDirectory {
    async fn get_entry(&self, name: &str) -> Result<Arc<dyn DirectoryEntry>, Status> {
        // Parse the entry name into its (component,instance) parts.
        let (component, instance) = name.split_once(',').ok_or(Status::NOT_FOUND)?;
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
    ) -> Result<(TraversalPosition, Box<dyn dirents_sink::Sealed>), Status> {
        let next_entry = match pos {
            TraversalPosition::End => {
                // Bail out early when there is no work to do.
                // This method is always called at least once with TraversalPosition::End.
                return Ok((TraversalPosition::End, sink.seal()));
            }
            TraversalPosition::Start => None,
            TraversalPosition::Name(entry) => {
                // All generated filenames are guaranteed to have the ',' separator.
                entry.split_once(',')
            }
            TraversalPosition::Index(_) => panic!("TraversalPosition::Index is never used"),
        };

        let target = self.target.upgrade().map_err(|e| e.as_zx_status())?;
        let mut instances = self.provider.list_instances().await.map_err(|_| Status::INTERNAL)?;
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
                    .map_err(|_| Status::INTERNAL)?;
                if let Ok(()) = open_capability_at_source(OpenRequest {
                    flags: fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE,
                    open_mode: fio::MODE_TYPE_DIRECTORY,
                    relative_path: PathBuf::new(),
                    source,
                    target: &target,
                    server_chan: &mut server.into_channel(),
                })
                .await
                {
                    if let Ok(mut dirents) = files_async::readdir(&proxy).await {
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
                            // path segment.
                            let entry_name = format!("{},{}", &instance, &dirent.name);
                            sink = match sink.append(
                                &EntryInfo::new(fio::INO_UNKNOWN, fio::DIRENT_TYPE_DIRECTORY),
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
            capability::{CapabilitySource, ComponentCapability},
            model::{routing::RoutingError, testing::routing_test_helpers::RoutingTestBuilder},
        },
        ::routing::component_instance::ComponentInstanceInterface,
        cm_rust::*,
        cm_rust_testing::{ChildDeclBuilder, CollectionDeclBuilder, ComponentDeclBuilder},
        moniker::{
            AbsoluteMoniker, AbsoluteMonikerBase, PartialAbsoluteMoniker, PartialChildMoniker,
        },
        std::{
            collections::{HashMap, HashSet},
            convert::TryInto,
        },
        vfs::pseudo_directory,
    };

    #[derive(Clone)]
    struct MockCollectionCapabilityProvider {
        instances: HashMap<String, WeakComponentInstance>,
    }

    #[async_trait]
    impl CollectionCapabilityProvider<ComponentInstance> for MockCollectionCapabilityProvider {
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
                        child_moniker: PartialChildMoniker::new(instance.to_string(), None),
                        moniker: PartialAbsoluteMoniker::root(),
                    })?
                    .clone(),
            })
        }

        async fn list_instances(&self) -> Result<Vec<String>, RoutingError> {
            Ok(self.instances.keys().cloned().collect())
        }

        fn clone_boxed(&self) -> Box<dyn CollectionCapabilityProvider<ComponentInstance>> {
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
                        source_name: "fuchsia.sys2.Realm".into(),
                        target_path: "/svc/fuchsia.sys2.Realm".try_into().unwrap(),
                        dependency_type: DependencyType::Strong,
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
    async fn serve_collection_test() {
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

        let provider = MockCollectionCapabilityProvider {
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
        serve_collection(
            test.model.root().as_weak(),
            &test.model.root(),
            Box::new(provider),
            fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE,
            fio::MODE_TYPE_DIRECTORY,
            PathBuf::new(),
            &mut server_end,
        )
        .await
        .expect("failed to serve");

        // List the entries of the directory served by `serve_collection`.
        let entries =
            files_async::readdir(&service_proxy).await.expect("failed to read directory entries");
        let instance_names: HashSet<String> = entries.into_iter().map(|d| d.name).collect();
        assert_eq!(instance_names.len(), 2);
        assert!(instance_names.contains("foo,default"));
        assert!(instance_names.contains("bar,default"));

        // Open one of the entries.
        let collection_dir = io_util::directory::open_directory(
            &service_proxy,
            "foo,default",
            fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE,
        )
        .await
        .expect("failed to open collection dir");

        // Make sure we're reading the expected directory.
        let entries = files_async::readdir(&collection_dir)
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

        let provider = MockCollectionCapabilityProvider {
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
        serve_collection(
            test.model.root().as_weak(),
            test.model.root(),
            Box::new(provider),
            fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE,
            fio::MODE_TYPE_DIRECTORY,
            PathBuf::new(),
            &mut server_end,
        )
        .await
        .expect("failed to serve");

        // Choose a value such that there is only room for a single entry.
        const MAX_BYTES: u64 = 30;

        let (status, buf) = service_proxy.read_dirents(MAX_BYTES).await.expect("read_dirents");
        assert_eq!(Status::from_raw(status), Status::OK);
        let entries = files_async::parse_dir_entries(&buf);
        assert_eq!(entries.len(), 1);
        assert_eq!(entries[0].as_ref().expect("complete entry").name, "bar,default");

        let (status, buf) = service_proxy.read_dirents(MAX_BYTES).await.expect("read_dirents");
        assert_eq!(Status::from_raw(status), Status::OK);
        let entries = files_async::parse_dir_entries(&buf);
        assert_eq!(entries.len(), 1);
        assert_eq!(entries[0].as_ref().expect("complete entry").name, "bar,one");

        let (status, buf) = service_proxy.read_dirents(MAX_BYTES).await.expect("read_dirents");
        assert_eq!(Status::from_raw(status), Status::OK);
        let entries = files_async::parse_dir_entries(&buf);
        assert_eq!(entries.len(), 1);
        assert_eq!(entries[0].as_ref().expect("complete entry").name, "foo,default");
    }
}

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::directory_broker::{DirectoryBroker, RoutingFn},
    crate::model::addable_directory::AddableDirectory,
    crate::model::*,
    cm_rust::{CapabilityPath, ComponentDecl, ExposeDecl, UseDecl, UseStorageDecl},
    fuchsia_vfs_pseudo_fs::directory,
    std::collections::HashMap,
};

/// Represents the directory hierarchy of the exposed directory, not including the nodes for the
/// capabilities themselves.
pub struct DirTree {
    directory_nodes: HashMap<String, Box<DirTree>>,
    broker_nodes: HashMap<String, RoutingFn>,
}

impl DirTree {
    /// Builds a directory hierarchy from a component's `uses` declarations.
    /// `routing_factory` is a closure that generates the routing function that will be called
    /// when a leaf node is opened.
    pub fn build_from_uses(
        routing_factory: impl Fn(AbsoluteMoniker, UseDecl) -> RoutingFn,
        abs_moniker: &AbsoluteMoniker,
        decl: ComponentDecl,
    ) -> Result<Self, ModelError> {
        let mut tree = DirTree { directory_nodes: HashMap::new(), broker_nodes: HashMap::new() };
        for use_ in decl.uses {
            tree.add_use_capability(&routing_factory, abs_moniker, &use_)?;
        }
        Ok(tree)
    }

    /// Builds a directory hierarchy from a component's `exposes` declarations.
    /// `routing_factory` is a closure that generates the routing function that will be called
    /// when a leaf node is opened.
    pub fn build_from_exposes(
        routing_factory: impl Fn(AbsoluteMoniker, ExposeDecl) -> RoutingFn,
        abs_moniker: &AbsoluteMoniker,
        decl: ComponentDecl,
    ) -> Self {
        let mut tree = DirTree { directory_nodes: HashMap::new(), broker_nodes: HashMap::new() };
        for expose in decl.exposes {
            tree.add_expose_capability(&routing_factory, abs_moniker, &expose);
        }
        tree
    }

    /// Installs the directory tree into `root_dir`.
    pub fn install<'entries>(
        self,
        abs_moniker: &AbsoluteMoniker,
        root_dir: &mut impl AddableDirectory<'entries>,
    ) -> Result<(), ModelError> {
        for (name, subtree) in self.directory_nodes {
            let mut subdir = directory::simple::empty();
            subtree.install(abs_moniker, &mut subdir)?;
            root_dir.add_node(&name, subdir, abs_moniker)?;
        }
        for (name, route_fn) in self.broker_nodes {
            let node = DirectoryBroker::new(route_fn);
            root_dir.add_node(&name, node, abs_moniker)?;
        }
        Ok(())
    }

    fn add_use_capability(
        &mut self,
        routing_factory: &impl Fn(AbsoluteMoniker, UseDecl) -> RoutingFn,
        abs_moniker: &AbsoluteMoniker,
        use_: &UseDecl,
    ) -> Result<(), ModelError> {
        let path = match use_ {
            cm_rust::UseDecl::Service(d) => &d.target_path,
            cm_rust::UseDecl::LegacyService(d) => &d.target_path,
            cm_rust::UseDecl::Directory(d) => &d.target_path,
            cm_rust::UseDecl::Storage(UseStorageDecl::Data(p)) => &p,
            cm_rust::UseDecl::Storage(UseStorageDecl::Cache(p)) => &p,
            cm_rust::UseDecl::Storage(UseStorageDecl::Meta) => {
                // Meta storage doesn't show up in the namespace, nothing to do.
                return Ok(());
            }
        };
        let tree = self.to_directory_node(path);
        let routing_fn = routing_factory(abs_moniker.clone(), use_.clone());
        tree.broker_nodes.insert(path.basename.to_string(), routing_fn);
        Ok(())
    }

    fn add_expose_capability(
        &mut self,
        routing_factory: &impl Fn(AbsoluteMoniker, ExposeDecl) -> RoutingFn,
        abs_moniker: &AbsoluteMoniker,
        expose: &ExposeDecl,
    ) {
        let path = match expose {
            cm_rust::ExposeDecl::Service(d) => &d.target_path,
            cm_rust::ExposeDecl::LegacyService(d) => &d.target_path,
            cm_rust::ExposeDecl::Directory(d) => &d.target_path,
        };
        let tree = self.to_directory_node(path);
        let routing_fn = routing_factory(abs_moniker.clone(), expose.clone());
        tree.broker_nodes.insert(path.basename.to_string(), routing_fn);
    }

    fn to_directory_node(&mut self, path: &CapabilityPath) -> &mut DirTree {
        let components = path.dirname.split("/");
        let mut tree = self;
        for component in components {
            if !component.is_empty() {
                tree = tree.directory_nodes.entry(component.to_string()).or_insert(Box::new(
                    DirTree { directory_nodes: HashMap::new(), broker_nodes: HashMap::new() },
                ));
            }
        }
        tree
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::model::testing::{mocks, routing_test_helpers::default_component_decl, test_utils},
        cm_rust::{
            CapabilityPath, ExposeDecl, ExposeDirectoryDecl, ExposeLegacyServiceDecl, ExposeSource,
            UseDecl, UseDirectoryDecl, UseLegacyServiceDecl, UseSource, UseStorageDecl,
        },
        fidl::endpoints::{ClientEnd, ServerEnd},
        fidl_fuchsia_io::MODE_TYPE_DIRECTORY,
        fidl_fuchsia_io::{DirectoryMarker, NodeMarker, OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE},
        fuchsia_async as fasync,
        fuchsia_vfs_pseudo_fs::directory::{self, entry::DirectoryEntry},
        fuchsia_zircon as zx,
        std::{convert::TryFrom, iter},
    };

    #[fuchsia_async::run_singlethreaded(test)]
    async fn build_from_uses() {
        // Call `build_from_uses` with a routing factory that routes to a mock directory or service,
        // and a `ComponentDecl` with `use` declarations.
        let routing_factory = mocks::proxy_use_routing_factory();
        let decl = ComponentDecl {
            uses: vec![
                UseDecl::Directory(UseDirectoryDecl {
                    source: UseSource::Realm,
                    source_path: CapabilityPath::try_from("/data/baz").unwrap(),
                    target_path: CapabilityPath::try_from("/in/data/hippo").unwrap(),
                }),
                UseDecl::LegacyService(UseLegacyServiceDecl {
                    source: UseSource::Realm,
                    source_path: CapabilityPath::try_from("/svc/baz").unwrap(),
                    target_path: CapabilityPath::try_from("/in/svc/hippo").unwrap(),
                }),
                UseDecl::Storage(UseStorageDecl::Data(
                    CapabilityPath::try_from("/in/data/persistent").unwrap(),
                )),
                UseDecl::Storage(UseStorageDecl::Cache(
                    CapabilityPath::try_from("/in/data/cache").unwrap(),
                )),
                UseDecl::Storage(UseStorageDecl::Meta),
            ],
            ..default_component_decl()
        };
        let abs_moniker = AbsoluteMoniker::root();
        let tree = DirTree::build_from_uses(routing_factory, &abs_moniker, decl.clone())
            .expect("Unable to build 'uses' directory");

        // Convert the tree to a directory.
        let mut in_dir = directory::simple::empty();
        tree.install(&abs_moniker, &mut in_dir).expect("Unable to build pseudodirectory");
        let (in_dir_client, in_dir_server) = zx::Channel::create().unwrap();
        in_dir.open(
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            MODE_TYPE_DIRECTORY,
            &mut iter::empty(),
            ServerEnd::<NodeMarker>::new(in_dir_server.into()),
        );
        fasync::spawn(async move {
            let _ = in_dir.await;
        });
        let in_dir_proxy = ClientEnd::<DirectoryMarker>::new(in_dir_client)
            .into_proxy()
            .expect("failed to create directory proxy");
        assert_eq!(
            vec!["in/data/cache", "in/data/hippo", "in/data/persistent", "in/svc/hippo"],
            test_utils::list_directory_recursive(&in_dir_proxy).await
        );

        // Expect that calls on the directory nodes reach the mock directory/service.
        assert_eq!("friend", test_utils::read_file(&in_dir_proxy, "in/data/hippo/hello").await);
        assert_eq!(
            "friend",
            test_utils::read_file(&in_dir_proxy, "in/data/persistent/hello").await
        );
        assert_eq!("friend", test_utils::read_file(&in_dir_proxy, "in/data/cache/hello").await);
        assert_eq!(
            "hippos".to_string(),
            test_utils::call_echo(&in_dir_proxy, "in/svc/hippo").await
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn build_from_exposes() {
        // Call `build_from_exposes` with a routing factory that routes to a mock directory or
        // service, and a `ComponentDecl` with `expose` declarations.
        let routing_factory = mocks::proxy_expose_routing_factory();
        let decl = ComponentDecl {
            exposes: vec![
                ExposeDecl::Directory(ExposeDirectoryDecl {
                    source: ExposeSource::Self_,
                    source_path: CapabilityPath::try_from("/data/baz").unwrap(),
                    target_path: CapabilityPath::try_from("/in/data/hippo").unwrap(),
                }),
                ExposeDecl::Directory(ExposeDirectoryDecl {
                    source: ExposeSource::Self_,
                    source_path: CapabilityPath::try_from("/data/foo").unwrap(),
                    target_path: CapabilityPath::try_from("/in/data/bar").unwrap(),
                }),
                ExposeDecl::LegacyService(ExposeLegacyServiceDecl {
                    source: ExposeSource::Self_,
                    source_path: CapabilityPath::try_from("/svc/baz").unwrap(),
                    target_path: CapabilityPath::try_from("/in/svc/hippo").unwrap(),
                }),
            ],
            ..default_component_decl()
        };
        let abs_moniker = AbsoluteMoniker::root();
        let tree = DirTree::build_from_exposes(routing_factory, &abs_moniker, decl.clone());

        // Convert the tree to a directory.
        let mut expose_dir = directory::simple::empty();
        tree.install(&abs_moniker, &mut expose_dir).expect("Unable to build pseudodirectory");
        let (expose_dir_client, expose_dir_server) = zx::Channel::create().unwrap();
        expose_dir.open(
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            MODE_TYPE_DIRECTORY,
            &mut iter::empty(),
            ServerEnd::<NodeMarker>::new(expose_dir_server.into()),
        );
        fasync::spawn(async move {
            let _ = expose_dir.await;
        });
        let expose_dir_proxy = ClientEnd::<DirectoryMarker>::new(expose_dir_client)
            .into_proxy()
            .expect("failed to create directory proxy");
        assert_eq!(
            vec!["in/data/bar", "in/data/hippo", "in/svc/hippo"],
            test_utils::list_directory_recursive(&expose_dir_proxy).await
        );

        // Expect that calls on the directory nodes reach the mock directory/service.
        assert_eq!("friend", test_utils::read_file(&expose_dir_proxy, "in/data/bar/hello").await);
        assert_eq!(
            "friend",
            test_utils::read_file(&expose_dir_proxy, "in/data/hippo/hello").await
        );
        assert_eq!(
            "hippos".to_string(),
            test_utils::call_echo(&expose_dir_proxy, "in/svc/hippo").await
        );
    }
}

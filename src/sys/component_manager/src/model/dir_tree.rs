// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        capability::CapabilitySource,
        model::{
            addable_directory::{AddableDirectory, AddableDirectoryWithResult},
            error::ModelError,
            moniker::AbsoluteMoniker,
            routing_facade::RoutingFacade,
        },
    },
    cm_rust::{CapabilityPath, ComponentDecl, ExposeDecl, UseDecl},
    directory_broker::{DirectoryBroker, RoutingFn},
    fuchsia_vfs_pseudo_fs_mt::directory::immutable::simple as pfs,
    std::collections::HashMap,
    std::sync::Arc,
};

type Directory = Arc<pfs::Simple>;

pub(super) struct CapabilityUsageTree {
    directory_nodes: HashMap<String, CapabilityUsageTree>,
    dir: Box<Directory>,
    routing_facade: RoutingFacade,
}

impl CapabilityUsageTree {
    pub fn new(dir: Directory, routing_facade: RoutingFacade) -> Self {
        Self { directory_nodes: HashMap::new(), dir: Box::new(dir), routing_facade }
    }

    pub async fn mark_capability_used(
        &mut self,
        abs_moniker: &AbsoluteMoniker,
        source: CapabilitySource,
    ) -> Result<(), ModelError> {
        // Do nothing for capabilities without paths
        let path = {
            match source.path() {
                Some(path) => path,
                None => return Ok(()),
            }
        };
        let basename = path.basename.to_string();
        let tree = self.to_directory_node(path, abs_moniker).await?;
        // TODO(44746): This is probably not correct. The capability source lacks crucial
        // information about the capability routing, such as rights to apply on a directory.  Could
        // possibly use `route_use_fn_factory` instead.
        let routing_factory = tree.routing_facade.route_capability_source_fn_factory();
        let routing_fn = routing_factory(abs_moniker.clone(), source);

        let node = DirectoryBroker::new(routing_fn);

        // Adding a node to the Hub can fail
        tree.dir.add_node(&basename, node, abs_moniker).unwrap_or_else(|_| {
            // TODO(xbhatnag): The error received is not granular enough to know if the node
            // already exists, so treat this as a success for now. Ideally, pseudo_vfs should
            // have an exists() operation.
        });
        Ok(())
    }

    async fn to_directory_node(
        &mut self,
        path: &CapabilityPath,
        abs_moniker: &AbsoluteMoniker,
    ) -> Result<&mut CapabilityUsageTree, ModelError> {
        let components = path.dirname.split("/");
        let mut tree = self;
        for component in components {
            if !component.is_empty() {
                // If the next component does not exist in the tree, create it.
                if !tree.directory_nodes.contains_key(component) {
                    let dir_node = pfs::simple();
                    let routing_facade = tree.routing_facade.clone();
                    let child_tree = CapabilityUsageTree::new(dir_node.clone(), routing_facade);
                    tree.dir.add_node(component, dir_node, abs_moniker)?;
                    tree.directory_nodes.insert(component.to_string(), child_tree);
                }

                tree = tree
                    .directory_nodes
                    .get_mut(component)
                    .expect("to_directory_node: it is impossible for this tree to not exist.");
            }
        }
        Ok(tree)
    }
}

/// Represents the directory hierarchy of the exposed directory, not including the nodes for the
/// capabilities themselves.
pub(super) struct DirTree {
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
    ) -> Self {
        let mut tree = DirTree { directory_nodes: HashMap::new(), broker_nodes: HashMap::new() };
        for use_ in decl.uses {
            tree.add_use_capability(&routing_factory, abs_moniker, &use_);
        }
        tree
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
        root_dir: &mut impl AddableDirectory,
    ) -> Result<(), ModelError> {
        for (name, subtree) in self.directory_nodes {
            let mut subdir = pfs::simple();
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
    ) {
        let path = match use_.path() {
            Some(path) => path,
            None => return,
        };
        let tree = self.to_directory_node(path);
        let routing_fn = routing_factory(abs_moniker.clone(), use_.clone());
        tree.broker_nodes.insert(path.basename.to_string(), routing_fn);
    }

    fn add_expose_capability(
        &mut self,
        routing_factory: &impl Fn(AbsoluteMoniker, ExposeDecl) -> RoutingFn,
        abs_moniker: &AbsoluteMoniker,
        expose: &ExposeDecl,
    ) {
        let path = match expose {
            cm_rust::ExposeDecl::Service(d) => &d.target_path,
            cm_rust::ExposeDecl::Protocol(d) => &d.target_path,
            cm_rust::ExposeDecl::Directory(d) => &d.target_path,
            cm_rust::ExposeDecl::Runner(_) => {
                // Runners do not add directory entries.
                return;
            }
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
        crate::model::testing::{mocks, test_helpers, test_helpers::*},
        cm_rust::{
            CapabilityName, CapabilityPath, ExposeDecl, ExposeDirectoryDecl, ExposeProtocolDecl,
            ExposeRunnerDecl, ExposeSource, ExposeTarget, UseDecl, UseDirectoryDecl,
            UseProtocolDecl, UseRunnerDecl, UseSource, UseStorageDecl,
        },
        fidl::endpoints::{ClientEnd, ServerEnd},
        fidl_fuchsia_io::MODE_TYPE_DIRECTORY,
        fidl_fuchsia_io::{DirectoryMarker, NodeMarker, OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE},
        fidl_fuchsia_io2 as fio2,
        fuchsia_async::EHandle,
        fuchsia_vfs_pseudo_fs_mt::{
            directory::entry::DirectoryEntry, execution_scope::ExecutionScope, path,
        },
        fuchsia_zircon as zx,
        std::convert::TryFrom,
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
                    rights: fio2::Operations::Connect,
                    subdir: None,
                }),
                UseDecl::Protocol(UseProtocolDecl {
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
                UseDecl::Runner(UseRunnerDecl { source_name: CapabilityName::from("elf") }),
            ],
            ..default_component_decl()
        };
        let abs_moniker = AbsoluteMoniker::root();
        let tree = DirTree::build_from_uses(routing_factory, &abs_moniker, decl.clone());

        // Convert the tree to a directory.
        let mut in_dir = pfs::simple();
        tree.install(&abs_moniker, &mut in_dir).expect("Unable to build pseudodirectory");
        let (in_dir_client, in_dir_server) = zx::Channel::create().unwrap();
        in_dir.open(
            ExecutionScope::from_executor(Box::new(EHandle::local())),
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            MODE_TYPE_DIRECTORY,
            path::Path::empty(),
            ServerEnd::<NodeMarker>::new(in_dir_server.into()),
        );
        let in_dir_proxy = ClientEnd::<DirectoryMarker>::new(in_dir_client)
            .into_proxy()
            .expect("failed to create directory proxy");
        assert_eq!(
            vec!["in/data/cache", "in/data/hippo", "in/data/persistent", "in/svc/hippo"],
            test_helpers::list_directory_recursive(&in_dir_proxy).await
        );

        // Expect that calls on the directory nodes reach the mock directory/service.
        assert_eq!("friend", test_helpers::read_file(&in_dir_proxy, "in/data/hippo/hello").await);
        assert_eq!(
            "friend",
            test_helpers::read_file(&in_dir_proxy, "in/data/persistent/hello").await
        );
        assert_eq!("friend", test_helpers::read_file(&in_dir_proxy, "in/data/cache/hello").await);
        assert_eq!(
            "hippos".to_string(),
            test_helpers::call_echo(&in_dir_proxy, "in/svc/hippo").await
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
                    target: ExposeTarget::Realm,
                    rights: Some(fio2::Operations::Connect),
                    subdir: None,
                }),
                ExposeDecl::Directory(ExposeDirectoryDecl {
                    source: ExposeSource::Self_,
                    source_path: CapabilityPath::try_from("/data/foo").unwrap(),
                    target_path: CapabilityPath::try_from("/in/data/bar").unwrap(),
                    target: ExposeTarget::Realm,
                    rights: Some(fio2::Operations::Connect),
                    subdir: None,
                }),
                ExposeDecl::Protocol(ExposeProtocolDecl {
                    source: ExposeSource::Self_,
                    source_path: CapabilityPath::try_from("/svc/baz").unwrap(),
                    target_path: CapabilityPath::try_from("/in/svc/hippo").unwrap(),
                    target: ExposeTarget::Realm,
                }),
                ExposeDecl::Runner(ExposeRunnerDecl {
                    source: ExposeSource::Self_,
                    source_name: CapabilityName::from("elf"),
                    target: ExposeTarget::Realm,
                    target_name: CapabilityName::from("elf"),
                }),
            ],
            ..default_component_decl()
        };
        let abs_moniker = AbsoluteMoniker::root();
        let tree = DirTree::build_from_exposes(routing_factory, &abs_moniker, decl.clone());

        // Convert the tree to a directory.
        let mut expose_dir = pfs::simple();
        tree.install(&abs_moniker, &mut expose_dir).expect("Unable to build pseudodirectory");
        let (expose_dir_client, expose_dir_server) = zx::Channel::create().unwrap();
        expose_dir.open(
            ExecutionScope::from_executor(Box::new(EHandle::local())),
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            MODE_TYPE_DIRECTORY,
            path::Path::empty(),
            ServerEnd::<NodeMarker>::new(expose_dir_server.into()),
        );
        let expose_dir_proxy = ClientEnd::<DirectoryMarker>::new(expose_dir_client)
            .into_proxy()
            .expect("failed to create directory proxy");
        assert_eq!(
            vec!["in/data/bar", "in/data/hippo", "in/svc/hippo"],
            test_helpers::list_directory_recursive(&expose_dir_proxy).await
        );

        // Expect that calls on the directory nodes reach the mock directory/service.
        assert_eq!("friend", test_helpers::read_file(&expose_dir_proxy, "in/data/bar/hello").await);
        assert_eq!(
            "friend",
            test_helpers::read_file(&expose_dir_proxy, "in/data/hippo/hello").await
        );
        assert_eq!(
            "hippos".to_string(),
            test_helpers::call_echo(&expose_dir_proxy, "in/svc/hippo").await
        );
    }
}

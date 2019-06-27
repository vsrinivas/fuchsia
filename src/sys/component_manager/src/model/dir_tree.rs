// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::directory_broker::{DirectoryBroker, RoutingFn},
    crate::model::addable_directory::AddableDirectory,
    crate::model::*,
    cm_rust::{Capability, ComponentDecl, ExposeDecl},
    fuchsia_vfs_pseudo_fs::directory,
    log::*,
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
        routing_factory: impl Fn(AbsoluteMoniker, Capability) -> RoutingFn,
        abs_moniker: &AbsoluteMoniker,
        decl: ComponentDecl,
    ) -> Result<Self, ModelError> {
        let mut tree = DirTree { directory_nodes: HashMap::new(), broker_nodes: HashMap::new() };
        for use_ in decl.uses {
            let capability = match use_ {
                cm_rust::UseDecl::Directory(d) => Capability::Directory(d.target_path),
                cm_rust::UseDecl::Service(d) => Capability::Service(d.target_path),
                cm_rust::UseDecl::Storage(_) => {
                    error!("storage capabilities are not supported");
                    return Err(ModelError::ComponentInvalid);
                }
            };
            tree.add_capability(&routing_factory, abs_moniker, &capability);
        }
        Ok(tree)
    }

    /// Builds a directory hierarchy from a component's `exposes` declarations.
    /// `routing_factory` is a closure that generates the routing function that will be called
    /// when a leaf node is opened.
    pub fn build_from_exposes(
        routing_factory: impl Fn(AbsoluteMoniker, Capability) -> RoutingFn,
        abs_moniker: &AbsoluteMoniker,
        decl: ComponentDecl,
    ) -> Self {
        let mut tree = DirTree { directory_nodes: HashMap::new(), broker_nodes: HashMap::new() };
        for expose in decl.exposes {
            let capability = match expose {
                ExposeDecl::Service(d) => Capability::Service(d.target_path),
                ExposeDecl::Directory(d) => Capability::Directory(d.target_path),
            };
            tree.add_capability(&routing_factory, abs_moniker, &capability);
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

    fn add_capability(
        &mut self,
        routing_factory: &impl Fn(AbsoluteMoniker, Capability) -> RoutingFn,
        abs_moniker: &AbsoluteMoniker,
        capability: &Capability,
    ) {
        let path = capability.path().expect("missing path");
        let components = path.dirname.split("/");
        let mut tree = self;
        for component in components {
            if !component.is_empty() {
                tree = tree.directory_nodes.entry(component.to_string()).or_insert(Box::new(
                    DirTree { directory_nodes: HashMap::new(), broker_nodes: HashMap::new() },
                ));
            }
        }
        let routing_fn = routing_factory(abs_moniker.clone(), capability.clone());
        tree.broker_nodes.insert(path.basename.to_string(), routing_fn);
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::model::testing::{mocks, routing_test_helpers::default_component_decl, test_utils},
        cm_rust::{
            CapabilityPath, ExposeDecl, ExposeDirectoryDecl, ExposeServiceDecl, ExposeSource,
            UseDecl, UseDirectoryDecl, UseServiceDecl,
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
        let routing_factory = mocks::proxying_routing_factory();
        let decl = ComponentDecl {
            uses: vec![
                UseDecl::Directory(UseDirectoryDecl {
                    source_path: CapabilityPath::try_from("/data/baz").unwrap(),
                    target_path: CapabilityPath::try_from("/in/data/hippo").unwrap(),
                }),
                UseDecl::Service(UseServiceDecl {
                    source_path: CapabilityPath::try_from("/svc/baz").unwrap(),
                    target_path: CapabilityPath::try_from("/in/svc/hippo").unwrap(),
                }),
                UseDecl::Directory(UseDirectoryDecl {
                    source_path: CapabilityPath::try_from("/data/foo").unwrap(),
                    target_path: CapabilityPath::try_from("/in/data/bar").unwrap(),
                }),
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
            let _ = await!(in_dir);
        });
        let in_dir_proxy = ClientEnd::<DirectoryMarker>::new(in_dir_client)
            .into_proxy()
            .expect("failed to create directory proxy");
        assert_eq!(
            vec!["in/data/bar", "in/data/hippo", "in/svc/hippo"],
            await!(test_utils::list_directory_recursive(&in_dir_proxy))
        );

        // Expect that calls on the directory nodes reach the mock directory/service.
        assert_eq!("friend", await!(test_utils::read_file(&in_dir_proxy, "in/data/bar/hello")));
        assert_eq!("friend", await!(test_utils::read_file(&in_dir_proxy, "in/data/hippo/hello")));
        assert_eq!(
            "hippos".to_string(),
            await!(test_utils::call_echo(&in_dir_proxy, "in/svc/hippo"))
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn build_from_exposes() {
        // Call `build_from_uses` with a routing factory that routes to a mock directory or service,
        // and a `ComponentDecl` with `expose` declarations.
        let routing_factory = mocks::proxying_routing_factory();
        let decl = ComponentDecl {
            exposes: vec![
                ExposeDecl::Directory(ExposeDirectoryDecl {
                    source: ExposeSource::Self_,
                    source_path: CapabilityPath::try_from("/data/baz").unwrap(),
                    target_path: CapabilityPath::try_from("/in/data/hippo").unwrap(),
                }),
                ExposeDecl::Service(ExposeServiceDecl {
                    source: ExposeSource::Self_,
                    source_path: CapabilityPath::try_from("/svc/baz").unwrap(),
                    target_path: CapabilityPath::try_from("/in/svc/hippo").unwrap(),
                }),
                ExposeDecl::Directory(ExposeDirectoryDecl {
                    source: ExposeSource::Self_,
                    source_path: CapabilityPath::try_from("/data/foo").unwrap(),
                    target_path: CapabilityPath::try_from("/in/data/bar").unwrap(),
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
            let _ = await!(expose_dir);
        });
        let expose_dir_proxy = ClientEnd::<DirectoryMarker>::new(expose_dir_client)
            .into_proxy()
            .expect("failed to create directory proxy");
        assert_eq!(
            vec!["in/data/bar", "in/data/hippo", "in/svc/hippo"],
            await!(test_utils::list_directory_recursive(&expose_dir_proxy))
        );

        // Expect that calls on the directory nodes reach the mock directory/service.
        assert_eq!("friend", await!(test_utils::read_file(&expose_dir_proxy, "in/data/bar/hello")));
        assert_eq!(
            "friend",
            await!(test_utils::read_file(&expose_dir_proxy, "in/data/hippo/hello"))
        );
        assert_eq!(
            "hippos".to_string(),
            await!(test_utils::call_echo(&expose_dir_proxy, "in/svc/hippo"))
        );
    }
}

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::directory_broker::{DirectoryBroker, RoutingFn},
    crate::model::{
        error::ModelError, moniker::AbsoluteMoniker, routing_fn_factory::RoutingFnFactory,
    },
    cm_rust::{Capability, ComponentDecl},
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
    pub fn build_from_uses(
        route_fn_factory: RoutingFnFactory,
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
            tree.add_capability(&route_fn_factory, abs_moniker, capability);
        }
        Ok(tree)
    }

    /// Installs the directory tree into `root_dir`.
    pub fn install(
        self,
        abs_moniker: &AbsoluteMoniker,
        root_dir: &mut directory::simple::Simple<'static>,
    ) -> Result<(), ModelError> {
        for (name, subtree) in self.directory_nodes {
            let mut subdir = directory::simple::empty();
            subtree.install(abs_moniker, &mut subdir)?;
            root_dir
                .add_entry(&name, subdir)
                .map_err(|_| ModelError::add_entry_error(abs_moniker.clone(), &name as &str))?;
        }
        for (name, route_fn) in self.broker_nodes {
            let node = DirectoryBroker::new(route_fn);
            root_dir
                .add_entry(&name, node)
                .map_err(|_| ModelError::add_entry_error(abs_moniker.clone(), &name as &str))?;
        }
        Ok(())
    }

    fn add_capability(
        &mut self,
        route_fn_factory: &RoutingFnFactory,
        abs_moniker: &AbsoluteMoniker,
        capability: Capability,
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
        tree.broker_nodes.insert(
            path.basename.to_string(),
            route_fn_factory.create_route_fn(&abs_moniker, capability),
        );
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::model::testing::{mocks, routing_test_helpers::default_component_decl, test_utils},
        cm_rust::{CapabilityPath, UseDecl, UseDirectoryDecl, UseServiceDecl},
        fidl::endpoints::{ClientEnd, ServerEnd},
        fidl_fuchsia_io::{DirectoryMarker, NodeMarker, OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE},
        fuchsia_async as fasync,
        fuchsia_vfs_pseudo_fs::{
            directory::{self, entry::DirectoryEntry},
            file::simple::read_only,
        },
        fuchsia_zircon as zx,
        std::{convert::TryFrom, iter, sync::Arc},
    };

    #[fuchsia_async::run_singlethreaded(test)]
    async fn make_in_directory() {
        // Make a directory tree that will be forwarded to by ProxyingRoutingFnFactory.
        let mut sub_dir = directory::simple::empty();
        let (sub_dir_client, sub_dir_server) = zx::Channel::create().unwrap();
        sub_dir.open(
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            0,
            &mut iter::empty(),
            ServerEnd::<NodeMarker>::new(sub_dir_server.into()),
        );

        // Add a 'hello' file in the subdirectory for testing purposes.
        sub_dir
            .add_entry("hello", { read_only(move || Ok(b"friend".to_vec())) })
            .map_err(|(s, _)| s)
            .expect("Failed to add 'hello' entry");

        let sub_dir_proxy = ClientEnd::<DirectoryMarker>::new(sub_dir_client)
            .into_proxy()
            .expect("failed to create directory proxy");
        fasync::spawn(async move {
            let _ = await!(sub_dir);
        });

        let route_fn_factory = Arc::new(mocks::ProxyingRoutingFnFactory::new(sub_dir_proxy));
        let decl = ComponentDecl {
            uses: vec![
                UseDecl::Directory(UseDirectoryDecl {
                    source_path: CapabilityPath::try_from("/data/baz").unwrap(),
                    target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                }),
                UseDecl::Service(UseServiceDecl {
                    source_path: CapabilityPath::try_from("/svc/baz").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                }),
                UseDecl::Directory(UseDirectoryDecl {
                    source_path: CapabilityPath::try_from("/data/foo").unwrap(),
                    target_path: CapabilityPath::try_from("/data/bar").unwrap(),
                }),
            ],
            ..default_component_decl()
        };
        let abs_moniker = AbsoluteMoniker::root();
        let tree = DirTree::build_from_uses(route_fn_factory, &abs_moniker, decl.clone())
            .expect("Unable to build 'uses' directory");

        let mut in_dir = directory::simple::empty();
        tree.install(&abs_moniker, &mut in_dir).expect("Unable to build pseudodirectory");
        let (in_dir_client, in_dir_server) = zx::Channel::create().unwrap();
        in_dir.open(
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            0,
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
            vec!["data/bar", "data/hippo", "svc/hippo"],
            await!(test_utils::list_directory_recursive(&in_dir_proxy))
        );

        // All entries in the in directory lead to foo as a result of ProxyingRoutingFnFactory.
        assert_eq!("friend", await!(test_utils::read_file(&in_dir_proxy, "data/bar/hello")));
        assert_eq!("friend", await!(test_utils::read_file(&in_dir_proxy, "data/hippo/hello")));
        assert_eq!("friend", await!(test_utils::read_file(&in_dir_proxy, "svc/hippo/hello")));
    }

}

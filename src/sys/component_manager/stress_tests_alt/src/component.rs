// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl::endpoints::create_proxy,
    fidl_fuchsia_io as fio, fidl_fuchsia_sys2 as fsys,
    fuchsia_component::client::connect_to_protocol_at_dir_root,
    futures::future::BoxFuture,
    rand::{rngs::SmallRng, Rng},
};

pub struct Component {
    name: String,
    children: Vec<Component>,
    realm_svc: fsys::RealmProxy,
}

const COLLECTION_NAME: &'static str = "children";
const TREE_COMPONENT_URL: &'static str =
    "fuchsia-pkg://fuchsia.com/component-manager-stress-tests-alt#meta/dynamic_child_component.cm";

impl Component {
    /// Create a new component called `name` with a predefined collection and URL
    /// using the parent's fuchsia.sys2.Realm service
    pub async fn new(parent_realm_svc: &fsys::RealmProxy, name: impl ToString) -> Self {
        let name = name.to_string();

        let decl = fsys::ChildDecl {
            name: Some(name.clone()),
            url: Some(TREE_COMPONENT_URL.to_string()),
            startup: Some(fsys::StartupMode::Lazy),
            ..fsys::ChildDecl::EMPTY
        };

        let mut coll_ref = fsys::CollectionRef { name: COLLECTION_NAME.to_string() };

        parent_realm_svc
            .create_child(&mut coll_ref, decl)
            .await
            .expect("Could not send FIDL request to create child component")
            .expect("Could not create child component");

        let mut child_ref =
            fsys::ChildRef { name: name.clone(), collection: Some(COLLECTION_NAME.to_string()) };

        let (proxy, server_end) = create_proxy::<fio::DirectoryMarker>().unwrap();

        parent_realm_svc
            .bind_child(&mut child_ref, server_end)
            .await
            .expect("Could not send FIDL request to bind to child component")
            .expect("Could not bind to child component");

        let child_realm_svc = connect_to_protocol_at_dir_root::<fsys::RealmMarker>(&proxy)
            .expect("Could not open Realm protocol in exposed directory of child component");

        Self { name, children: vec![], realm_svc: child_realm_svc }
    }

    /// Create a child of this component. `rng` is used to generate a random name for the child
    /// component.
    pub async fn add_child(&mut self, rng: &mut SmallRng) {
        let name = format!("C{}", rng.gen::<u64>());
        let child = Component::new(&self.realm_svc, name).await;
        self.children.push(child);
    }

    /// Traverse the topology and at a random position, add a new component.
    /// `rng` is used for traversal and for generating the name of the child component.
    pub fn traverse_and_add_random_child<'a>(
        &'a mut self,
        rng: &'a mut SmallRng,
    ) -> BoxFuture<'a, ()> {
        Box::pin(async move {
            let num_children = self.children.len();

            if self.has_children() && rng.gen_bool(0.75) {
                // We have children and chose to traverse.
                // Bias towards traversal. This will encourage depth in the tree.
                let index = rng.gen_range(0, num_children);
                let child = self.children.get_mut(index).unwrap();
                child.traverse_and_add_random_child(rng).await;
            } else {
                // We do not have children or did not choose to traverse.
                // Create the child at this node
                self.add_child(rng).await;
            }
        })
    }

    /// true if the component has children
    pub fn has_children(&self) -> bool {
        !self.children.is_empty()
    }

    /// Returns the number of child components in this entire subtree.
    pub fn num_descendants(&self) -> usize {
        let mut num_children = self.children.len();
        for child in &self.children {
            num_children += child.num_descendants();
        }
        return num_children;
    }

    /// Traverse the topology and at a random position, delete a component.
    /// `rng` is used for traversal.
    pub fn traverse_and_destroy_random_child<'a>(
        &'a mut self,
        rng: &'a mut SmallRng,
    ) -> BoxFuture<'a, ()> {
        Box::pin(async move {
            // Pick a random child
            let num_children = self.children.len();
            let index = rng.gen_range(0, num_children);
            let child = self.children.get_mut(index).unwrap();

            if child.has_children() && rng.gen_bool(0.5) {
                // The child has children and we chose to traverse.
                child.traverse_and_destroy_random_child(rng).await;
            } else {
                // The child does not have children or we did not choose to traverse.
                // Destroy the child.
                let mut child_ref = fsys::ChildRef {
                    name: child.name.clone(),
                    collection: Some(COLLECTION_NAME.to_string()),
                };
                self.realm_svc
                    .destroy_child(&mut child_ref)
                    .await
                    .expect("Could not send FIDL request to destroy child")
                    .expect("Could not destroy child");
                self.children.remove(index);
            }
        })
    }
}

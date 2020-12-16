// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    cm_rust::{ChildDecl, ComponentDecl},
    fidl_fuchsia_sys2::StartupMode,
    moniker::PartialMoniker,
    std::{boxed::Box, cell::RefCell, collections::HashMap, convert::Into, sync::Arc, sync::Weak},
    thiserror::Error,
};

/// Errors that may occur while building or operating on a `ComponentTree`.
#[derive(Error, Debug)]
pub enum ComponentTreeError {
    #[error("no component declaration found for url `{0}`")]
    ComponentDeclNotFound(String),
    #[error("invalid child declaration in component declaration with url `{0}`")]
    InvalidChildDecl(String),
    #[error("no child node found with moniker `{0}`")]
    ComponentNodeNotFound(PartialMoniker),
}

pub struct ComponentNode {
    pub decl: Box<ComponentDecl>,
    parent: Weak<ComponentNode>,
    children: RefCell<HashMap<PartialMoniker, Arc<ComponentNode>>>,
}

pub struct ComponentTree {
    pub root: Arc<ComponentNode>,
}

pub struct ComponentTreeBuilder {
    decls_by_url: HashMap<String, ComponentDecl>,
}

impl ComponentTreeBuilder {
    /// Constructs a `ComponentTreeBuilder` from a map of component declarations keyed by
    /// component url.
    pub fn new(decls_by_url: HashMap<String, ComponentDecl>) -> Self {
        ComponentTreeBuilder { decls_by_url: decls_by_url }
    }

    fn new_root_node(decl: Box<ComponentDecl>) -> Arc<ComponentNode> {
        Arc::new(ComponentNode {
            decl: decl,
            parent: Weak::new(),
            children: RefCell::new(HashMap::new()),
        })
    }

    fn new_child_node(decl: Box<ComponentDecl>, parent: &Arc<ComponentNode>) -> Arc<ComponentNode> {
        Arc::new(ComponentNode {
            decl: decl,
            parent: Arc::downgrade(parent),
            children: RefCell::new(HashMap::new()),
        })
    }

    /// Constructs and returns a `ComponentTree` based at the component with url `root_url`,
    /// consuming the builder. Returns an error if `root_url` or the url of any subsequent
    /// child is not present in the builder's `decls_by_url` map, or if any `ComponentDecl`
    /// in `decls_by_url` contains an invalid `ChildDecl`.
    pub fn build<T: Into<String>>(
        mut self,
        root_url: T,
    ) -> Result<ComponentTree, ComponentTreeError> {
        let root_url = root_url.into();
        match self.decls_by_url.remove(&root_url) {
            Some(root_decl) => {
                let root_node = ComponentTreeBuilder::new_root_node(Box::new(root_decl));
                if let Err(err) = self.add_descendants(&root_node) {
                    return Err(err);
                }
                Ok(ComponentTree { root: root_node })
            }
            None => Err(ComponentTreeError::ComponentDeclNotFound(root_url)),
        }
    }

    fn add_descendants(&mut self, node: &Arc<ComponentNode>) -> Result<(), ComponentTreeError> {
        for child in node.decl.children.iter() {
            if child.name.is_empty() {
                return Err(ComponentTreeError::InvalidChildDecl(child.url.to_string()));
            }
            match self.decls_by_url.remove(&child.url) {
                Some(child_decl) => {
                    let child_node =
                        ComponentTreeBuilder::new_child_node(Box::new(child_decl), node);
                    if let Err(err) = self.add_descendants(&child_node) {
                        return Err(err);
                    }
                    node.children
                        .borrow_mut()
                        .insert(PartialMoniker::new(child.name.clone(), None), child_node);
                }
                None => {
                    return Err(ComponentTreeError::ComponentDeclNotFound(child.url.to_string()))
                }
            };
        }
        Ok(())
    }
}

impl ComponentNode {
    /// Returns the child node corresponding to `moniker`, or an error if `moniker` does not
    /// match any child of this `ComponentNode`.
    pub fn get_child_node(
        self: &Arc<Self>,
        moniker: &PartialMoniker,
    ) -> Result<Arc<ComponentNode>, ComponentTreeError> {
        match self.children.borrow().get(moniker) {
            Some(child_node) => Ok(Arc::clone(child_node)),
            None => Err(ComponentTreeError::ComponentNodeNotFound(moniker.clone())),
        }
    }

    /// Returns the parent of this `ComponentNode`, or `None` if the node has no parent.
    pub fn try_get_parent(self: &Arc<Self>) -> Option<Arc<ComponentNode>> {
        self.parent.upgrade()
    }
}

impl ComponentTree {
    /// Given a sequence of `PartialMonikers`, attempts to follow the corresponding path
    /// down the `ComponentTree` starting at the root node. The vector `monikers` corresponds
    /// to a valid path for this ComponentTree if, for each i > 0, the i-th element is the
    /// `PartialMoniker` of some child of a node whose `PartialMoniker` is the (i-1)-th element.
    ///
    /// Returns the `ComponentNode` whose `PartialMoniker` is the final element of `monikers` if
    /// the path is valid and nonempty, or returns the root node if the `monikers` is empty. Returns
    /// an error if the path is invalid.
    pub fn get_node(
        &self,
        monikers: &Vec<&PartialMoniker>,
    ) -> Result<Arc<ComponentNode>, ComponentTreeError> {
        let mut node = Arc::clone(&self.root);
        for moniker in monikers.iter() {
            match node.get_child_node(&moniker) {
                Ok(next_node) => {
                    node = Arc::clone(&next_node);
                }
                Err(err) => return Err(err),
            };
        }
        Ok(node)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn new_child_decl(name: &String, url: &String) -> ChildDecl {
        ChildDecl {
            name: name.to_string(),
            url: url.to_string(),
            startup: StartupMode::Lazy,
            environment: None,
        }
    }

    fn new_component_decl(children: Vec<ChildDecl>) -> ComponentDecl {
        ComponentDecl {
            program: None,
            uses: vec![],
            exposes: vec![],
            offers: vec![],
            capabilities: vec![],
            children: children,
            collections: vec![],
            facets: None,
            environments: vec![],
        }
    }

    fn new_root_node(decl: ComponentDecl) -> ComponentNode {
        ComponentNode {
            decl: Box::new(decl),
            parent: Weak::new(),
            children: RefCell::new(HashMap::new()),
        }
    }

    fn new_child_node(decl: ComponentDecl, parent: &Arc<ComponentNode>) -> ComponentNode {
        ComponentNode {
            decl: Box::new(decl),
            parent: Arc::downgrade(parent),
            children: RefCell::new(HashMap::new()),
        }
    }

    #[test]
    // Checks that the `get_child_node` method successfully returns a child
    // of a ComponentNode, and returns the expected error when a nonexistant
    // child node is requested.
    fn get_child_node() {
        let root_decl = new_component_decl(vec![]);
        let root_node = Arc::new(new_root_node(root_decl.clone()));

        let foo_decl = new_component_decl(vec![]);
        let foo_node = Arc::new(new_child_node(foo_decl.clone(), &root_node));
        let foo_moniker = PartialMoniker::new("foo".to_string(), None);

        root_node.children.borrow_mut().insert(foo_moniker.clone(), foo_node);

        let valid_child = root_node.get_child_node(&foo_moniker);
        assert!(valid_child.is_ok());
        assert_eq!(*valid_child.unwrap().decl, foo_decl);

        let other_moniker = PartialMoniker::new("other".to_string(), None);
        let invalid_child = root_node.get_child_node(&other_moniker);
        assert!(
            invalid_child.is_err(),
            ComponentTreeError::ComponentNodeNotFound(other_moniker.clone())
        );
    }

    #[test]
    // Checks that the `try_get_parent` method successfully returns the parent
    // of a `ComponentNode`, and returns `None` when called on a `ComponentNode`
    // without a parent.
    fn try_get_parent_node() {
        let root_decl = new_component_decl(vec![]);
        let root_node = Arc::new(new_root_node(root_decl.clone()));

        let foo_decl = new_component_decl(vec![]);
        let foo_node = Arc::new(new_child_node(foo_decl.clone(), &root_node));

        let has_parent = foo_node.try_get_parent();
        assert!(has_parent.is_some());
        assert_eq!(*has_parent.unwrap().decl, root_decl);

        let no_parent = root_node.try_get_parent();
        assert!(no_parent.is_none());
    }

    #[test]
    // Builds a tree with a single node and checks that the node can be looked up at an
    // empty sequence of PartialMonikers.
    fn build_tree_and_look_up_single_node() {
        let root_url = "root_url".to_string();
        let root_decl = new_component_decl(vec![]);
        let mut decls = HashMap::new();
        decls.insert(root_url.clone(), root_decl.clone());
        let builder = ComponentTreeBuilder::new(decls);
        let build_tree_result = builder.build(root_url);
        assert!(build_tree_result.is_ok());

        let get_root_result = build_tree_result.unwrap().get_node(&vec![]);
        assert!(get_root_result.is_ok());

        let root_node = get_root_result.unwrap();
        assert_eq!(*root_node.decl, root_decl);
        assert!(root_node.try_get_parent().is_none());
        assert!(root_node.children.borrow().is_empty());
    }

    #[test]
    // Checks that the expected error is returned when the ComponentTreeBuilder's
    // `build` method is called with an unrecognized component url.
    fn build_tree_root_url_not_found() {
        let root_url = "root_url".to_string();
        let other_url = "other_url".to_string();
        let mut decls = HashMap::new();
        decls.insert(root_url.clone(), new_component_decl(vec![]));
        let builder = ComponentTreeBuilder::new(decls);
        let result = builder.build(other_url.clone());
        assert!(result.is_err(), ComponentTreeError::ComponentDeclNotFound(other_url));
    }

    #[test]
    // Builds a tree with 4 nodes:
    //
    //          root
    //         /    \
    //       foo    bar
    //       /
    //     baz
    //
    // Checks that each node is populated correctly and can be looked up by a sequence
    // of PartialMonikers. Also checks that lookup fails with an invalid sequence of
    // PartialMonikers.
    fn build_tree_and_look_up_multi_node() {
        let root_url = "root_url".to_string();
        let foo_url = "foo_url".to_string();
        let bar_url = "bar_url".to_string();
        let baz_url = "baz_url".to_string();

        let foo_name = "foo".to_string();
        let bar_name = "bar".to_string();
        let baz_name = "baz".to_string();

        let root_decl = new_component_decl(vec![
            new_child_decl(&foo_name, &foo_url),
            new_child_decl(&bar_name, &bar_url),
        ]);
        let foo_decl = new_component_decl(vec![new_child_decl(&baz_name, &baz_url)]);
        let bar_decl = new_component_decl(vec![]);
        let baz_decl = new_component_decl(vec![]);

        let mut decls = HashMap::new();
        decls.insert(root_url.to_string(), root_decl.clone());
        decls.insert(foo_url.to_string(), foo_decl.clone());
        decls.insert(bar_url.to_string(), bar_decl.clone());
        decls.insert(baz_url.to_string(), baz_decl.clone());

        let builder = ComponentTreeBuilder::new(decls);
        let build_tree_result = builder.build(root_url);
        assert!(build_tree_result.is_ok());
        let tree = build_tree_result.unwrap();

        let foo_moniker = PartialMoniker::new(foo_name.clone(), None);
        let bar_moniker = PartialMoniker::new(bar_name.clone(), None);
        let baz_moniker = PartialMoniker::new(baz_name.clone(), None);

        let get_root_result = tree.get_node(&vec![]);
        assert!(get_root_result.is_ok());
        let root_node = get_root_result.unwrap();
        assert_eq!(*root_node.decl, root_decl);
        assert_eq!(root_node.children.borrow().len(), 2);
        assert!(root_node.try_get_parent().is_none());

        let get_foo_result = tree.get_node(&vec![&foo_moniker]);
        assert!(get_foo_result.is_ok());
        let foo_node = get_foo_result.unwrap();
        assert_eq!(*foo_node.decl, foo_decl);
        assert_eq!(foo_node.children.borrow().len(), 1);
        let foo_parent = foo_node.try_get_parent();
        assert!(foo_parent.is_some());
        assert_eq!(*foo_parent.unwrap().decl, root_decl);

        let get_bar_result = tree.get_node(&vec![&bar_moniker]);
        assert!(get_bar_result.is_ok());
        let bar_node = get_bar_result.unwrap();
        assert_eq!(*bar_node.decl, bar_decl);
        assert!(bar_node.children.borrow().is_empty());
        let bar_parent = bar_node.try_get_parent();
        assert!(bar_parent.is_some());
        assert_eq!(*bar_parent.unwrap().decl, root_decl);

        let get_baz_result = tree.get_node(&vec![&foo_moniker, &baz_moniker]);
        assert!(get_baz_result.is_ok());
        let baz_node = get_baz_result.unwrap();
        assert_eq!(*baz_node.decl, baz_decl);
        assert!(baz_node.children.borrow().is_empty());
        let baz_parent = baz_node.try_get_parent();
        assert!(baz_parent.is_some());
        assert_eq!(*baz_parent.unwrap().decl, foo_decl);

        let other_moniker = PartialMoniker::new("other".to_string(), None);
        assert!(
            tree.get_node(&vec![&other_moniker]).is_err(),
            ComponentTreeError::ComponentNodeNotFound(other_moniker.clone())
        );
        assert!(
            tree.get_node(&vec![&foo_moniker, &other_moniker]).is_err(),
            ComponentTreeError::ComponentNodeNotFound(other_moniker.clone())
        );
    }
}

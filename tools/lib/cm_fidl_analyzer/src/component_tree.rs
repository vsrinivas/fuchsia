// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    cm_rust::{ChildDecl, ComponentDecl},
    fidl_fuchsia_sys2::StartupMode,
    moniker::PartialMoniker,
    std::{
        boxed::Box,
        cell::RefCell,
        collections::{HashMap, VecDeque},
        convert::Into,
        sync::{Arc, Weak},
    },
    thiserror::Error,
};

/// Errors that may occur while building or operating on a `ComponentTree`.
#[derive(Error, Debug)]
pub enum ComponentTreeError {
    #[error("no component declaration found for url `{0}`")]
    ComponentDeclNotFound(String),
    #[error("invalid child declaration in component declaration with url `{0}`")]
    InvalidChildDecl(String),
    #[error("no child node with moniker `{0}` found for node `{1}`")]
    ChildNodeNotFound(String, String),
}

pub struct ComponentNode {
    pub decl: Box<ComponentDecl>,
    pub moniker_path: Vec<PartialMoniker>,
    parent: Weak<ComponentNode>,
    children: RefCell<HashMap<PartialMoniker, Arc<ComponentNode>>>,
}

pub struct ComponentTree {
    pub root: Arc<ComponentNode>,
}

pub struct ComponentTreeBuilder {
    decls_by_url: HashMap<String, ComponentDecl>,
}

/// The `ComponentNodeVisitor` trait defines an interface for operating on a `ComponentNode`.
/// An error should be returned if the operation fails.
pub trait ComponentNodeVisitor {
    fn visit_node(&mut self, node: &Arc<ComponentNode>) -> Result<(), anyhow::Error>;
}

/// The `ComponentTreeWalker` trait defines an interface for iteratively operating on nodes
/// of a `ComponentTree`, given a type implementing a per-node operation via the
/// `ComponentNodeVisitor` trait.
pub trait ComponentTreeWalker {
    /// Walks a `ComponentTree`, doing the operation implemented by `visitor` at each node.
    /// If the operation fails at a node, terminates the walk and propagates the error.
    fn walk<T: ComponentNodeVisitor>(
        &mut self,
        tree: &ComponentTree,
        visitor: &mut T,
    ) -> Result<(), anyhow::Error> {
        let mut node = Arc::clone(&tree.root);
        loop {
            visitor.visit_node(&node)?;
            match self.get_next_node()? {
                Some(next_node) => {
                    node = Arc::clone(&next_node);
                }
                None => {
                    return Ok(());
                }
            }
        }
    }

    /// Returns a `Some(next_node)` containing the node following `node` in some enumeration of a
    /// subset of a `ComponentTree`'s nodes, or returns `None` if `node` is the final node. If an
    /// error occurred while trying to get the next node, returns that error instead.
    fn get_next_node(&mut self) -> Result<Option<Arc<ComponentNode>>, anyhow::Error>;
}

/// A walker implementing breadth-first traversal of a full `ComponentTree`, starting at the root
/// node.
pub struct BreadthFirstWalker {
    discovered: VecDeque<Arc<ComponentNode>>,
}

impl ComponentNode {
    /// Returns a string representing the path to this `ComponentNode` from the root node.
    /// Each path segment is the `PartialMoniker` of a component relative to its parent.
    /// The root node is represented as "/".
    pub fn short_display(&self) -> String {
        if self.moniker_path.len() == 0 {
            return "/".to_owned();
        }
        let mut display = "".to_owned();
        for moniker in self.moniker_path.iter() {
            display.push('/');
            display.push_str(moniker.as_str());
        }
        display
    }

    /// Returns the child node corresponding to `moniker`, or an error if `moniker` does not
    /// match any child of this `ComponentNode`.
    pub fn get_child_node(
        self: &Arc<Self>,
        moniker: &PartialMoniker,
    ) -> Result<Arc<ComponentNode>, ComponentTreeError> {
        match self.children.borrow().get(moniker) {
            Some(child_node) => Ok(Arc::clone(child_node)),
            None => Err(ComponentTreeError::ChildNodeNotFound(
                moniker.to_string(),
                self.short_display(),
            )),
        }
    }

    pub fn get_children(self: &Arc<Self>) -> Vec<Arc<ComponentNode>> {
        let child_map = self.children.borrow();
        let mut child_nodes = Vec::<Arc<ComponentNode>>::with_capacity(child_map.len());
        for child in child_map.values() {
            child_nodes.push(Arc::clone(child));
        }
        child_nodes
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

impl ComponentTreeBuilder {
    /// Constructs a `ComponentTreeBuilder` from a map of component declarations keyed by
    /// component url.
    pub fn new(decls_by_url: HashMap<String, ComponentDecl>) -> Self {
        ComponentTreeBuilder { decls_by_url }
    }

    fn new_root_node(decl: Box<ComponentDecl>) -> Arc<ComponentNode> {
        Arc::new(ComponentNode {
            decl,
            moniker_path: vec![],
            parent: Weak::new(),
            children: RefCell::new(HashMap::new()),
        })
    }

    fn new_child_node(
        decl: Box<ComponentDecl>,
        moniker: &PartialMoniker,
        parent: &Arc<ComponentNode>,
    ) -> Arc<ComponentNode> {
        let mut moniker_path = parent.moniker_path.clone();
        moniker_path.push(moniker.clone());

        Arc::new(ComponentNode {
            decl,
            moniker_path,
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
                self.add_descendants(&root_node)?;
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
                    let moniker = PartialMoniker::new(child.name.clone(), None);
                    let child_node =
                        ComponentTreeBuilder::new_child_node(Box::new(child_decl), &moniker, node);
                    self.add_descendants(&child_node)?;
                    node.children.borrow_mut().insert(moniker, child_node);
                }
                None => {
                    return Err(ComponentTreeError::ComponentDeclNotFound(child.url.to_string()))
                }
            };
        }
        Ok(())
    }
}

impl BreadthFirstWalker {
    pub fn new(tree: &ComponentTree) -> Self {
        let mut walker = BreadthFirstWalker { discovered: VecDeque::new() };
        walker.discover_child_nodes(&tree.root);
        walker
    }

    fn discover_child_nodes(&mut self, node: &Arc<ComponentNode>) {
        let children = node.get_children();
        self.discovered.reserve(children.len());
        for child in children {
            self.discovered.push_back(Arc::clone(&child));
        }
    }
}

impl ComponentTreeWalker for BreadthFirstWalker {
    fn get_next_node(&mut self) -> Result<Option<Arc<ComponentNode>>, anyhow::Error> {
        match self.discovered.pop_front() {
            Some(next_node) => {
                self.discover_child_nodes(&next_node);
                Ok(Some(next_node))
            }
            None => Ok(None),
        }
    }
}

#[cfg(test)]
mod tests {
    use {super::*, std::collections::HashSet};

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
            children,
            collections: vec![],
            facets: None,
            environments: vec![],
        }
    }

    fn new_root_node(decl: ComponentDecl) -> ComponentNode {
        ComponentNode {
            decl: Box::new(decl),
            moniker_path: vec![],
            parent: Weak::new(),
            children: RefCell::new(HashMap::new()),
        }
    }

    fn new_child_node(
        decl: ComponentDecl,
        moniker: &PartialMoniker,
        parent: &Arc<ComponentNode>,
    ) -> ComponentNode {
        let mut moniker_path = parent.moniker_path.clone();
        moniker_path.push(moniker.clone());
        ComponentNode {
            decl: Box::new(decl),
            moniker_path,
            parent: Arc::downgrade(parent),
            children: RefCell::new(HashMap::new()),
        }
    }

    // Builds a `ComponentTree` with a single node.
    fn build_single_node_tree() -> Result<ComponentTree, ComponentTreeError> {
        let root_url = "root_url".to_string();
        let root_decl = new_component_decl(vec![]);
        let mut decls = HashMap::new();
        decls.insert(root_url.clone(), root_decl.clone());
        ComponentTreeBuilder::new(decls).build(root_url)
    }

    // Builds a `ComponentTree` with 4 nodes and the following structure:
    //
    //          root
    //         /    \
    //       foo    bar
    //       /
    //     baz
    //
    fn build_multi_node_tree() -> Result<ComponentTree, ComponentTreeError> {
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

        ComponentTreeBuilder::new(decls).build(root_url)
    }

    // A test ComponentNodeVisitor which just records the short display string of each
    // ComponentNode visited.
    #[derive(Default)]
    struct RouteMappingVisitor {
        pub route: Vec<String>,
    }

    impl ComponentNodeVisitor for RouteMappingVisitor {
        fn visit_node(&mut self, node: &Arc<ComponentNode>) -> Result<(), anyhow::Error> {
            self.route.push(node.short_display());
            Ok(())
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
        let foo_moniker = PartialMoniker::new("foo".to_string(), None);
        let foo_node = Arc::new(new_child_node(foo_decl.clone(), &foo_moniker, &root_node));

        root_node.children.borrow_mut().insert(foo_moniker.clone(), foo_node);

        let valid_child = root_node.get_child_node(&foo_moniker);
        assert!(valid_child.is_ok());
        assert_eq!(valid_child.unwrap().short_display(), "/foo");

        let other_moniker = PartialMoniker::new("other".to_string(), None);
        let invalid_child = root_node.get_child_node(&other_moniker);
        assert!(
            invalid_child.is_err(),
            ComponentTreeError::ChildNodeNotFound(
                other_moniker.to_string(),
                root_node.short_display()
            )
        );
    }

    #[test]
    // Checks that `get_children` returns all children of a ComponentNode, or an
    // empty vector if the node has no children.
    fn get_children() {
        let root_decl = new_component_decl(vec![]);
        let root_node = Arc::new(new_root_node(root_decl.clone()));

        let foo_decl = new_component_decl(vec![]);
        let foo_moniker = PartialMoniker::new("foo".to_string(), None);
        let foo_node = Arc::new(new_child_node(foo_decl.clone(), &foo_moniker, &root_node));

        let bar_decl = new_component_decl(vec![]);
        let bar_moniker = PartialMoniker::new("bar".to_string(), None);
        let bar_node = Arc::new(new_child_node(bar_decl.clone(), &bar_moniker, &root_node));

        root_node.children.borrow_mut().insert(foo_moniker.clone(), foo_node.clone());
        root_node.children.borrow_mut().insert(bar_moniker.clone(), bar_node.clone());

        let mut root_children = HashSet::new();
        for child in root_node.get_children() {
            root_children.insert(child.short_display());
        }
        let expected: HashSet<String> =
            vec![foo_node.short_display(), bar_node.short_display()].into_iter().collect();
        assert_eq!(root_children, expected);
        assert!(foo_node.get_children().is_empty());
    }

    #[test]
    // Checks that the `try_get_parent` method successfully returns the parent
    // of a `ComponentNode`, and returns `None` when called on a `ComponentNode`
    // without a parent.
    fn try_get_parent_node() {
        let root_decl = new_component_decl(vec![]);
        let root_node = Arc::new(new_root_node(root_decl.clone()));

        let foo_decl = new_component_decl(vec![]);
        let foo_moniker = PartialMoniker::new("foo".to_string(), None);
        let foo_node = Arc::new(new_child_node(foo_decl, &foo_moniker, &root_node));

        let has_parent = foo_node.try_get_parent();
        assert!(has_parent.is_some());
        assert_eq!(has_parent.unwrap().short_display(), "/");

        let no_parent = root_node.try_get_parent();
        assert!(no_parent.is_none());
    }

    #[test]
    // Builds a tree with a single node and checks that the node can be looked up at an
    // empty sequence of PartialMonikers.
    fn build_tree_and_look_up_single_node() {
        let build_tree_result = build_single_node_tree();
        assert!(build_tree_result.is_ok());

        let get_root_result = build_tree_result.unwrap().get_node(&vec![]);
        assert!(get_root_result.is_ok());

        let root_node = get_root_result.unwrap();
        assert_eq!(root_node.short_display(), "/");
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
    // Builds a tree with 4 nodes using `build_multi_node_tree()`. Checks that each node is
    // populated correctly and can be looked up by a sequence of PartialMonikers. Also checks
    // that lookup fails with an invalid sequence of PartialMonikers.
    fn build_tree_and_look_up_multi_node() {
        let build_tree_result = build_multi_node_tree();
        assert!(build_tree_result.is_ok());
        let tree = build_tree_result.unwrap();

        let foo_moniker = PartialMoniker::new("foo".to_string(), None);
        let bar_moniker = PartialMoniker::new("bar".to_string(), None);
        let baz_moniker = PartialMoniker::new("baz".to_string(), None);

        let get_root_result = tree.get_node(&vec![]);
        assert!(get_root_result.is_ok());
        let root_node = get_root_result.unwrap();
        assert_eq!(root_node.short_display(), "/");
        assert_eq!(root_node.children.borrow().len(), 2);
        assert!(root_node.try_get_parent().is_none());

        let get_foo_result = tree.get_node(&vec![&foo_moniker]);
        assert!(get_foo_result.is_ok());
        let foo_node = get_foo_result.unwrap();
        assert_eq!(foo_node.short_display(), "/foo");
        assert_eq!(foo_node.children.borrow().len(), 1);
        let foo_parent = foo_node.try_get_parent();
        assert!(foo_parent.is_some());
        assert_eq!(foo_parent.unwrap().short_display(), "/");

        let get_bar_result = tree.get_node(&vec![&bar_moniker]);
        assert!(get_bar_result.is_ok());
        let bar_node = get_bar_result.unwrap();
        assert_eq!(bar_node.short_display(), "/bar");
        assert!(bar_node.children.borrow().is_empty());
        let bar_parent = bar_node.try_get_parent();
        assert!(bar_parent.is_some());
        assert_eq!(bar_parent.unwrap().short_display(), "/");

        let get_baz_result = tree.get_node(&vec![&foo_moniker, &baz_moniker]);
        assert!(get_baz_result.is_ok());
        let baz_node = get_baz_result.unwrap();
        assert_eq!(baz_node.short_display(), "/foo/baz");
        assert!(baz_node.children.borrow().is_empty());
        let baz_parent = baz_node.try_get_parent();
        assert!(baz_parent.is_some());
        assert_eq!(baz_parent.unwrap().short_display(), "/foo");

        let other_moniker = PartialMoniker::new("other".to_string(), None);
        assert!(
            tree.get_node(&vec![&other_moniker]).is_err(),
            ComponentTreeError::ChildNodeNotFound(
                other_moniker.to_string(),
                root_node.short_display()
            )
        );
        assert!(
            tree.get_node(&vec![&foo_moniker, &other_moniker]).is_err(),
            ComponentTreeError::ChildNodeNotFound(
                other_moniker.to_string(),
                foo_node.short_display()
            )
        );
    }

    #[test]
    // Checks that the BreadthFirstWalker visits each node once when the
    // ComponentTree has a single node.
    fn breadth_first_walker_single_node() {
        let build_tree_result = build_single_node_tree();
        assert!(build_tree_result.is_ok());
        let tree = build_tree_result.unwrap();

        let mut walker = BreadthFirstWalker::new(&tree);
        let mut visitor = RouteMappingVisitor::default();
        assert!(walker.walk(&tree, &mut visitor).is_ok());
        assert_eq!(visitor.route, vec!["/"]);
    }

    #[test]
    // Checks that the BreadthFirstWalker visits each node once in breadth-first
    // order when the ComponentTree has multiple nodes.
    fn breadth_first_walker_multi_node() {
        let build_tree_result = build_multi_node_tree();
        assert!(build_tree_result.is_ok());
        let tree = build_tree_result.unwrap();

        let mut walker = BreadthFirstWalker::new(&tree);
        let mut visitor = RouteMappingVisitor::default();
        assert!(walker.walk(&tree, &mut visitor).is_ok());
        // Sibling nodes "/foo" and "/bar" may be visited in either order.
        assert!(
            (visitor.route == vec!["/", "/foo", "/bar", "/foo/baz"])
                | (visitor.route == vec!["/", "/bar", "/foo", "/foo/baz"])
        );
    }
}

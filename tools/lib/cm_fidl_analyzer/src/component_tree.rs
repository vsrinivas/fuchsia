// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    cm_rust::{ChildDecl, ComponentDecl, EnvironmentDecl},
    moniker::{AbsoluteMoniker, AbsoluteMonikerBase, ChildMonikerBase, PartialChildMoniker},
    routing::environment::{DebugRegistry, EnvironmentExtends, RunnerRegistry},
    serde::{Deserialize, Serialize},
    std::{
        collections::{HashMap, VecDeque},
        convert::Into,
        fmt,
        fmt::Display,
    },
    thiserror::Error,
};

/// Errors that may occur while building or operating on a `ComponentTree`.
#[derive(Clone, Debug, Deserialize, Error, PartialEq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum ComponentTreeError {
    #[error("no component declaration found for url `{0}` requested by node `{1}`")]
    ComponentDeclNotFound(String, String),
    #[error("invalid child declaration containing url `{0}` at node `{1}`")]
    InvalidChildDecl(String, String),
    #[error("no node found with path `{0}`")]
    ComponentNodeNotFound(String),
    #[error("environment `{0}` requested by child `{1}` not found at node `{2}`")]
    EnvironmentNotFound(String, String, String),
}

/// A representation of a component's position in the component topology. The last segment of
/// a component's `NodePath` is its `PartialChildMoniker` as designated by its parent component, and the
/// prefix is the parent component's `NodePath`.
#[derive(Clone, Debug, Default, Eq, Hash, PartialEq)]
pub struct NodePath(Vec<PartialChildMoniker>);

/// A representation of a v2 component containing a `ComponentDecl` as well as the `NodePath`s of
/// the component itself and of its parent and children (if any).
#[derive(Clone)]
pub struct ComponentNode {
    pub decl: ComponentDecl,
    node_path: NodePath,
    url: String,
    parent: Option<NodePath>,
    children: Vec<NodePath>,
    environment: NodeEnvironment,
}

/// The environment of a v2 component.
#[derive(Clone, Debug)]
pub struct NodeEnvironment {
    /// The name of this environment as defined by its creator. Should be `None` for the root
    /// environment.
    name: Option<String>,
    /// The relationship of this environment to that of the component instance's parent.
    extends: EnvironmentExtends,
    /// The runners available in this environment.
    runner_registry: RunnerRegistry,
    /// Protocols available in this environment as debug capabilities.
    debug_registry: DebugRegistry,
}

/// A representation of the set of all v2 components, together with their parent/child relationships.
#[derive(Default)]
pub struct ComponentTree {
    nodes: HashMap<NodePath, ComponentNode>,
}

#[derive(Default)]
pub struct BuildTreeResult {
    pub tree: Option<ComponentTree>,
    pub errors: Vec<ComponentTreeError>,
}

/// A builder which constructs a ComponentTree from a collection of component declarations indexed
/// by component URL.
pub struct ComponentTreeBuilder {
    decls_by_url: HashMap<String, ComponentDecl>,
    result: BuildTreeResult,
}

/// The `ComponentNodeVisitor` trait defines an interface for operating on a `ComponentNode`.
/// An error should be returned if the operation fails.
pub trait ComponentNodeVisitor {
    fn visit_node(&mut self, node: &ComponentNode) -> Result<(), anyhow::Error>;
}

/// The `ComponentTreeWalker` trait defines an interface for iteratively operating on nodes
/// of a `ComponentTree`, given a type implementing a per-node operation via the
/// `ComponentNodeVisitor` trait.
pub trait ComponentTreeWalker<'a> {
    /// Walks a `ComponentTree`, doing the operation implemented by `visitor` at each node.
    /// If the operation fails at a node, terminates the walk and propagates the error.
    fn walk<T: ComponentNodeVisitor>(
        &mut self,
        tree: &'a ComponentTree,
        visitor: &mut T,
    ) -> Result<(), anyhow::Error> {
        let mut node = tree.get_root_node()?;
        loop {
            visitor.visit_node(node)?;
            match self.get_next_node(tree)? {
                Some(ref next_node) => {
                    node = next_node;
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
    fn get_next_node(
        &mut self,
        tree: &'a ComponentTree,
    ) -> Result<Option<&ComponentNode>, anyhow::Error>;
}

/// A walker implementing breadth-first traversal of a full `ComponentTree`, starting at the root
/// node.
pub struct BreadthFirstWalker<'a> {
    discovered: VecDeque<&'a ComponentNode>,
}

impl NodePath {
    pub fn new(monikers: Vec<PartialChildMoniker>) -> Self {
        let mut node_path = NodePath::default();
        node_path.0 = monikers;
        node_path
    }

    /// Construct NodePath from string references that correspond to parsable
    /// `ChildMoniker` instances.
    pub fn absolute_from_vec(vec: Vec<&str>) -> Self {
        let abs_moniker: AbsoluteMoniker = vec.into();
        Self::new(
            abs_moniker
                .path()
                .into_iter()
                .map(|child_moniker| child_moniker.to_partial())
                .collect(),
        )
    }

    /// Returns a new `NodePath` which extends `self` by appending `moniker` at the end of the path.
    pub fn extended(&self, moniker: PartialChildMoniker) -> Self {
        let mut node_path = NodePath::new(self.0.clone());
        node_path.0.push(moniker);
        node_path
    }

    /// Construct string references that correspond to underlying
    /// `ChildMoniker` instances.
    pub fn as_vec(&self) -> Vec<&str> {
        self.0.iter().map(|moniker| moniker.as_str()).collect()
    }
}

impl Display for NodePath {
    // Displays a `NodePath` as a slash-separated path.
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        if self.0.is_empty() {
            return write!(f, "/");
        }
        let mut path_string = "".to_owned();
        for moniker in self.0.iter() {
            path_string.push('/');
            path_string.push_str(moniker.as_str());
        }
        write!(f, "{}", path_string)
    }
}

impl From<AbsoluteMoniker> for NodePath {
    fn from(moniker: AbsoluteMoniker) -> Self {
        Self::new(
            moniker.path().into_iter().map(|child_moniker| child_moniker.to_partial()).collect(),
        )
    }
}

impl NodeEnvironment {
    pub fn name(&self) -> Option<&str> {
        self.name.as_deref()
    }

    pub fn extends(&self) -> &EnvironmentExtends {
        &self.extends
    }

    pub fn runner_registry(&self) -> &RunnerRegistry {
        &self.runner_registry
    }

    pub fn debug_registry(&self) -> &DebugRegistry {
        &self.debug_registry
    }

    // Defines an environment for the root node with the given `runner_registry` and `debug_registry`.
    fn new_root(runner_registry: RunnerRegistry, debug_registry: DebugRegistry) -> Self {
        Self { name: None, extends: EnvironmentExtends::None, runner_registry, debug_registry }
    }

    // Defines an environment inheriting the parent realm's environment without modification.
    fn new_inheriting() -> Self {
        Self {
            name: None,
            extends: EnvironmentExtends::Realm,
            runner_registry: RunnerRegistry::default(),
            debug_registry: DebugRegistry::default(),
        }
    }

    // Defines a named environment from a parent instance's `EnvironmentDecl`.
    fn from_decl(env_decl: &EnvironmentDecl) -> Self {
        Self {
            name: Some(env_decl.name.clone()),
            extends: env_decl.extends.into(),
            runner_registry: RunnerRegistry::from_decl(&env_decl.runners),
            debug_registry: env_decl.debug_capabilities.clone().into(),
        }
    }
}

impl ComponentNode {
    /// Returns a string representing the path to this `ComponentNode` from the root node.
    /// Each path segment is the `PartialChildMoniker` of a component relative to its parent.
    /// The root node is represented as "/".
    pub fn short_display(&self) -> String {
        self.node_path.to_string()
    }

    pub fn url(&self) -> String {
        self.url.clone()
    }

    pub fn moniker(&self) -> Option<&PartialChildMoniker> {
        self.node_path.0.last()
    }

    pub fn node_path(&self) -> NodePath {
        self.node_path.clone()
    }

    pub fn parent(&self) -> Option<&NodePath> {
        self.parent.as_ref()
    }

    pub fn children(&self) -> &Vec<NodePath> {
        &self.children
    }

    pub fn environment(&self) -> &NodeEnvironment {
        &self.environment
    }

    // Creates a new `ComponentNode` with the specified declaration, url, parent, moniker, and
    // environment. Does not populate the node's `children` field.
    fn new(
        decl: ComponentDecl,
        url: String,
        parent: Option<NodePath>,
        moniker: Option<PartialChildMoniker>,
        environment: NodeEnvironment,
    ) -> Self {
        ComponentNode {
            decl,
            node_path: ComponentNode::get_node_path(parent.clone(), moniker),
            url,
            parent,
            children: vec![],
            environment,
        }
    }

    fn get_node_path(parent: Option<NodePath>, moniker: Option<PartialChildMoniker>) -> NodePath {
        if let Some(postfix) = moniker {
            if let Some(prefix) = parent {
                return prefix.extended(postfix);
            } else {
                return NodePath::new(vec![postfix]);
            }
        }
        NodePath::default()
    }

    fn environment_for_child(
        &self,
        child_decl: &ChildDecl,
    ) -> Result<NodeEnvironment, ComponentTreeError> {
        match child_decl.environment.as_ref() {
            Some(child_env_name) => {
                let env_decl =
                    self.decl.environments.iter().find(|&env| &env.name == child_env_name).ok_or(
                        ComponentTreeError::EnvironmentNotFound(
                            child_env_name.clone(),
                            child_decl.name.clone(),
                            self.node_path().to_string(),
                        ),
                    )?;
                Ok(NodeEnvironment::from_decl(env_decl))
            }
            None => Ok(NodeEnvironment::new_inheriting()),
        }
    }
}

impl ComponentTree {
    pub fn len(&self) -> usize {
        self.nodes.len()
    }

    pub fn is_empty(&self) -> bool {
        self.nodes.is_empty()
    }

    /// Returns the node at `node_path`, or an error if there is no node at `node_path`.
    pub fn get_node(&self, node_path: &NodePath) -> Result<&ComponentNode, ComponentTreeError> {
        match self.nodes.get(node_path) {
            Some(ref node) => Ok(node),
            None => Err(ComponentTreeError::ComponentNodeNotFound(node_path.to_string())),
        }
    }

    /// Returns the root node, or an error if the root node is not found.
    pub fn get_root_node(&self) -> Result<&ComponentNode, ComponentTreeError> {
        self.get_node(&NodePath::new(vec![]))
    }

    /// Returns the parent of `node`, or returns `None` if `node` has no parent, or else returns an
    /// error if `node` contains an invalid `NodePath` as its parent identifier.
    pub fn try_get_parent(
        &self,
        node: &ComponentNode,
    ) -> Result<Option<&ComponentNode>, ComponentTreeError> {
        match node.parent {
            Some(ref parent) => Ok(Some(self.get_node(parent)?)),
            None => Ok(None),
        }
    }

    pub fn get_child_node(
        &self,
        node: &ComponentNode,
        moniker: PartialChildMoniker,
    ) -> Result<&ComponentNode, ComponentTreeError> {
        Ok(self.get_node(&node.node_path.extended(moniker))?)
    }

    /// Returns the children of `node`, or returns an error if `node` contains an invalid `NodePath` as
    /// one of its child identifier.
    pub fn get_children(
        &self,
        node: &ComponentNode,
    ) -> Result<Vec<&ComponentNode>, ComponentTreeError> {
        let mut children = Vec::new();
        for child in node.children.iter() {
            children.push(self.get_node(child)?);
        }
        Ok(children)
    }
}

impl ComponentTreeBuilder {
    /// Constructs a `ComponentTreeBuilder` from a map of component declarations keyed by
    /// component url.
    pub fn new(decls_by_url: HashMap<String, ComponentDecl>) -> Self {
        ComponentTreeBuilder { decls_by_url, result: BuildTreeResult::default() }
    }

    /// Constructs and returns a `ComponentTree` based at the component with url `root_url`,
    /// consuming the builder. Returns an error if `root_url` or the url of any subsequent
    /// child is not present in the builder's `decls_by_url` map, or if any `ComponentDecl`
    /// in `decls_by_url` contains an invalid `ChildDecl`.
    pub fn build<T: Into<String>>(mut self, root_url: T) -> BuildTreeResult {
        let mut tree = ComponentTree { nodes: HashMap::new() };

        let root_url = root_url.into();
        match self.decls_by_url.get(&root_url) {
            Some(root_decl) => {
                let mut root_node = ComponentNode::new(
                    root_decl.clone(),
                    root_url,
                    None,
                    None,
                    // TODO(pesk): probably runner_registry and debug_registry should be args
                    // to this method. What are they derived from?
                    NodeEnvironment::new_root(RunnerRegistry::default(), DebugRegistry::default()),
                );
                self.add_descendants(&mut tree, &mut root_node);
                tree.nodes.insert(root_node.node_path.clone(), root_node);
                self.result.tree = Some(tree)
            }
            None => self
                .result
                .errors
                .push(ComponentTreeError::ComponentDeclNotFound(root_url, "".to_string())),
        }
        self.result
    }

    fn add_descendants(&mut self, tree: &mut ComponentTree, node: &mut ComponentNode) {
        for child in node.decl.children.iter() {
            if child.name.is_empty() {
                self.result.errors.push(ComponentTreeError::InvalidChildDecl(
                    child.url.to_string(),
                    node.short_display(),
                ));
                continue;
            }
            match self.decls_by_url.get(&child.url) {
                Some(child_decl) => match node.environment_for_child(child) {
                    Ok(environment) => {
                        let mut child_node = ComponentNode::new(
                            child_decl.clone(),
                            child.url.clone(),
                            Some(node.node_path.clone()),
                            Some(PartialChildMoniker::new(child.name.clone(), None)),
                            environment,
                        );
                        self.add_descendants(tree, &mut child_node);
                        node.children.push(child_node.node_path.clone());
                        tree.nodes.insert(child_node.node_path.clone(), child_node);
                    }
                    Err(err) => self.result.errors.push(err),
                },
                None => self.result.errors.push(ComponentTreeError::ComponentDeclNotFound(
                    child.url.to_string(),
                    node.short_display(),
                )),
            }
        }
    }
}

impl<'a> BreadthFirstWalker<'a> {
    pub fn new(tree: &'a ComponentTree) -> Result<Self, anyhow::Error> {
        let mut walker = BreadthFirstWalker { discovered: VecDeque::new() };
        walker.discover_child_nodes(&tree, tree.get_root_node()?)?;
        Ok(walker)
    }

    fn discover_child_nodes(
        &mut self,
        tree: &'a ComponentTree,
        node: &'a ComponentNode,
    ) -> Result<(), anyhow::Error> {
        let children = tree.get_children(node)?;
        self.discovered.reserve(children.len());
        for child in children {
            self.discovered.push_back(child);
        }
        Ok(())
    }
}

impl<'a> ComponentTreeWalker<'a> for BreadthFirstWalker<'a> {
    fn get_next_node(
        &mut self,
        tree: &'a ComponentTree,
    ) -> Result<Option<&'a ComponentNode>, anyhow::Error> {
        match self.discovered.pop_front() {
            Some(next_node) => {
                self.discover_child_nodes(tree, &next_node)?;
                Ok(Some(next_node))
            }
            None => Ok(None),
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        cm_rust::{
            CapabilityName, ChildDecl, DebugProtocolRegistration, DebugRegistration,
            RegistrationSource, RunnerRegistration,
        },
        fidl_fuchsia_sys2::StartupMode,
    };

    fn display_nodes(nodes: Vec<&ComponentNode>) -> Vec<String> {
        nodes.iter().map(|x| x.short_display()).collect()
    }

    fn new_child_decl(name: String, url: String, environment: Option<String>) -> ChildDecl {
        ChildDecl { name, url, startup: StartupMode::Lazy, environment, on_terminate: None }
    }

    fn new_environment_decl(
        name: String,
        extends: fidl_fuchsia_sys2::EnvironmentExtends,
        runners: Vec<RunnerRegistration>,
        debug_capabilities: Vec<DebugRegistration>,
    ) -> EnvironmentDecl {
        EnvironmentDecl {
            name,
            extends,
            runners,
            resolvers: vec![],
            debug_capabilities,
            stop_timeout_ms: None,
        }
    }

    fn new_component_decl(
        children: Vec<ChildDecl>,
        environments: Vec<EnvironmentDecl>,
    ) -> ComponentDecl {
        ComponentDecl {
            program: None,
            uses: vec![],
            exposes: vec![],
            offers: vec![],
            capabilities: vec![],
            children,
            collections: vec![],
            facets: None,
            environments,
        }
    }

    // Builds a `ComponentTree` with a single node.
    fn build_single_node_tree() -> BuildTreeResult {
        let root_url = "root_url".to_string();
        let root_decl = new_component_decl(vec![], vec![]);
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
    fn build_multi_node_tree() -> BuildTreeResult {
        let root_url = "root_url".to_string();
        let foo_url = "foo_url".to_string();
        let bar_url = "bar_url".to_string();
        let baz_url = "baz_url".to_string();

        let foo_name = "foo".to_string();
        let bar_name = "bar".to_string();
        let baz_name = "baz".to_string();

        let root_decl = new_component_decl(
            vec![
                new_child_decl(foo_name, foo_url.clone(), None),
                new_child_decl(bar_name, bar_url.clone(), None),
            ],
            vec![],
        );
        let foo_decl =
            new_component_decl(vec![new_child_decl(baz_name, baz_url.clone(), None)], vec![]);
        let bar_decl = new_component_decl(vec![], vec![]);
        let baz_decl = new_component_decl(vec![], vec![]);

        let mut decls = HashMap::new();
        decls.insert(root_url.to_string(), root_decl.clone());
        decls.insert(foo_url.to_string(), foo_decl.clone());
        decls.insert(bar_url.to_string(), bar_decl.clone());
        decls.insert(baz_url.to_string(), baz_decl.clone());

        ComponentTreeBuilder::new(decls).build(root_url)
    }

    // A test ComponentNodeVisitor which just records the path string of each ComponentNode visited.
    #[derive(Default)]
    struct RouteMappingVisitor {
        pub route: Vec<String>,
    }

    impl ComponentNodeVisitor for RouteMappingVisitor {
        fn visit_node(&mut self, node: &ComponentNode) -> Result<(), anyhow::Error> {
            self.route.push(node.short_display());
            Ok(())
        }
    }

    // Tests the `extended()` and `to_string()` methods of `NodePath`.
    #[test]
    fn node_path_operations() {
        let empty_node_path = NodePath::default();
        assert_eq!(empty_node_path.to_string(), "/");

        let foo_moniker = PartialChildMoniker::new("foo".to_string(), None);
        let foo_node_path = empty_node_path.extended(foo_moniker);
        assert_eq!(foo_node_path.to_string(), "/foo");

        let bar_moniker = PartialChildMoniker::new("bar".to_string(), None);
        let bar_node_path = foo_node_path.extended(bar_moniker);
        assert_eq!(bar_node_path.to_string(), "/foo/bar");
    }

    // Builds `ComponentNode`s with and without parents and children and tests the
    // `short_display()` and `url()` methods.
    #[test]
    fn build_node() {
        let foo_moniker = PartialChildMoniker::new("foo".to_string(), None);
        let bar_moniker = PartialChildMoniker::new("bar".to_string(), None);

        let root_url = "root_url".to_string();
        let leaf_url = "leaf_url".to_string();

        let root_node = ComponentNode::new(
            new_component_decl(vec![], vec![]),
            root_url.clone(),
            None,
            None,
            NodeEnvironment::new_root(RunnerRegistry::default(), DebugRegistry::default()),
        );
        assert_eq!(root_node.short_display(), "/");
        assert_eq!(root_node.url(), root_url);

        let leaf_node = ComponentNode::new(
            new_component_decl(vec![], vec![]),
            leaf_url.clone(),
            Some(NodePath::new(vec![foo_moniker])),
            Some(bar_moniker),
            NodeEnvironment::new_inheriting(),
        );
        assert_eq!(leaf_node.short_display(), "/foo/bar");
        assert_eq!(leaf_node.url(), leaf_url);
    }

    // Builds a tree with a single node, retrieves the root node, and checks that `try_get_parent`
    // and `get_children` return OK but trivial results.
    #[test]
    fn single_node_tree() -> Result<(), ComponentTreeError> {
        let tree_result = build_single_node_tree();
        assert!(tree_result.errors.is_empty());
        let tree = tree_result.tree.unwrap();

        let root_node = tree.get_root_node()?;
        assert!(root_node.parent.is_none());
        assert!(root_node.children.is_empty());
        assert_eq!(root_node.short_display(), "/");

        let parent = tree.try_get_parent(&root_node)?;
        assert!(parent.is_none());

        let children = tree.get_children(&root_node)?;
        assert!(children.is_empty());

        Ok(())
    }

    // Checks that the expected error is returned when the ComponentTreeBuilder's
    // `build` method is called with an unrecognized component url.
    #[test]
    fn build_tree_root_url_not_found() {
        let root_url = "root_url".to_string();
        let other_url = "other_url".to_string();
        let mut decls = HashMap::new();
        decls.insert(root_url.clone(), new_component_decl(vec![], vec![]));
        let build_result = ComponentTreeBuilder::new(decls).build(other_url.clone());
        assert!(build_result.tree.is_none());
        assert_eq!(build_result.errors.len(), 1);
        assert_eq!(
            build_result.errors[0],
            ComponentTreeError::ComponentDeclNotFound(other_url, "".to_string())
        );
    }

    // Builds a tree with 4 nodes using `build_multi_node_tree()`. Checks that the `node_path`,
    // `parent`, and `children` fields of each node are populated correctly and can be looked up
    // by a sequence of PartialChildMonikers. Also checks that lookup fails with an invalid sequence
    // of PartialChildMonikers.
    #[test]
    fn build_tree_and_look_up_multi_node() -> Result<(), ComponentTreeError> {
        let tree_result = build_multi_node_tree();
        assert!(tree_result.errors.is_empty());
        let tree = tree_result.tree.unwrap();

        let foo_path = NodePath::new(vec![PartialChildMoniker::new("foo".to_string(), None)]);
        let bar_path = NodePath::new(vec![PartialChildMoniker::new("bar".to_string(), None)]);
        let baz_path = foo_path.extended(PartialChildMoniker::new("baz".to_string(), None));
        let other_path = NodePath::new(vec![PartialChildMoniker::new("other".to_string(), None)]);

        let root_node = tree.get_root_node()?;
        assert_eq!(root_node.node_path.to_string(), "/");
        assert!(root_node.parent.is_none());
        assert_eq!(root_node.children, vec![foo_path.clone(), bar_path.clone()]);

        let foo_node = tree.get_node(&foo_path)?;
        assert_eq!(foo_node.node_path.to_string(), "/foo");
        assert!(foo_node.parent.is_some());
        assert_eq!(foo_node.parent.as_ref().unwrap().to_string(), "/");
        assert_eq!(foo_node.children, vec![baz_path.clone()]);

        let bar_node = tree.get_node(&bar_path)?;
        assert_eq!(bar_node.node_path.to_string(), "/bar");
        assert!(bar_node.parent.is_some());
        assert_eq!(bar_node.parent.as_ref().unwrap().to_string(), "/");
        assert!(bar_node.children.is_empty());

        let baz_node = tree.get_node(&baz_path)?;
        assert_eq!(baz_node.node_path.to_string(), "/foo/baz");
        assert!(baz_node.parent.is_some());
        assert_eq!(baz_node.parent.as_ref().unwrap().to_string(), "/foo");
        assert!(baz_node.children.is_empty());

        let get_other_node_result = tree.get_node(&other_path);
        assert!(get_other_node_result.is_err());
        assert_eq!(
            get_other_node_result.err().unwrap(),
            ComponentTreeError::ComponentNodeNotFound(other_path.to_string())
        );

        Ok(())
    }

    // Builds a tree with a child node that inherits a named environment from
    // its parent. Checks that the child node's environment is correctly populated.
    #[test]
    fn build_tree_with_environment() -> Result<(), ComponentTreeError> {
        let root_url = "root_url".to_string();
        let foo_url = "foo_url".to_string();
        let foo_name = "foo".to_string();
        let foo_env_name = "foo_env".to_string();
        let foo_extends = fidl_fuchsia_sys2::EnvironmentExtends::Realm;
        let foo_runner_name = CapabilityName("foo_runner".to_string());
        let foo_debug_name = CapabilityName("foo_debug".to_string());
        let foo_runner = RunnerRegistration {
            source_name: foo_runner_name.clone(),
            target_name: foo_runner_name.clone(),
            source: RegistrationSource::Parent,
        };
        let foo_debug = DebugRegistration::Protocol(DebugProtocolRegistration {
            source_name: foo_debug_name.clone(),
            source: RegistrationSource::Parent,
            target_name: foo_debug_name.clone(),
        });

        let root_decl = new_component_decl(
            vec![new_child_decl(foo_name.clone(), foo_url.clone(), Some(foo_env_name.clone()))],
            vec![new_environment_decl(
                foo_env_name.clone(),
                foo_extends.clone(),
                vec![foo_runner],
                vec![foo_debug],
            )],
        );
        let foo_decl = new_component_decl(vec![], vec![]);

        let mut decls = HashMap::new();
        decls.insert(root_url.to_string(), root_decl);
        decls.insert(foo_url.to_string(), foo_decl);

        let build_result = ComponentTreeBuilder::new(decls).build(root_url);
        assert!(build_result.errors.is_empty());
        let tree = build_result.tree.unwrap();

        let foo_node =
            tree.get_node(&NodePath::new(vec![PartialChildMoniker::new(foo_name, None)]))?;
        assert_eq!(foo_node.environment().name(), Some(foo_env_name).as_deref());
        assert_eq!(foo_node.environment().extends(), &foo_extends.into());
        assert!(foo_node.environment().runner_registry().get_runner(&foo_runner_name).is_some());
        assert!(foo_node.environment().debug_registry().get_capability(&foo_debug_name).is_some());
        Ok(())
    }

    // Builds a tree with a child node that inherits a named environment from its parent,
    // but the parent does not declare that environment. Checks that the tree builder logs
    // an EnvironmentNotFound error describing the problem.
    #[test]
    fn build_tree_missing_environment() {
        let root_url = "root_url".to_string();
        let foo_url = "foo_url".to_string();
        let foo_name = "foo".to_string();
        let foo_env_name = "foo_env".to_string();

        let root_decl = new_component_decl(
            vec![new_child_decl(foo_name.clone(), foo_url.clone(), Some(foo_env_name.clone()))],
            vec![],
        );
        let foo_decl = new_component_decl(vec![], vec![]);

        let mut decls = HashMap::new();
        decls.insert(root_url.to_string(), root_decl);
        decls.insert(foo_url.to_string(), foo_decl);

        let build_result = ComponentTreeBuilder::new(decls).build(root_url);
        assert_eq!(build_result.errors.len(), 1);
        assert_eq!(
            build_result.errors[0],
            ComponentTreeError::EnvironmentNotFound(foo_env_name, foo_name, "/".to_string())
        );
    }

    // Builds a tree with 4 nodes using `build_multi_node_tree()` and checks that `try_get_parent`
    // returns the expected result for each node.
    #[test]
    fn try_get_parent() -> Result<(), ComponentTreeError> {
        let tree_result = build_multi_node_tree();
        assert!(tree_result.errors.is_empty());
        let tree = tree_result.tree.unwrap();

        let foo_path = NodePath::new(vec![PartialChildMoniker::new("foo".to_string(), None)]);
        let bar_path = NodePath::new(vec![PartialChildMoniker::new("bar".to_string(), None)]);
        let baz_path = foo_path.extended(PartialChildMoniker::new("baz".to_string(), None));

        let root_parent = tree.try_get_parent(tree.get_root_node()?)?;
        assert!(root_parent.is_none());

        let foo_parent = tree.try_get_parent(tree.get_node(&foo_path)?)?;
        assert_eq!(foo_parent.unwrap().short_display(), "/");

        let bar_parent = tree.try_get_parent(tree.get_node(&bar_path)?)?;
        assert_eq!(bar_parent.unwrap().short_display(), "/");

        let baz_parent = tree.try_get_parent(tree.get_node(&baz_path)?)?;
        assert_eq!(baz_parent.unwrap().short_display(), "/foo");

        Ok(())
    }

    // Builds a tree with 4 nodes using `build_multi_node_tree()` and checks that `get_children`
    // returns the expected result for each node.
    #[test]
    fn get_children() -> Result<(), ComponentTreeError> {
        let tree_result = build_multi_node_tree();
        assert!(tree_result.errors.is_empty());
        let tree = tree_result.tree.unwrap();

        let foo_path = NodePath::new(vec![PartialChildMoniker::new("foo".to_string(), None)]);
        let bar_path = NodePath::new(vec![PartialChildMoniker::new("bar".to_string(), None)]);
        let baz_path = foo_path.extended(PartialChildMoniker::new("baz".to_string(), None));

        let root_children = tree.get_children(tree.get_root_node()?)?;
        assert_eq!(display_nodes(root_children), vec!["/foo", "/bar"]);

        let foo_children = tree.get_children(tree.get_node(&foo_path)?)?;
        assert_eq!(display_nodes(foo_children), vec!["/foo/baz"]);

        let bar_children = tree.get_children(tree.get_node(&bar_path)?)?;
        assert!(bar_children.is_empty());

        let baz_children = tree.get_children(tree.get_node(&baz_path)?)?;
        assert!(baz_children.is_empty());

        Ok(())
    }

    // Checks that the BreadthFirstWalker visits each node once when the
    // ComponentTree has a single node.
    #[test]
    fn breadth_first_walker_single_node() -> Result<(), anyhow::Error> {
        let tree_result = build_single_node_tree();
        assert!(tree_result.errors.is_empty());
        let tree = tree_result.tree.unwrap();

        let mut walker = BreadthFirstWalker::new(&tree)?;
        let mut visitor = RouteMappingVisitor::default();
        assert!(walker.walk(&tree, &mut visitor).is_ok());
        assert_eq!(visitor.route, vec!["/"]);

        Ok(())
    }

    // Checks that the BreadthFirstWalker visits each node once in breadth-first
    // order when the ComponentTree has multiple nodes.
    #[test]
    fn breadth_first_walker_multi_node() -> Result<(), anyhow::Error> {
        let tree_result = build_multi_node_tree();
        assert!(tree_result.errors.is_empty());
        let tree = tree_result.tree.unwrap();

        let mut walker = BreadthFirstWalker::new(&tree)?;
        let mut visitor = RouteMappingVisitor::default();
        assert!(walker.walk(&tree, &mut visitor).is_ok());
        // Sibling nodes "/foo" and "/bar" may be visited in either order.
        assert!(
            (visitor.route == vec!["/", "/foo", "/bar", "/foo/baz"])
                | (visitor.route == vec!["/", "/bar", "/foo", "/foo/baz"])
        );

        Ok(())
    }
}

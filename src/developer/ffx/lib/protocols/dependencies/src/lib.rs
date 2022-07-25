// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//! This is a library specifically intended for use in macro generation in order
//! to check for runtime dependencies.
//! See //src/developer/ffx/build/templates/protocols_macro.md for more details.

use std::collections::HashMap;

/// Represents a directed graph.
#[derive(Default, Debug)]
pub struct DependencyGraph {
    pub nodes: HashMap<&'static str, Node>,
}

#[derive(Debug, Clone)]
pub enum VerifyError {
    /// When there is a dependency that does not refer to a node in the graph,
    /// this error is returned, along with the [Node] information from which
    /// the error originated (if possible).
    InvalidDependencyError(String),
    /// Identifies the strongly connected component (the graph loop) that
    /// encapsulates the dependency cycle.
    CycleFound(Vec<Node>),
}

impl DependencyGraph {
    /// Returns:
    /// * Err if it finds a cycle, denoting a list from start to end for one
    ///   cycle, OR if there is a dependency that is not to a root node.
    /// * Ok if there are no cycles in the graph and all elements reference
    ///   each other in their dependencies.
    pub fn verify(mut self) -> Result<(), VerifyError> {
        let nodes = self.nodes.values().map(|n| n.protocol).collect::<Vec<_>>();
        for node in nodes.iter() {
            if let Err(e) = self.visit(node) {
                return Err(e);
            }
        }
        Ok(())
    }

    fn visit(&mut self, protocol: &'static str) -> Result<(), VerifyError> {
        let (build_target, deps) = if let Some(node) = self.nodes.get_mut(protocol) {
            if node.actively_visiting {
                // If all nodes are tagged as being actively visited, then this
                // entails the entirety of a strongly connected component, as we have
                // now cycled in on ourselves. Returns the list encompassing this
                // strongly connected component.
                let strong_component = self
                    .nodes
                    .values()
                    .cloned()
                    .filter_map(|n| if n.actively_visiting { Some(n) } else { None })
                    .collect::<Vec<_>>();
                return Err(VerifyError::CycleFound(strong_component));
            }
            // If this node has been totally expanded, return None as no more work
            // is required (no strongly-connected components exist beyond this node.
            if node.visited {
                return Ok(());
            }
            node.actively_visiting = true;
            (node.build_target.clone(), node.dependencies)
        } else {
            // Due to the check in the for-loop below, this code should never actually
            // be triggered, but it is just here as a precaution in case a top-level
            // lookup fails.
            return Err(VerifyError::InvalidDependencyError(format!(
                "'{}' is not a dependency to a protocol.",
                protocol
            )));
        };
        for neighbor in deps.iter() {
            let protocol = if let Some(node) = self.nodes.get(neighbor) {
                node.protocol
            } else {
                return Err(VerifyError::InvalidDependencyError(format!(
                    "Protocol endpoint for '{}'
depends on '{}' which is not a protocol endpoint.
Please amend the ffx_protocol declaration in '{}'.
Protocols are only permitted to declare dependencies on other known protocols.
Verify '{}' is not included in the protocols suite under //src/developer/ffx/daemon/protocols
or remove it from the list of dependencies.",
                    protocol, neighbor, build_target, neighbor
                )));
            };
            match self.visit(protocol) {
                Ok(()) => {}
                e => return e,
            }
        }
        let node = self.nodes.get_mut(protocol).unwrap();
        node.actively_visiting = false;
        node.visited = true;
        Ok(())
    }
}

#[allow(unused)]
/// A node in a directed graph representing a library and its dependencies.
#[derive(Debug, Clone)]
pub struct Node {
    /// This is the FIDL protocol name.
    pub protocol: &'static str,
    /// Represents the BUILD target.
    pub build_target: String,
    /// Adjacency list of protocol library FIDL protocols depended upon.
    pub dependencies: &'static [&'static str],

    /// For graph iteration assistance.
    visited: bool,
    actively_visiting: bool,
}

impl Node {
    pub fn new(
        protocol: &'static str,
        build_target: String,
        dependencies: &'static [&'static str],
    ) -> Self {
        Self { protocol, build_target, dependencies, visited: false, actively_visiting: false }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    impl VerifyError {
        fn unwrap_cycle_err(self) -> Vec<Node> {
            if let VerifyError::CycleFound(err) = self {
                err
            } else {
                panic!("expected cycle error, instead found: {:?}", self);
            }
        }
    }

    #[test]
    fn test_non_existent_protocol() {
        let dep =
            Node::new("foo.bar.Protocol", "//this/target".to_owned(), &["non.existent.Protocol"]);
        let mut graph = DependencyGraph::default();
        graph.nodes.insert(dep.protocol, dep);
        let res = graph.verify();
        assert!(res.is_err());
    }

    #[test]
    fn test_self_cycle() {
        let dep = Node::new("foo.bar.Protocol", "//this/target".to_owned(), &["foo.bar.Protocol"]);
        let mut graph = DependencyGraph::default();
        graph.nodes.insert(dep.protocol, dep);
        let res = graph.verify();
        assert!(res.is_err());
        // Strongly connected component size should be 1.
        let res = res.unwrap_err().unwrap_cycle_err();
        assert_eq!(res.len(), 1);
    }

    #[test]
    fn test_two_nodes_same_dep() {
        let dep = Node::new("foo.bar.Protocol", "//this/target".to_owned(), &["other.Protocol"]);
        let dep2 = Node::new("test.Protocol", "//test/target".to_owned(), &["other.Protocol"]);
        let dep3 = Node::new("other.Protocol", "//other/target".to_owned(), &[]);
        let mut graph = DependencyGraph::default();
        graph.nodes.insert(dep.protocol, dep);
        graph.nodes.insert(dep2.protocol, dep2);
        graph.nodes.insert(dep3.protocol, dep3);
        assert!(graph.verify().is_ok());
    }

    #[test]
    fn test_empty_graph() {
        let graph = DependencyGraph::default();
        assert!(graph.verify().is_ok());
    }

    #[test]
    fn test_single_element_graph_no_deps() {
        let dep = Node::new("foo.bar.Protocol", "//this/target".to_owned(), &[]);
        let mut graph = DependencyGraph::default();
        graph.nodes.insert(dep.protocol, dep);
        assert!(graph.verify().is_ok());
    }

    #[test]
    fn test_multiple_element_cycle() {
        let dep = Node::new(
            "foo.Protocol",
            "//this/target".to_owned(),
            &["other.Protocol", "bar.Protocol"],
        );
        let dep2 =
            Node::new("bar.Protocol", "//bar".to_owned(), &["other.Protocol", "quux.Protocol"]);
        let dep3 =
            Node::new("quux.Protocol", "//quux".to_owned(), &["other.Protocol", "foo.Protocol"]);
        let dep4 = Node::new("other.Protocol", "//other".to_owned(), &[]);
        let mut graph = DependencyGraph::default();
        graph.nodes.insert(dep.protocol, dep);
        graph.nodes.insert(dep2.protocol, dep2);
        graph.nodes.insert(dep3.protocol, dep3);
        graph.nodes.insert(dep4.protocol, dep4);
        let res = graph.verify();
        assert!(res.is_err());
        let err = res.unwrap_err().unwrap_cycle_err();
        assert_eq!(err.len(), 3);
        assert!(err.iter().any(|n| n.protocol == "foo.Protocol"));
        assert!(err.iter().any(|n| n.protocol == "bar.Protocol"));
        assert!(err.iter().any(|n| n.protocol == "quux.Protocol"));
    }
}

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::{HashMap, HashSet};
use std::fmt::Debug;
use std::hash::Hash;
use std::mem;

pub struct DependencyGraph<K: Eq + Hash, V> {
    // Storage for all nodes in the graph.
    nodes: Vec<Node<K, V>>,

    // Maps keys to the corresponding index in |nodes|.
    indices: HashMap<K, usize>,

    // Maps nodes to their children using an index into |nodes|.
    children: HashMap<usize, HashSet<usize>>,

    // Maps nodes to their parents using an index into |nodes|.
    parents: HashMap<usize, HashSet<usize>>,
}

enum Node<K, V> {
    // The root node has no data or key.
    Root,

    // Data nodes have a key and a value.
    Data(K, V),

    // Zombie nodes have been removed from the graph. This is used to avoid index updates to the
    // graph's storage vector when removing a node.
    Zombie,
}

#[derive(Debug, Clone, PartialEq)]
pub enum DependencyError<K: Clone + Debug + PartialEq> {
    MissingDependency(K),
    CircularDependency,
}

impl<K: Eq + Hash + Clone + Debug, V> DependencyGraph<K, V> {
    const ROOT_INDEX: usize = 0;

    /// Constructs a dependency graph and populates it with a root node.
    pub fn new() -> DependencyGraph<K, V> {
        Self {
            nodes: vec![Node::Root],
            indices: HashMap::new(),
            children: HashMap::new(),
            parents: HashMap::new(),
        }
    }

    /// Insert a node into the graph with |key| and attach |data| to the node.
    ///
    /// If the node already exists, it is replaced.
    pub fn insert_node(&mut self, key: K, data: V) {
        let index = self.nodes.len();
        self.nodes.push(Node::Data(key.clone(), data));
        if let Some(previous) = self.indices.insert(key, index) {
            self.nodes[previous] = Node::Zombie;
        }
    }

    /// Inserts an edge from the root of the graph to the node with key |to|.
    ///
    /// If |to| doesn't exist in the graph, return a MissingDependency error.
    pub fn insert_edge_from_root(&mut self, to: &K) -> Result<(), DependencyError<K>> {
        let to_index = self.get_index(to)?;
        self.children.entry(Self::ROOT_INDEX).or_insert(HashSet::new()).insert(to_index);
        self.parents.entry(to_index).or_insert(HashSet::new()).insert(Self::ROOT_INDEX);
        Ok(())
    }

    /// Inserts an edge from the root of the graph from the node with key |from| to the node with
    /// key |to|.
    ///
    /// If either |from| or |to| doesn't exist in the graph, return a MissingDependency error.
    pub fn insert_edge(&mut self, from: &K, to: &K) -> Result<(), DependencyError<K>> {
        let from_index = self.get_index(from)?;
        let to_index = self.get_index(to)?;
        self.children.entry(from_index).or_insert(HashSet::new()).insert(to_index);
        self.parents.entry(to_index).or_insert(HashSet::new()).insert(from_index);
        Ok(())
    }

    /// Resolve the dependency graph, returning the data associated with each node in a path from
    /// the root node to its dependencies. For each node in the path all of its dependencies must
    /// appear after its position in the path. Any node that has no dependency chain from the root
    /// is pruned from the path.
    ///
    /// If no such path is possible return a CircularDependency error.
    pub fn resolve(mut self) -> Result<Vec<V>, DependencyError<K>> {
        // This is a minor variation on Kahn's algorithm. We do not start by finding all nodes with
        // no incoming edges because we know that the root node has no incoming edges, and that all
        // other such nodes will be pruned.
        self.prune_orphans()?;

        let mut result = vec![];
        let mut next_indices = vec![Self::ROOT_INDEX];
        while let Some(index) = next_indices.pop() {
            if let Some(data) = self.remove(index) {
                result.push(data);
            }

            for child_index in self.children.entry(index).or_default().clone() {
                self.remove_edge(index, child_index)?;
                if self.is_orphaned(&self.nodes[child_index]) {
                    next_indices.push(child_index);
                }
            }
        }
        if self.is_empty() {
            Ok(result)
        } else {
            Err(DependencyError::CircularDependency)
        }
    }
}

impl<K: Eq + Hash + Clone + Debug, V> DependencyGraph<K, V> {
    fn get_index(&self, key: &K) -> Result<usize, DependencyError<K>> {
        let index = self.indices.get(key).ok_or(DependencyError::MissingDependency(key.clone()))?;
        Ok(*index)
    }

    fn remove_edge(
        &mut self,
        from_index: usize,
        to_index: usize,
    ) -> Result<(), DependencyError<K>> {
        let to_parents =
            self.parents.get_mut(&to_index).expect("remove_edge called with invalid to_index");
        to_parents.remove(&from_index);
        let from_children =
            self.children.get_mut(&from_index).expect("remove_edge called with invalid from_index");
        from_children.remove(&to_index);
        Ok(())
    }

    fn is_orphaned(&self, node: &Node<K, V>) -> bool {
        match node {
            Node::Data(key, _) => {
                let index = self.indices.get(key).expect("Key exists in node but not in graph");
                self.parents.get(index).map(|vs| vs.is_empty()).unwrap_or(true)
            }
            Node::Root => false,
            Node::Zombie => false,
        }
    }

    fn remove(&mut self, index: usize) -> Option<V> {
        // Pull out the node while maintaining all the other indices.
        match mem::replace(&mut self.nodes[index], Node::Zombie) {
            Node::Data(_, data) => Some(data),
            _ => None,
        }
    }

    // Prune all orphaned nodes (nodes with no parents). This removes nodes that have no path from
    // the root node.
    fn prune_orphans(&mut self) -> Result<(), DependencyError<K>> {
        while let Some((index, _)) =
            self.nodes.iter().enumerate().find(|(_, node)| self.is_orphaned(node))
        {
            self.remove(index);

            // Remove child edges.
            let child_indices = self.children.entry(index).or_default().clone();
            for child_index in child_indices {
                self.remove_edge(index, child_index)?;
            }
        }

        Ok(())
    }

    fn is_empty(&self) -> bool {
        self.children.values().all(|xs| xs.is_empty())
            && self.parents.values().all(|xs| xs.is_empty())
    }
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn simple() {
        let mut graph = DependencyGraph::new();

        graph.insert_node(1, "one");
        graph.insert_node(2, "two");
        graph.insert_node(3, "three");

        assert_eq!(graph.insert_edge_from_root(&1), Ok(()));
        assert_eq!(graph.insert_edge(&1, &2), Ok(()));
        assert_eq!(graph.insert_edge(&2, &3), Ok(()));

        assert_eq!(graph.resolve(), Ok(vec!["one", "two", "three",]));
    }

    #[test]
    fn empty() {
        let graph: DependencyGraph<usize, usize> = DependencyGraph::new();
        assert!(graph.resolve().unwrap().is_empty());
    }

    #[test]
    fn insert_replaces() {
        let mut graph = DependencyGraph::new();

        graph.insert_node(1, "one");
        graph.insert_node(1, "two");

        assert_eq!(graph.insert_edge_from_root(&1), Ok(()));

        assert_eq!(graph.resolve(), Ok(vec!["two"]));
    }

    #[test]
    fn branching_edges_from_root() {
        let mut graph = DependencyGraph::new();

        graph.insert_node(1, "one");
        graph.insert_node(2, "two");
        graph.insert_node(3, "three");
        graph.insert_node(4, "four");

        assert_eq!(graph.insert_edge_from_root(&1), Ok(()));
        assert_eq!(graph.insert_edge_from_root(&2), Ok(()));

        assert_eq!(graph.insert_edge(&1, &3), Ok(()));
        assert_eq!(graph.insert_edge(&2, &4), Ok(()));

        let result = graph.resolve().unwrap();
        assert!(result.contains(&"one"));
        assert!(result.contains(&"two"));
        assert!(result.contains(&"three"));
        assert!(result.contains(&"four"));
        assert!(
            result.iter().position(|&x| x == "one") < result.iter().position(|&x| x == "three")
        );
        assert!(result.iter().position(|&x| x == "two") < result.iter().position(|&x| x == "four"));
    }

    #[test]
    fn branching_edges() {
        let mut graph = DependencyGraph::new();

        graph.insert_node(1, "one");
        graph.insert_node(2, "two");
        graph.insert_node(3, "three");
        graph.insert_node(4, "four");
        graph.insert_node(5, "five");

        assert_eq!(graph.insert_edge_from_root(&1), Ok(()));

        assert_eq!(graph.insert_edge(&1, &2), Ok(()));
        assert_eq!(graph.insert_edge(&2, &3), Ok(()));

        assert_eq!(graph.insert_edge(&1, &4), Ok(()));
        assert_eq!(graph.insert_edge(&4, &5), Ok(()));

        let result = graph.resolve().unwrap();
        assert!(result.contains(&"one"));
        assert!(result.contains(&"two"));
        assert!(result.contains(&"three"));
        assert!(result.contains(&"four"));
        assert!(result.contains(&"five"));
        assert_eq!(result[0], "one");
        assert!(
            result.iter().position(|&x| x == "two") < result.iter().position(|&x| x == "three")
        );
        assert!(
            result.iter().position(|&x| x == "four") < result.iter().position(|&x| x == "five")
        );
    }

    #[test]
    fn multiple_incoming_edges() {
        let mut graph = DependencyGraph::new();

        graph.insert_node(1, "one");
        graph.insert_node(2, "two");
        graph.insert_node(3, "three");

        assert_eq!(graph.insert_edge_from_root(&1), Ok(()));

        assert_eq!(graph.insert_edge(&1, &2), Ok(()));
        assert_eq!(graph.insert_edge(&1, &3), Ok(()));
        assert_eq!(graph.insert_edge(&2, &3), Ok(()));

        assert_eq!(graph.resolve(), Ok(vec!["one", "two", "three",]));
    }

    #[test]
    fn prunes_disconnected_graph() {
        let mut graph = DependencyGraph::new();

        graph.insert_node(1, "one");
        graph.insert_node(2, "two");
        graph.insert_node(3, "three");

        assert_eq!(graph.insert_edge_from_root(&1), Ok(()));

        assert_eq!(graph.insert_edge(&2, &3), Ok(()));

        assert_eq!(graph.resolve(), Ok(vec!["one"]));
    }

    #[test]
    fn circular_dependency() {
        let mut graph = DependencyGraph::new();

        graph.insert_node(1, "one");
        graph.insert_node(2, "two");
        graph.insert_node(3, "three");

        assert_eq!(graph.insert_edge_from_root(&1), Ok(()));

        assert_eq!(graph.insert_edge(&1, &2), Ok(()));
        assert_eq!(graph.insert_edge(&2, &3), Ok(()));
        assert_eq!(graph.insert_edge(&3, &1), Ok(()));

        assert_eq!(graph.resolve(), Err(DependencyError::CircularDependency));
    }

    #[test]
    fn missing_root_dependency() {
        let mut graph: DependencyGraph<usize, usize> = DependencyGraph::new();

        assert_eq!(graph.insert_edge_from_root(&1), Err(DependencyError::MissingDependency(1)));
    }

    #[test]
    fn missing_dependency() {
        let mut graph = DependencyGraph::new();

        graph.insert_node(1, "one");

        assert_eq!(graph.insert_edge_from_root(&1), Ok(()));

        assert_eq!(graph.insert_edge(&1, &2), Err(DependencyError::MissingDependency(2)));
        assert_eq!(graph.insert_edge(&2, &1), Err(DependencyError::MissingDependency(2)));
    }
}

// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    std::{
        collections::{BTreeMap, HashMap, HashSet, VecDeque},
        hash::Hash,
    },
    thiserror::Error,
};

/// A directed graph, whose nodes contain an identifier of type `T`.
pub struct DirectedGraph<T: Hash + Copy + Ord>(HashMap<T, DirectedNode<T>>);

impl<T: Hash + Copy + Ord> DirectedGraph<T> {
    /// Created a new empty `DirectedGraph`.
    pub fn new() -> Self {
        Self(HashMap::new())
    }

    /// Add an edge to the graph, adding nodes if necessary.
    pub fn add_edge(&mut self, source: T, target: T) {
        self.0.entry(source).or_insert(DirectedNode::new()).add_target(target);
        self.0.entry(target).or_insert(DirectedNode::new());
    }

    /// Get targets of all edges from this node.
    pub fn get_targets(&self, id: T) -> Option<&HashSet<T>> {
        self.0.get(&id).as_ref().map(|node| &node.0)
    }

    /// Returns the nodes of the graph in reverse topological order, or an error if the graph
    /// contains a cycle.
    ///
    /// TODO: //src/devices/tools/banjo/srt/ast.rs can be migrated to use this feature.
    pub fn topological_sort(&self) -> Result<Vec<T>, Error> {
        // The out degree (number of edges) from each node.
        let mut degrees: BTreeMap<T, usize> = BTreeMap::new();
        // Inversion of the graph.
        let mut inverse_graph = DirectedGraph::new();

        for id in self.0.keys() {
            degrees.insert(*id, 0);
        }
        for id in self.0.keys() {
            for target in self.0[id].0.iter() {
                let entry = degrees.get_mut(id).unwrap();
                *entry += 1;
                inverse_graph.add_edge(target, id);
            }
        }
        // Remove mutability.
        let inverse_graph = inverse_graph;

        // Start with all nodes that have no incoming edges.
        let mut nodes_without_targets = degrees
            .iter()
            .filter(|(_, &degrees)| degrees == 0)
            .map(|(&id, _)| id)
            .collect::<VecDeque<_>>();

        let mut node_order: Vec<T> = vec![];
        // Pull one out of the queue.
        while let Some(id) = nodes_without_targets.pop_front() {
            assert_eq!(degrees.get(&id), Some(&0));
            node_order.push(id);

            // Decrement the incoming degree of all other nodes it points to.
            if let Some(inverse_targets) = inverse_graph.get_targets(&id) {
                let mut inverse_targets: Vec<T> = inverse_targets.iter().map(|t| **t).collect();
                inverse_targets.sort_unstable();
                for inverse_target in inverse_targets {
                    let degree = degrees.get_mut(&inverse_target).unwrap();
                    assert_ne!(*degree, 0);
                    *degree -= 1;
                    if *degree == 0 {
                        nodes_without_targets.push_back(inverse_target);
                    }
                }
            }
        }

        if node_order.len() != degrees.len() {
            // We didn't visit all the edges! There was a cycle.
            return Err(Error::CycleDetected);
        }
        Ok(node_order)
    }
}

/// A graph node. Contents contain the nodes mapped by edges from this node.
#[derive(Eq, PartialEq)]
struct DirectedNode<T: Hash + Copy + Ord>(HashSet<T>);

impl<T: Hash + Copy + Ord> DirectedNode<T> {
    /// Create an empty node.
    pub fn new() -> Self {
        Self(HashSet::new())
    }

    /// Add edge from this node to `target`.
    pub fn add_target(&mut self, target: T) {
        self.0.insert(target);
    }
}

/// Errors produced by `DirectedGraph`.
#[derive(Debug, Error)]
pub enum Error {
    #[error("a cycle was detected in the graph")]
    CycleDetected,
}

#[cfg(test)]
mod tests {
    use super::*;
    use matches::assert_matches;

    macro_rules! test_topological_sort {
        (
            $(
                $test_name:ident => {
                    edges = $edges:expr,
                    order = $order:expr,
                },
            )+
        ) => {
            $(
                #[test]
                fn $test_name() {
                    topological_sort_test(&$edges, &$order);
                }
            )+
        }
    }

    macro_rules! test_cycles {
        (
            $(
                $test_name:ident => {
                    edges = $edges:expr,
                },
            )+
        ) => {
            $(
                #[test]
                fn $test_name() {
                    cycles_test(&$edges);
                }
            )+
        }
    }

    fn topological_sort_test(edges: &[(&'static str, &'static str)], order: &[&'static str]) {
        let mut graph = DirectedGraph::new();
        edges.iter().for_each(|e| graph.add_edge(e.0, e.1));
        let actual_order = graph.topological_sort().expect("found a cycle");
        let expected_order: Vec<_> = order.iter().cloned().collect();
        assert_eq!(actual_order, expected_order, "Order doesn't match");
    }

    fn cycles_test(edges: &[(&'static str, &'static str)]) {
        let mut graph = DirectedGraph::new();
        edges.iter().for_each(|e| graph.add_edge(e.0, e.1));
        assert_matches!(graph.topological_sort(), Err(Error::CycleDetected));
    }

    test_topological_sort! {
        test_empty => {
            edges = [],
            order = [],
        },
        test_fan_out => {
            edges = [
                ("a", "b"),
                ("b", "c"),
                ("b", "d"),
                ("d", "e"),
            ],
            order = ["c", "e", "d", "b", "a"],
        },
        test_fan_in => {
            edges = [
                ("a", "b"),
                ("b", "d"),
                ("c", "d"),
                ("d", "e"),
            ],
            order = ["e", "d", "b", "c", "a"],
        },
        test_forest => {
            edges = [
                ("a", "b"),
                ("b", "c"),
                ("d", "e"),
            ],
            order = ["c", "e", "b", "d", "a"],
        },
        test_diamond => {
            edges = [
                ("a", "b"),
                ("a", "c"),
                ("b", "d"),
                ("c", "d"),
            ],
            order = ["d", "b", "c", "a"],
        },
        test_lattice => {
            edges = [
                ("a", "b"),
                ("a", "c"),
                ("b", "d"),
                ("b", "e"),
                ("c", "d"),
                ("e", "f"),
                ("d", "f"),
            ],
            order = ["f", "d", "e", "c", "b", "a"],
        },
        test_deduped_edge => {
            edges = [
                ("a", "b"),
                ("a", "b"),
                ("b", "c"),
            ],
            order = ["c", "b", "a"],
        },
    }

    test_cycles! {
        test_cycle_self_referential => {
            edges = [
                ("c", "d"),
                ("a", "a"),
            ],
        },
        test_cycle_one => {
            edges = [
                ("a", "b"),
                ("b", "c"),
                ("c", "d"),
                ("d", "a"),
            ],
        },
        test_cycle_two => {
            edges = [
                ("a", "b"),
                ("b", "c"),
                ("c", "a"),
                ("d", "e"),
                ("e", "f"),
                ("f", "d"),
            ],
        },
        test_cycle_connected => {
           edges = [
                ("a", "b"),
                ("b", "c"),
                ("c", "a"),
                ("a", "d"),
                ("d", "e"),
                ("e", "b"),
            ],
        },
        test_cycle_path_into => {
            edges = [
                ("a", "b"),
                ("b", "c"),
                ("c", "d"),
                ("d", "e"),
                ("e", "c"),
            ],
        },
        test_cycle_path_out_of => {
            edges = [
                ("a", "b"),
                ("b", "c"),
                ("c", "a"),
                ("c", "d"),
                ("d", "e"),
            ],
        },
    }
}

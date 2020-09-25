// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{
    cmp::min,
    collections::{HashMap, HashSet},
    default::Default,
    fmt::{Debug, Display},
    hash::Hash,
};

/// A directed graph, whose nodes contain an identifier of type `T`.
pub struct DirectedGraph<T: Hash + Copy + Ord + Debug + Display>(HashMap<T, DirectedNode<T>>);

impl<T: Hash + Copy + Ord + Debug + Display> DirectedGraph<T> {
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
    pub fn topological_sort(&self) -> Result<Vec<T>, Error<T>> {
        TarjanSCC::new(self).run()
    }
}

impl<T: Hash + Copy + Ord + Debug + Display> Default for DirectedGraph<T> {
    fn default() -> Self {
        Self(HashMap::new())
    }
}

/// A graph node. Contents contain the nodes mapped by edges from this node.
#[derive(Eq, PartialEq)]
struct DirectedNode<T: Hash + Copy + Ord + Debug + Display>(HashSet<T>);

impl<T: Hash + Copy + Ord + Debug + Display> DirectedNode<T> {
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
#[derive(Debug)]
pub enum Error<T: Hash + Copy + Ord + Debug + Display> {
    CyclesDetected(HashSet<Vec<T>>),
}

impl<T: Hash + Copy + Ord + Debug + Display> Error<T> {
    pub fn format_cycle(&self) -> String {
        match &self {
            Error::CyclesDetected(cycles) => {
                // Copy the cycles into a vector and sort them so our output is stable
                let mut cycles: Vec<_> = cycles.iter().cloned().collect();
                cycles.sort_unstable();

                let mut output = "{".to_string();
                for cycle in cycles.iter() {
                    output.push_str("{");
                    for item in cycle.iter() {
                        output.push_str(&format!("{} -> ", item));
                    }
                    if !cycle.is_empty() {
                        output.truncate(output.len() - 4);
                    }
                    output.push_str("}, ");
                }
                if !cycles.is_empty() {
                    output.truncate(output.len() - 2);
                }
                output.push_str("}");
                output
            }
        }
    }
}

/// Runs the tarjan strongly connected components algorithm on a graph to produce either a reverse
/// topological sort of the nodes in the graph, or a set of the cycles present in the graph.
///
/// Description of algorithm:
/// https://en.wikipedia.org/wiki/Tarjan%27s_strongly_connected_components_algorithm
struct TarjanSCC<'a, T: Hash + Copy + Ord + Debug + Display> {
    // Each node is assigned an index in the order we find them. This tracks the next index to use.
    index: u64,
    // The mappings between nodes and indices
    indices: HashMap<T, u64>,
    // The lowest index (numerically) that's accessible from each node
    low_links: HashMap<T, u64>,
    // The set of nodes we're currently in the process of considering
    stack: Vec<T>,
    // A set containing the nodes in the stack, so we can more efficiently check if an element is
    // in the stack
    on_stack: HashSet<T>,
    // Detected cycles
    cycles: HashSet<Vec<T>>,
    // Nodes sorted by reverse topological order
    node_order: Vec<T>,
    // The graph this run will be operating on
    graph: &'a DirectedGraph<T>,
}

impl<'a, T: Hash + Copy + Ord + Debug + Display> TarjanSCC<'a, T> {
    fn new(graph: &'a DirectedGraph<T>) -> Self {
        TarjanSCC {
            index: 0,
            indices: HashMap::new(),
            low_links: HashMap::new(),
            stack: Vec::new(),
            on_stack: HashSet::new(),
            cycles: HashSet::new(),
            node_order: Vec::new(),
            graph,
        }
    }

    /// Runs the tarjan scc algorithm. Must only be called once, as it will panic on subsequent
    /// calls.
    fn run(mut self) -> Result<Vec<T>, Error<T>> {
        // Sort the nodes we visit, to make the output deterministic instead of being based on
        // whichever node we find first.
        let mut nodes: Vec<_> = self.graph.0.keys().cloned().collect();
        nodes.sort_unstable();
        for node in &nodes {
            // Iterate over each node, visiting each one we haven't already visited. We determine
            // if a node has been visited by if an index has been assigned to it yet.
            if !self.indices.contains_key(node) {
                self.visit(*node);
            }
        }

        if self.cycles.is_empty() {
            Ok(self.node_order.drain(..).collect())
        } else {
            Err(Error::CyclesDetected(self.cycles.drain().collect()))
        }
    }

    fn visit(&mut self, current_node: T) {
        // assign a new index for this node, and push it on to the stack
        self.indices.insert(current_node, self.index);
        self.low_links.insert(current_node, self.index);
        self.index += 1;
        self.stack.push(current_node);
        self.on_stack.insert(current_node);

        let mut targets: Vec<_> = self.graph.0[&current_node].0.iter().cloned().collect();
        targets.sort_unstable();

        for target in &targets {
            if !self.indices.contains_key(target) {
                // Target has not yet been visited; recurse on it
                self.visit(*target);
                // Set our lowlink to the min of our lowlink and the target's new lowlink
                let current_node_low_link = *self.low_links.get(&current_node).unwrap();
                let target_low_link = *self.low_links.get(&target).unwrap();
                self.low_links.insert(current_node, min(current_node_low_link, target_low_link));
            } else if self.on_stack.contains(target) {
                let current_node_low_link = *self.low_links.get(&current_node).unwrap();
                let target_index = *self.indices.get(&target).unwrap();
                self.low_links.insert(current_node, min(current_node_low_link, target_index));
            }
        }

        // If current_node is a root node, pop the stack and generate an SCC
        if self.low_links.get(&current_node) == self.indices.get(&current_node) {
            let mut strongly_connected_nodes = HashSet::new();
            let mut stack_node;
            loop {
                stack_node = self.stack.pop().unwrap();
                self.on_stack.remove(&stack_node);
                strongly_connected_nodes.insert(stack_node);
                if stack_node == current_node {
                    break;
                }
            }
            self.insert_cycles_from_scc(
                &strongly_connected_nodes,
                stack_node,
                HashSet::new(),
                vec![],
            );
        }
        self.node_order.push(current_node);
    }

    /// Given a set of strongly connected components, computes the cycles present in the set and
    /// adds those cycles to self.cycles.
    fn insert_cycles_from_scc(
        &mut self,
        scc_nodes: &HashSet<T>,
        current_node: T,
        mut visited_nodes: HashSet<T>,
        mut path: Vec<T>,
    ) {
        if visited_nodes.contains(&current_node) {
            // We've already visited this node, we've got a cycle. Grab all the elements in the
            // path starting at the first time we visited this node.
            let (current_node_path_index, _) =
                path.iter().enumerate().find(|(_, val)| val == &&current_node).unwrap();
            let mut cycle = path[current_node_path_index..].to_vec();

            // Rotate the cycle such that the lowest value comes first, so that the cycles we
            // report are consistent.
            Self::rotate_cycle(&mut cycle);
            // Push a copy of the first node on to the end, so it's clear that this path ends where
            // it starts
            cycle.push(*cycle.first().unwrap());
            self.cycles.insert(cycle);
            return;
        }

        visited_nodes.insert(current_node);
        path.push(current_node);

        let targets_in_scc: Vec<_> =
            self.graph.0[&current_node].0.iter().filter(|n| scc_nodes.contains(n)).collect();
        for target in targets_in_scc {
            self.insert_cycles_from_scc(scc_nodes, *target, visited_nodes.clone(), path.clone());
        }
    }

    /// Rotates the cycle such that ordering is maintained and the lowest element comes first. This
    /// is so that the reported cycles are consistent, as opposed to varying based on which node we
    /// happened to find first.
    fn rotate_cycle(cycle: &mut Vec<T>) {
        let mut lowest_index = 0;
        let mut lowest_value = cycle.first().unwrap();
        for (index, node) in cycle.iter().enumerate() {
            if node < lowest_value {
                lowest_index = index;
                lowest_value = node;
            }
        }
        cycle.rotate_left(lowest_index);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

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
                    cycles = $cycles:expr,
                },
            )+
        ) => {
            $(
                #[test]
                fn $test_name() {
                    cycles_test(&$edges, &$cycles);
                }
            )+
        }
    }

    fn topological_sort_test(edges: &[(&'static str, &'static str)], order: &[&'static str]) {
        let mut graph = DirectedGraph::new();
        edges.iter().for_each(|e| graph.add_edge(e.0, e.1));
        let actual_order = graph.topological_sort().expect("found a cycle");

        let expected_order: Vec<_> = order.iter().cloned().collect();
        assert_eq!(expected_order, actual_order);
    }

    fn cycles_test(edges: &[(&'static str, &'static str)], cycles: &[&[&'static str]]) {
        let mut graph = DirectedGraph::new();
        edges.iter().for_each(|e| graph.add_edge(e.0, e.1));
        let Error::CyclesDetected(reported_cycles) = graph
            .topological_sort()
            .expect_err("topological sort succeeded on a dataset with a cycle");

        let expected_cycles: HashSet<Vec<_>> =
            cycles.iter().cloned().map(|c| c.iter().cloned().collect()).collect();
        assert_eq!(reported_cycles, expected_cycles);
    }

    // Tests with no cycles

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
            order = ["e", "d", "b", "a", "c"],
        },
        test_forest => {
            edges = [
                ("a", "b"),
                ("b", "c"),
                ("d", "e"),
            ],
            order = ["c", "b", "a", "e", "d"],
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
            order = ["f", "d", "e", "b", "c", "a"],
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
        // Tests where only 1 SCC contains cycles

        test_cycle_self_referential => {
            edges = [
                ("a", "a"),
            ],
            cycles = [
                &["a", "a"],
            ],
        },
        test_cycle_two_nodes => {
            edges = [
                ("a", "b"),
                ("b", "a"),
            ],
            cycles = [
                &["a", "b", "a"],
            ],
        },
        test_cycle_two_nodes_with_path_in => {
            edges = [
                ("a", "b"),
                ("b", "c"),
                ("c", "d"),
                ("d", "c"),
            ],
            cycles = [
                &["c", "d", "c"],
            ],
        },
        test_cycle_two_nodes_with_path_out => {
            edges = [
                ("a", "b"),
                ("b", "a"),
                ("b", "c"),
                ("c", "d"),
            ],
            cycles = [
                &["a", "b", "a"],
            ],
        },
        test_cycle_three_nodes => {
            edges = [
                ("a", "b"),
                ("b", "c"),
                ("c", "a"),
            ],
            cycles = [
                &["a", "b", "c", "a"],
            ],
        },
        test_cycle_three_nodes_with_inner_cycle => {
            edges = [
                ("a", "b"),
                ("b", "c"),
                ("c", "b"),
                ("c", "a"),
            ],
            cycles = [
                &["a", "b", "c", "a"],
                &["b", "c", "b"],
            ],
        },
        test_cycle_three_nodes_doubly_linked => {
            edges = [
                ("a", "b"),
                ("b", "a"),
                ("b", "c"),
                ("c", "b"),
                ("c", "a"),
                ("a", "c"),
            ],
            cycles = [
                &["a", "b", "a"],
                &["b", "c", "b"],
                &["a", "c", "a"],
                &["a", "b", "c", "a"],
                &["a", "c", "b", "a"],
            ],
        },
        test_cycle_with_inner_cycle => {
            edges = [
                ("a", "b"),
                ("b", "c"),
                ("c", "a"),

                ("b", "d"),
                ("d", "e"),
                ("e", "c"),
            ],
            cycles = [
                &["a", "b", "c", "a"],
                &["a", "b", "d", "e", "c", "a"],
            ],
        },
        test_two_join_cycles => {
            edges = [
                ("a", "b"),
                ("b", "c"),
                ("c", "a"),
                ("b", "d"),
                ("d", "a"),
            ],
            cycles = [
                &["a", "b", "c", "a"],
                &["a", "b", "d", "a"],
            ],
        },
        test_cycle_four_nodes_doubly_linked => {
            edges = [
                ("a", "b"),
                ("b", "a"),
                ("b", "c"),
                ("c", "b"),
                ("c", "d"),
                ("d", "c"),
                ("d", "a"),
                ("a", "d"),
            ],
            cycles = [
                &["a", "b", "c", "d", "a"],
                &["a", "b", "a"],
                &["a", "d", "c", "b", "a"],
                &["a", "d", "a"],
                &["b", "c", "b"],
                &["c", "d", "c"],
            ],
        },

        // Tests with multiple SCCs that contain cycles

        test_cycle_self_referential_islands => {
            edges = [
                ("a", "a"),
                ("b", "b"),
                ("c", "c"),
                ("d", "e"),
            ],
            cycles = [
                &["a", "a"],
                &["b", "b"],
                &["c", "c"],
            ],
        },
        test_cycle_two_sets_of_two_nodes => {
            edges = [
                ("a", "b"),
                ("b", "a"),
                ("c", "d"),
                ("d", "c"),
            ],
            cycles = [
                &["a", "b", "a"],
                &["c", "d", "c"],
            ],
        },
        test_cycle_two_sets_of_two_nodes_connected => {
            edges = [
                ("a", "b"),
                ("b", "a"),
                ("c", "d"),
                ("d", "c"),
                ("a", "c"),
            ],
            cycles = [
                &["a", "b", "a"],
                &["c", "d", "c"],
            ],
        },

    }
}

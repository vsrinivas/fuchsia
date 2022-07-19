// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    fidl_fuchsia_diagnostics::Selector,
    fuchsia_inspect::{
        hierarchy::{DiagnosticsHierarchy, InspectHierarchyMatcher},
        testing::DiagnosticsHierarchyGetter,
        Inspector,
    },
    rand::{distributions::Uniform, Rng, SeedableRng},
    selectors::VerboseError,
    std::{
        borrow::Cow,
        collections::{HashSet, VecDeque},
        convert::TryInto,
        sync::Arc,
    },
};

pub struct InspectHierarchyGenerator<R: SeedableRng + Rng> {
    rng: R,
    pub inspector: Inspector,
}

impl<R: SeedableRng + Rng> InspectHierarchyGenerator<R> {
    pub fn new(rng: R, inspector: Inspector) -> Self {
        Self { rng, inspector }
    }

    /// Generates prufer sequence by sampling uniformly from the rng.
    fn generate_prufer_sequence(&mut self, n: usize) -> Vec<usize> {
        (0..n - 2).map(|_| self.rng.sample(Uniform::new(0, n))).collect()
    }

    /// Generate hierarchy from prufer sequence
    fn generate_hierarchy_from_prufer(&mut self, sequence: &[usize]) {
        let n = sequence.len() + 2;
        let mut degree = vec![1; n];

        for node in sequence {
            degree[*node] += 1;
        }

        let mut ptr = 0;
        while ptr < n && degree[ptr] != 1 {
            ptr += 1;
        }
        let mut leaf = ptr;

        let mut edges: Vec<Vec<usize>> = vec![Vec::new(); n];
        for v in sequence {
            edges[leaf].push(*v);
            edges[*v].push(leaf);

            degree[*v] -= 1;
            if degree[*v] == 1 && *v < ptr {
                leaf = *v;
            } else {
                ptr += 1;
                while ptr < n && degree[ptr] != 1 {
                    ptr += 1;
                }
                leaf = ptr;
            }
        }
        edges[leaf].push(n - 1);
        edges[n - 1].push(leaf);

        // Now we have the tree in undirectred form
        // we will construct the inspect hierarchy assuming 0 to be
        // inspector root
        let mut unprocessed_nodes = VecDeque::new();
        unprocessed_nodes.push_back((0, self.inspector.root().clone_weak()));

        let mut processed_edges: HashSet<(usize, usize)> = HashSet::with_capacity(n - 1);

        while let Some((u, node)) = unprocessed_nodes.pop_front() {
            for v in &edges[u] {
                // Already processed v -> u
                if processed_edges.contains(&(*v, u)) {
                    continue;
                }
                // Processed edge from u -> v
                processed_edges.insert((u, *v));

                let child_node = node.create_child(format!("child_{}", *v));

                // Add IntProperty to the child node
                child_node.record_uint("value", *v as u64);
                unprocessed_nodes.push_back((*v, child_node.clone_weak()));
                node.record(child_node);
            }
        }
    }

    /// Generate random inspect hierarchy with n nodes.
    pub fn generate_hierarchy(&mut self, n: usize) {
        let prufer_sequence = self.generate_prufer_sequence(n);
        self.generate_hierarchy_from_prufer(&prufer_sequence);
    }
}

impl<R: SeedableRng + Rng> DiagnosticsHierarchyGetter<String> for InspectHierarchyGenerator<R> {
    fn get_diagnostics_hierarchy(&self) -> Cow<'_, DiagnosticsHierarchy> {
        self.inspector.get_diagnostics_hierarchy()
    }
}

/// Generate selectors which selects the nodes in the tree
/// and all the properties on the nodes till a given depth.
pub fn generate_selectors_till_level(depth: usize) -> Vec<String> {
    // TODO(fxbug.dev/104109): Create a good combination of wildcards and exact matches
    let mut selector: String = String::from("*:root");
    (0..depth)
        .map(|_| {
            let current_selector = format!("{}:*", selector);
            selector.push_str("/*");
            current_selector
        })
        .collect()
}

/// Parse selectors and returns an InspectHierarchyMatcher
pub fn parse_selectors(selectors: &[String]) -> InspectHierarchyMatcher {
    selectors
        .into_iter()
        .map(|selector| {
            Arc::new(
                selectors::parse_selector::<VerboseError>(selector)
                    .expect("Selectors must be valid and parseable."),
            )
        })
        .collect::<Vec<Arc<Selector>>>()
        .try_into()
        .expect("Unable to construct hierarchy matcher from selectors.")
}

#[cfg(test)]
mod tests {
    use super::*;
    use fuchsia_inspect::assert_json_diff;
    use pretty_assertions::assert_eq;
    use rand::rngs::StdRng;

    #[fuchsia::test]
    async fn random_generated_hierarchy_is_reproducible() {
        let inspector = Inspector::new();
        let mut hierarchy_generator =
            InspectHierarchyGenerator::new(StdRng::seed_from_u64(0), inspector);
        hierarchy_generator.generate_hierarchy(10);
        let result = hierarchy_generator.get_diagnostics_hierarchy();
        assert_json_diff!(
        result,
        root:{
            child_4:{
                value:4
            },
            child_5:{
                value:5,
                child_2:{
                    value:2,
                    child_7:{
                        value:7,
                        child_1:{
                            value:1
                        },
                        child_3:{
                            value:3
                        },
                        child_6:{
                            value:6
                        },
                        child_9:{
                            value:9
                        }
                    },
                    child_8:{
                        value:8
                    }
                }
            }
        });
    }

    #[fuchsia::test]
    fn generated_selectors_test() {
        let selectors = generate_selectors_till_level(3);
        assert_eq!(selectors, vec!["*:root:*", "*:root/*:*", "*:root/*/*:*"]);
    }
}

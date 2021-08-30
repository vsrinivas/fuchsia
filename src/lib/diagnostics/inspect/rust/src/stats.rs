// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
//! # Inspect stats node.
//!
//! Stats installs a lazy node (or a node with a snapshot) reporting stats about the Inspect being
//! served by the component (such as size, number of dynamic children, etc).
//!
//! # Examples
//!
//! ```
//! use fuchsia_inspect as inspect;
//! use fuchsia_inspect::stats;
//!
//! let inspector = /* the inspector of your choice */
//! let mut root = inspector.root();  // Or perhaps a different Inspect Node of your choice.
//! let mut stats = stats::Node::new(&inspector, root);
//! ```

use super::{InspectType, Inspector, LazyNode, State};
use futures::FutureExt;

/// Contains information about inspect such as size and number of dynamic children.
pub struct Node<T: InspectType> {
    inner: T,
}

// The metric node name, as exposed by the stats node.
const FUCHSIA_INSPECT_STATS: &str = "fuchsia.inspect.Stats";
const CURRENT_SIZE_KEY: &str = "current_size";
const MAXIMUM_SIZE_KEY: &str = "maximum_size";
const TOTAL_DYNAMIC_CHILDREN_KEY: &str = "total_dynamic_children";
const ALLOCATED_BLOCKS_KEY: &str = "allocated_blocks";
const DEALLOCATED_BLOCKS_KEY: &str = "deallocated_blocks";
const FAILED_ALLOCATIONS_KEY: &str = "failed_allocations";

impl<T: InspectType> Node<T> {
    /// Unwraps the underlying lazy node and returns it.
    pub fn take(self) -> T {
        self.inner
    }
}

impl Node<LazyNode> {
    /// Creates a new stats node as a child of `parent` that will expose the given `Inspector`
    /// stats.
    pub fn new(inspector: &Inspector, parent: &super::Node) -> Self {
        let weak_root_node = inspector.root().clone_weak();
        let lazy_node = parent.create_lazy_child(FUCHSIA_INSPECT_STATS, move || {
            let root_node = weak_root_node.clone_weak();
            async move {
                let inspector = Inspector::new();
                if let Some(state) = root_node.state() {
                    write_stats(&state, inspector.root());
                }
                Ok(inspector)
            }
            .boxed()
        });
        Self { inner: lazy_node }
    }
}

impl Node<super::Node> {
    /// Takes a snapshot of the stats and writes them to the given parent.
    pub fn snapshot(inspector: &Inspector, parent: &super::Node) -> Self {
        let node = parent.create_child(FUCHSIA_INSPECT_STATS);
        if let Some(state) = inspector.state() {
            write_stats(&state, &node);
        }
        Self { inner: node }
    }
}

fn write_stats(state: &State, node: &super::Node) {
    if let Some(stats) = state.try_lock().ok().map(|state| state.stats()) {
        node.record_uint(CURRENT_SIZE_KEY, stats.current_size as u64);
        node.record_uint(MAXIMUM_SIZE_KEY, stats.maximum_size as u64);
        node.record_uint(TOTAL_DYNAMIC_CHILDREN_KEY, stats.total_dynamic_children as u64);
        node.record_uint(ALLOCATED_BLOCKS_KEY, stats.allocated_blocks as u64);
        node.record_uint(DEALLOCATED_BLOCKS_KEY, stats.deallocated_blocks as u64);
        node.record_uint(FAILED_ALLOCATIONS_KEY, stats.failed_allocations as u64);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::assert_data_tree;
    use inspect_format::constants;

    #[test]
    fn inspect_stats() {
        let inspector = Inspector::new();
        let snapshot_parent = inspector.root().create_child("snapshot");
        let _snapshot_node = super::Node::snapshot(&inspector, &snapshot_parent);
        let _node = super::Node::new(&inspector, inspector.root());
        inspector.root().record_lazy_child("foo", || {
            async move {
                let inspector = Inspector::new();
                inspector.root().record_uint("a", 1);
                Ok(inspector)
            }
            .boxed()
        });
        assert_data_tree!(inspector, root: {
            foo: {
                a: 1u64,
            },
            "snapshot": {
                "fuchsia.inspect.Stats": {
                    current_size: 4096u64,
                    maximum_size: constants::DEFAULT_VMO_SIZE_BYTES as u64,
                    total_dynamic_children: 0u64,  // snapshot was taken before adding any lazy node.
                    allocated_blocks: 5u64,
                    deallocated_blocks: 0u64,
                    failed_allocations: 0u64,
                },
            },
            "fuchsia.inspect.Stats": {
                current_size: 4096u64,
                maximum_size: constants::DEFAULT_VMO_SIZE_BYTES as u64,
                total_dynamic_children: 2u64,
                allocated_blocks: 23u64,
                deallocated_blocks: 0u64,
                failed_allocations: 0u64,
            }
        });

        for i in 0..100 {
            inspector.root().record_string(format!("testing-{}", i), "testing".repeat(i + 1));
        }

        {
            let _ = inspector.root().create_int("drop", 1);
        }

        assert_data_tree!(inspector, root: contains {
            "fuchsia.inspect.Stats": {
                current_size: 61440u64,
                maximum_size: constants::DEFAULT_VMO_SIZE_BYTES as u64,
                total_dynamic_children: 2u64,
                allocated_blocks: 325u64,
                // 2 blocks are deallocated because of the "drop" int block and its
                // STRING_REFERENCE
                deallocated_blocks: 2u64,
                failed_allocations: 0u64,
            }
        });

        for i in 101..220 {
            inspector.root().record_string(format!("testing-{}", i), "testing".repeat(i + 1));
        }

        assert_data_tree!(inspector, root: contains {
            "fuchsia.inspect.Stats": {
                current_size: 262144u64,
                maximum_size: constants::DEFAULT_VMO_SIZE_BYTES as u64,
                total_dynamic_children: 2u64,
                allocated_blocks: 681u64,
                // 2 additional blocks are deallocated because of the failed allocation
                deallocated_blocks: 4u64,
                failed_allocations: 1u64,
            }
        });
    }
}

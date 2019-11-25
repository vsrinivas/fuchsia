// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::{ManagedNode, NodeWriter};

use fuchsia_inspect::Node;
use std::collections::VecDeque;

/// This struct is intended to represent a list node in Inspect, which doesn't support list
/// natively. Furthermore, it makes sure that the number of items does not exceed |capacity|
///
/// Each item in `BoundedListNode` is represented as a child node with name as index. This
/// index is always increasing and does not wrap around. For example, if capacity is 3,
/// then the children names are `[0, 1, 2]` on first three addition. When a new node is
/// added, `0` is popped, and the children names are `[1, 2, 3]`.
pub struct BoundedListNode {
    node: Node,
    index: usize,
    capacity: usize,
    items: VecDeque<ManagedNode>,
}

impl BoundedListNode {
    /// Create a new BoundedListNode with capacity 1 or |capacity|, whichever is larger.
    pub fn new(node: Node, capacity: usize) -> Self {
        Self {
            node,
            index: 0,
            capacity: std::cmp::max(capacity, 1),
            items: VecDeque::with_capacity(capacity),
        }
    }

    /// Create a new entry within a list and return a writer that creates properties or children
    /// for this entry. The writer does not have to be kept for the created properties and
    /// children to be maintained in the list.
    ///
    /// If creating new entry exceeds capacity of the list, the oldest entry is evicted.
    pub fn create_entry(&mut self) -> NodeWriter<'_> {
        if self.items.len() >= self.capacity {
            self.items.pop_front();
        }

        let entry = self.node.create_child(&self.index.to_string());
        let entry_node = ManagedNode::new(entry);
        self.items.push_back(entry_node);

        self.index += 1;
        self.items.back_mut().unwrap().writer()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use fuchsia_inspect::{assert_inspect_tree, Inspector};

    #[test]
    fn test_bounded_list_node_basic() {
        let inspector = Inspector::new();
        let list_node = inspector.root().create_child("list_node");
        let mut list_node = BoundedListNode::new(list_node, 3);
        let _ = list_node.create_entry();
        assert_inspect_tree!(inspector, root: { list_node: { "0": {} } });
        let _ = list_node.create_entry();
        assert_inspect_tree!(inspector, root: { list_node: { "0": {}, "1": {} } });
    }

    #[test]
    fn test_bounded_list_node_eviction() {
        let inspector = Inspector::new();
        let list_node = inspector.root().create_child("list_node");
        let mut list_node = BoundedListNode::new(list_node, 3);
        let _ = list_node.create_entry();
        let _ = list_node.create_entry();
        let _ = list_node.create_entry();

        assert_inspect_tree!(inspector, root: { list_node: { "0": {}, "1": {}, "2": {} } });

        let _ = list_node.create_entry();
        assert_inspect_tree!(inspector, root: { list_node: { "1": {}, "2": {}, "3": {} } });

        let _ = list_node.create_entry();
        assert_inspect_tree!(inspector, root: { list_node: { "2": {}, "3": {}, "4": {} } });
    }

    #[test]
    fn test_bounded_list_node_specified_zero_capacity() {
        let inspector = Inspector::new();
        let list_node = inspector.root().create_child("list_node");
        let mut list_node = BoundedListNode::new(list_node, 0);
        let _ = list_node.create_entry();
        assert_inspect_tree!(inspector, root: { list_node: { "0": {} } });
        let _ = list_node.create_entry();
        assert_inspect_tree!(inspector, root: { list_node: { "1": {} } });
    }

    #[test]
    fn test_bounded_list_node_holds_its_values() {
        let inspector = Inspector::new();
        let list_node = inspector.root().create_child("list_node");
        let mut list_node = BoundedListNode::new(list_node, 3);

        {
            let mut node_writer = list_node.create_entry();
            node_writer
                .create_string("str_key", "str_value")
                .create_child("child")
                .create_int("int_key", 2);
        } // <-- node_writer is dropped

        // verify list node 0 is still in the tree
        assert_inspect_tree!(inspector, root: {
            list_node: {
                "0": {
                    str_key: "str_value",
                    child: {
                        int_key: 2i64,
                    }
                }
            }
        });
    }
}

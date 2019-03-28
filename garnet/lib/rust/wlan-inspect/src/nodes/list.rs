// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{NodeExt, SharedNodePtr};

/// This struct is intended to represent a list node in Inspect, which doesn't support list
/// natively. Furthermore, it makes sure that the number of items does not exceed |capacity|
///
/// Each item in `BoundedListNode` is represented as a child node with name as index. This
/// index is always increasing and does not wrap around. For example, if capacity is 3,
/// then the children names are `[0, 1, 2]` on first three addition. When a new node is
/// added, `0` is popped, and the children names are `[1, 2, 3]`.
pub struct BoundedListNode {
    node: SharedNodePtr,
    index: usize,
    capacity: usize,
}

impl BoundedListNode {
    pub fn new(node: SharedNodePtr, capacity: usize) -> Self {
        Self { node, index: 0, capacity }
    }

    pub fn request_entry(&mut self) -> SharedNodePtr {
        let mut node = self.node.lock();
        let entry = node.create_child(&self.index.to_string());
        if self.index >= self.capacity {
            let stale_node_index = self.index - self.capacity;
            node.remove_child(&stale_node_index.to_string());
        }
        self.index += 1;
        entry
    }

    #[cfg(test)]
    pub fn inner(&self) -> SharedNodePtr {
        self.node.clone()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use fuchsia_inspect as finspect;

    #[test]
    fn test_bounded_list_node() {
        let mut list_node = BoundedListNode::new(finspect::ObjectTreeNode::new_root(), 3);
        let _ = list_node.request_entry();
        let _ = list_node.request_entry();
        let _ = list_node.request_entry();
        verify_children(&list_node, vec!["0", "1", "2"]);

        let _ = list_node.request_entry();
        verify_children(&list_node, vec!["1", "2", "3"]);

        let _ = list_node.request_entry();
        verify_children(&list_node, vec!["2", "3", "4"]);
    }

    fn verify_children(list_node: &BoundedListNode, expected_names: Vec<&str>) {
        let mut children_names = list_node.node.lock().get_children_names();
        children_names.sort();
        assert_eq!(children_names, expected_names);
    }
}

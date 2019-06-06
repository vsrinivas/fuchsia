// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_inspect::vmo::Node;
use parking_lot::Mutex;
use std::collections::{HashMap, VecDeque};
use std::sync::Arc;

type IfaceId = u16;

/// This class is intended to be used by Inspect nodes that need to hold a number of iface
/// children nodes. For ifaces that have been removed, it keeps that iface child node and
/// mark it as dead. When the number of dead iface nodes exceeds |max_dead_nodes|, the oldest
/// dead node is removed.
///
/// The intention is that even if a user unplugs a device, we want to keep that iface node's
/// debug information. However, the number of dead iface children shouldn't grow unbounded
/// (e.g., if the user unplugs and replugs the same device repeatedly, or tests continuously
/// create and destroy virtual devices).
pub struct IfacesTrees {
    /// Iface devices still in use
    live_ifaces: HashMap<IfaceId, Arc<IfaceTreeHolder>>,
    /// Iface devices that have been removed but whose debug infos are still kept in Inspect tree
    dead_ifaces: VecDeque<Arc<IfaceTreeHolder>>,
    max_dead_nodes: usize,
}

impl IfacesTrees {
    pub fn new(max_dead_nodes: usize) -> Self {
        Self { live_ifaces: HashMap::new(), dead_ifaces: VecDeque::new(), max_dead_nodes }
    }

    /// Create iface node as child of |node|. Return an IfaceTreeHolder into which the client can
    /// insert the rest of the tree.
    pub fn create_iface_child(&mut self, node: &Node, iface_id: IfaceId) -> Arc<IfaceTreeHolder> {
        let child = node.create_child(&format!("iface-{}", iface_id));
        let holder = Arc::new(IfaceTreeHolder::new(child));
        self.live_ifaces.insert(iface_id, holder.clone());
        holder
    }

    /// Mark |iface_id| as dead. If this causes the number of dead nodes to exceed
    /// the maximum allowed number of dead nodes, remove the oldest dead node.
    ///
    /// This method is a no-op if |iface_id| is not tracked by `IfaceManager`
    pub fn notify_iface_removed(&mut self, iface_id: IfaceId) {
        if let Some(iface_tree) = self.live_ifaces.remove(&iface_id) {
            self.dead_ifaces.push_back(iface_tree);
            if self.dead_ifaces.len() > self.max_dead_nodes {
                self.dead_ifaces.pop_front();
            }
        }
    }
}

pub struct IfaceTreeHolder {
    pub node: Node,
    subtree: Mutex<Option<Arc<dyn IfaceTree + Send + Sync>>>,
}

impl IfaceTreeHolder {
    pub fn new(node: Node) -> Self {
        Self { node, subtree: Mutex::new(None) }
    }

    pub fn place_iface_subtree(&self, subtree: Arc<dyn IfaceTree + Send + Sync>) {
        self.subtree.lock().replace(subtree);
    }
}

pub trait IfaceTree {}

#[cfg(test)]
mod tests {
    use super::*;

    use fuchsia_inspect::{
        assert_inspect_tree,
        vmo::{Inspector, StringProperty},
    };

    #[test]
    fn test_iface_manager_eviction() {
        let inspector = Inspector::new().unwrap();
        let mut ifaces_trees = IfacesTrees::new(2);

        ifaces_trees.create_iface_child(inspector.root(), 1);
        ifaces_trees.create_iface_child(inspector.root(), 2);
        ifaces_trees.create_iface_child(inspector.root(), 3);
        ifaces_trees.create_iface_child(inspector.root(), 4);
        assert_inspect_tree!(inspector, root: {
            "iface-1": {},
            "iface-2": {},
            "iface-3": {},
            "iface-4": {},
        });

        // If threshold is not exceeded, `notify_iface_removed` should not cause removal yet
        ifaces_trees.notify_iface_removed(2);
        assert_inspect_tree!(inspector, root: {
            "iface-1": {},
            "iface-2": {},
            "iface-3": {},
            "iface-4": {},
        });
        ifaces_trees.notify_iface_removed(3);
        assert_inspect_tree!(inspector, root: {
            "iface-1": {},
            "iface-2": {},
            "iface-3": {},
            "iface-4": {},
        });

        // Ifaces are removed now that number of dead nodes exceeds threshold
        ifaces_trees.notify_iface_removed(1);
        assert_inspect_tree!(inspector, root: { "iface-1": {}, "iface-3": {}, "iface-4": {} });
        ifaces_trees.notify_iface_removed(4);
        assert_inspect_tree!(inspector, root: { "iface-1": {}, "iface-4": {} });

        // Notify removal on nonexistent node should have no effect
        ifaces_trees.notify_iface_removed(5);
        assert_inspect_tree!(inspector, root: { "iface-1": {}, "iface-4": {} });
    }

    #[test]
    fn test_iface_tree_holder() {
        let inspector = Inspector::new().unwrap();
        let mut ifaces_trees = IfacesTrees::new(2);

        let holder = ifaces_trees.create_iface_child(inspector.root(), 1);
        let iface_subtree = Arc::new(TestIfaceSubtree {
            _apple: holder.node.create_string("apple", "yum"),
            _banana: holder.node.create_child("banana"),
        });
        holder.place_iface_subtree(iface_subtree);

        assert_inspect_tree!(inspector, root: {
            "iface-1": {
                apple: "yum",
                banana: {},
            }
        });
    }

    struct TestIfaceSubtree {
        _apple: StringProperty,
        _banana: Node,
    }
    impl IfaceTree for TestIfaceSubtree {}
}

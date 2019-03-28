// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::{HashSet, VecDeque};

use crate::{NodeExt, SharedNodePtr};

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
pub struct IfaceManager {
    /// Iface devices still in use
    live_ifaces: HashSet<IfaceId>,
    /// Iface devices that have been removed but whose debug infos are still kept in Inspect tree
    dead_ifaces: VecDeque<IfaceId>,
    max_dead_nodes: usize,
}

impl IfaceManager {
    pub fn new(max_dead_nodes: usize) -> Self {
        Self { live_ifaces: HashSet::new(), dead_ifaces: VecDeque::new(), max_dead_nodes }
    }

    /// Create iface node as child of |node| and return the iface child.
    pub fn create_iface_child(&mut self, node: SharedNodePtr, iface_id: IfaceId) -> SharedNodePtr {
        self.live_ifaces.insert(iface_id);
        node.lock().create_child(&format!("iface-{}", iface_id))
    }

    /// Mark |iface_id| as dead. If this causes the number of dead nodes to exceed
    /// the maximum number of dead nodes, remove the oldest dead node from |node|.
    ///
    /// This method is a no-op if |iface_id| is not tracked by `IfaceManager`
    pub fn notify_iface_removed(&mut self, node: SharedNodePtr, iface_id: IfaceId) {
        if self.live_ifaces.remove(&iface_id) {
            self.dead_ifaces.push_back(iface_id);
            if self.dead_ifaces.len() > self.max_dead_nodes {
                if let Some(stale_iface_id) = self.dead_ifaces.pop_front() {
                    node.lock().remove_child(&format!("iface-{}", stale_iface_id));
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use fuchsia_inspect as finspect;

    #[test]
    fn test_iface_manager() {
        let mut iface_mgr = IfaceManager::new(2);
        let node = finspect::ObjectTreeNode::new_root();

        iface_mgr.create_iface_child(node.clone(), 1);
        iface_mgr.create_iface_child(node.clone(), 2);
        iface_mgr.create_iface_child(node.clone(), 3);
        iface_mgr.create_iface_child(node.clone(), 4);
        verify_iface_children(&node, vec!["iface-1", "iface-2", "iface-3", "iface-4"]);

        // If threshold is not exceeded, `notify_iface_removed` should not cause removal yet
        iface_mgr.notify_iface_removed(node.clone(), 2);
        verify_iface_children(&node, vec!["iface-1", "iface-2", "iface-3", "iface-4"]);
        iface_mgr.notify_iface_removed(node.clone(), 3);
        verify_iface_children(&node, vec!["iface-1", "iface-2", "iface-3", "iface-4"]);

        // Ifaces are removed now that number of dead nodes exceeds threshold
        iface_mgr.notify_iface_removed(node.clone(), 1);
        verify_iface_children(&node, vec!["iface-1", "iface-3", "iface-4"]);
        iface_mgr.notify_iface_removed(node.clone(), 4);
        verify_iface_children(&node, vec!["iface-1", "iface-4"]);

        // Notify removal on nonexistent node should have no effect
        iface_mgr.notify_iface_removed(node.clone(), 5);
        verify_iface_children(&node, vec!["iface-1", "iface-4"]);
    }

    fn verify_iface_children(node: &SharedNodePtr, expected_children: Vec<&str>) {
        let iface_children = node.lock().get_children_names();
        let mut iface_children: Vec<String> =
            iface_children.into_iter().filter(|name| name.starts_with("iface-")).collect();
        iface_children.sort();
        assert_eq!(iface_children, expected_children);
    }
}

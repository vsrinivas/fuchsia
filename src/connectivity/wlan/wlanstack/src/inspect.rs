// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_inspect::{Inspector, NumericProperty, Property, UintProperty},
    fuchsia_inspect_contrib::{
        auto_persist::{self, AutoPersist},
        nodes::BoundedListNode,
    },
    parking_lot::Mutex,
    rand,
    std::{collections::HashSet, sync::Arc},
    wlan_common::hasher::WlanHasher,
    wlan_inspect::{IfaceTreeHolder, IfacesTrees},
};

pub const VMO_SIZE_BYTES: usize = 1000 * 1024;
const MAX_DEAD_IFACE_NODES: usize = 2;

/// Limit was chosen arbitrary. 20 events seem enough to log multiple PHY/iface create or
/// destroy events.
const DEVICE_EVENTS_LIMIT: usize = 20;

pub struct WlanstackTree {
    /// Root of the tree
    pub inspector: Inspector,
    /// Key used to hash privacy-sensitive values in the tree
    pub hasher: WlanHasher,
    /// "device_events" subtree
    pub device_events: Mutex<AutoPersist<BoundedListNode>>,
    /// "iface-<n>" subtrees, where n is the iface ID.
    ifaces_trees: Mutex<IfacesTrees>,
    /// "active_iface" property, what's the currently active iface. This assumes only one iface
    /// is active at a time
    latest_active_client_iface: Mutex<Option<UintProperty>>,

    // Keep track of removed ifaces. Not an Inspect node/property.
    removed_ifaces: Arc<Mutex<HashSet<u16>>>,
}

impl WlanstackTree {
    pub fn new(
        inspector: Inspector,
        persistence_req_sender: auto_persist::PersistenceReqSender,
    ) -> Self {
        let device_events = inspector.root().create_child("device_events");
        let device_events = AutoPersist::new(
            BoundedListNode::new(device_events, DEVICE_EVENTS_LIMIT),
            "wlanstack-device-events",
            persistence_req_sender.clone(),
        );
        let ifaces_trees = IfacesTrees::new(MAX_DEAD_IFACE_NODES);
        Self {
            inspector,
            // According to doc, `rand::random` uses ThreadRng, which is cryptographically secure:
            // https://docs.rs/rand/0.5.0/rand/rngs/struct.ThreadRng.html
            hasher: WlanHasher::new(rand::random::<u64>().to_le_bytes()),
            device_events: Mutex::new(device_events),
            ifaces_trees: Mutex::new(ifaces_trees),
            latest_active_client_iface: Mutex::new(None),
            removed_ifaces: Arc::new(Mutex::new(HashSet::new())),
        }
    }

    pub fn create_iface_child(&self, iface_id: u16) -> Arc<IfaceTreeHolder> {
        self.removed_ifaces.lock().remove(&iface_id);
        self.ifaces_trees.lock().create_iface_child(self.inspector.root(), iface_id)
    }

    pub fn notify_iface_removed(&self, iface_id: u16) {
        self.ifaces_trees.lock().notify_iface_removed(iface_id);
        self.removed_ifaces.lock().insert(iface_id);
    }

    pub fn mark_active_client_iface(&self, iface_id: u16) {
        self.latest_active_client_iface
            .lock()
            .get_or_insert_with(|| {
                self.inspector.root().create_uint("latest_active_client_iface", iface_id as u64)
            })
            .set(iface_id as u64);
    }

    pub fn unmark_active_client_iface(&self, iface_id: u16) {
        let mut active_iface = self.latest_active_client_iface.lock();
        if let Some(property) = active_iface.as_ref() {
            if let Ok(id) = property.get() {
                if id == iface_id as u64 {
                    active_iface.take();
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use {super::*, crate::test_helper, wlan_common::assert_variant};

    #[test]
    fn test_mark_unmark_active_client_iface_simple() {
        let (inspect_tree, _persistence_stream) = test_helper::fake_inspect_tree();

        inspect_tree.mark_active_client_iface(0);
        assert_active_client_iface_eq(&inspect_tree, 0);
        inspect_tree.unmark_active_client_iface(0);
        assert_variant!(inspect_tree.latest_active_client_iface.lock().as_ref(), None);
    }

    #[test]
    fn test_mark_unmark_active_client_iface_interleave() {
        let (inspect_tree, _persistence_stream) = test_helper::fake_inspect_tree();

        inspect_tree.mark_active_client_iface(0);
        assert_active_client_iface_eq(&inspect_tree, 0);
        // We don't support two concurrent active client iface in practice. This test is
        // just us being paranoid about the unmark call coming later than the call to
        // mark the new iface.
        inspect_tree.mark_active_client_iface(1);
        assert_active_client_iface_eq(&inspect_tree, 1);

        // Stale unmark call should have no effect on the tree
        inspect_tree.unmark_active_client_iface(0);
        assert_active_client_iface_eq(&inspect_tree, 1);

        inspect_tree.unmark_active_client_iface(1);
        assert_variant!(inspect_tree.latest_active_client_iface.lock().as_ref(), None);
    }

    fn assert_active_client_iface_eq(inspect_tree: &WlanstackTree, iface_id: u64) {
        assert_variant!(inspect_tree.latest_active_client_iface.lock().as_ref(), Some(property) => {
            assert_variant!(property.get(), Ok(id) => {
                assert_eq!(id, iface_id);
            });
        });
    }
}

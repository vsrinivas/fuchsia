// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_inspect::Inspector,
    fuchsia_inspect_contrib::nodes::BoundedListNode,
    parking_lot::Mutex,
    rand,
    std::sync::Arc,
    wlan_inspect::iface_mgr::{IfaceTreeHolder, IfacesTrees},
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
    pub hash_key: [u8; 8],
    /// "device_events" subtree
    pub device_events: Mutex<BoundedListNode>,
    /// "iface-<n>" subtrees, where n is the iface ID.
    ifaces_trees: Mutex<IfacesTrees>,
}

impl WlanstackTree {
    pub fn new(inspector: Inspector) -> Self {
        let device_events = inspector.root().create_child("device_events");
        let ifaces_trees = IfacesTrees::new(MAX_DEAD_IFACE_NODES);
        Self {
            inspector,
            // According to doc, `rand::random` uses ThreadRng, which is cryptographically secure:
            // https://docs.rs/rand/0.5.0/rand/rngs/struct.ThreadRng.html
            hash_key: rand::random::<u64>().to_le_bytes(),
            device_events: Mutex::new(BoundedListNode::new(device_events, DEVICE_EVENTS_LIMIT)),
            ifaces_trees: Mutex::new(ifaces_trees),
        }
    }

    pub fn create_iface_child(&self, iface_id: u16) -> Arc<IfaceTreeHolder> {
        self.ifaces_trees.lock().create_iface_child(self.inspector.root(), iface_id)
    }

    pub fn notify_iface_removed(&self, iface_id: u16) {
        self.ifaces_trees.lock().notify_iface_removed(iface_id)
    }
}

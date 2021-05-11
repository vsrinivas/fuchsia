// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_inspect::Inspector, fuchsia_inspect_contrib::nodes::BoundedListNode, parking_lot::Mutex,
};

pub const VMO_SIZE_BYTES: usize = 1000 * 1024;

/// Limit was chosen arbitrary. 20 events seem enough to log multiple PHY/iface create or
/// destroy events.
const DEVICE_EVENTS_LIMIT: usize = 20;

pub struct WlanMonitorTree {
    /// Root of the tree
    pub inspector: Inspector,
    /// "device_events" subtree
    pub device_events: Mutex<BoundedListNode>,
}

impl WlanMonitorTree {
    pub fn new(inspector: Inspector) -> Self {
        let device_events = inspector.root().create_child("device_events");
        Self {
            inspector,
            device_events: Mutex::new(BoundedListNode::new(device_events, DEVICE_EVENTS_LIMIT)),
        }
    }
}

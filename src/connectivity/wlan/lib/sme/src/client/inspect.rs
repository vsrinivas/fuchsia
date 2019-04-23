// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use wlan_inspect::nodes::{BoundedListNode, NodeExt, SharedNodePtr};

/// These limits are set to capture roughly 5 to 10 recent connection attempts. An average
/// successful connection attempt would generate about 5 state events and 7 supplicant events (this
/// number may be different in error cases).
const STATE_EVENTS_LIMIT: usize = 50;
const RSN_EVENTS_LIMIT: usize = 50;

/// Limit set to capture roughly join scans for 10 recent connection attempts.
const JOIN_SCAN_EVENTS_LIMIT: usize = 10;

/// Wrapper struct SME inspection nodes
pub struct SmeNode {
    /// Inspection node to log recent state transitions, or cases where an event would that would
    /// normally cause a state transition doesn't due to an error.
    pub states: BoundedListNode,
    /// Inspection node to log EAPOL frames processed by supplicant and its output.
    pub rsn_events: BoundedListNode,
    /// Inspection node to log recent join scan results.
    pub join_scan_events: BoundedListNode,
}

impl SmeNode {
    pub fn new(node: SharedNodePtr) -> Self {
        let mut node = node.lock();
        let states = BoundedListNode::new(node.create_child("states"), STATE_EVENTS_LIMIT);
        let rsn_events = BoundedListNode::new(node.create_child("rsn_events"), RSN_EVENTS_LIMIT);
        let join_scan_events =
            BoundedListNode::new(node.create_child("join_scan_events"), JOIN_SCAN_EVENTS_LIMIT);
        Self { states, rsn_events, join_scan_events }
    }
}

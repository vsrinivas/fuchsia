// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_inspect::vmo::Node;
use fuchsia_inspect_contrib::nodes::BoundedListNode;
use parking_lot::Mutex;
use wlan_inspect::iface_mgr::IfaceTree;

/// These limits are set to capture roughly 5 to 10 recent connection attempts. An average
/// successful connection attempt would generate about 5 state events and 7 supplicant events (this
/// number may be different in error cases).
const STATE_EVENTS_LIMIT: usize = 50;
const RSN_EVENTS_LIMIT: usize = 50;

/// Limit set to capture roughly join scans for 10 recent connection attempts.
const JOIN_SCAN_EVENTS_LIMIT: usize = 10;

/// Wrapper struct SME inspection nodes
pub struct SmeTree {
    /// Inspection node to log recent state transitions, or cases where an event would that would
    /// normally cause a state transition doesn't due to an error.
    pub states: Mutex<BoundedListNode>,
    /// Inspection node to log EAPOL frames processed by supplicant and its output.
    pub rsn_events: Mutex<BoundedListNode>,
    /// Inspection node to log recent join scan results.
    pub join_scan_events: Mutex<BoundedListNode>,
}

impl SmeTree {
    pub fn new(node: &Node) -> Self {
        let states = BoundedListNode::new(node.create_child("states"), STATE_EVENTS_LIMIT);
        let rsn_events = BoundedListNode::new(node.create_child("rsn_events"), RSN_EVENTS_LIMIT);
        let join_scan_events =
            BoundedListNode::new(node.create_child("join_scan_events"), JOIN_SCAN_EVENTS_LIMIT);
        Self {
            states: Mutex::new(states),
            rsn_events: Mutex::new(rsn_events),
            join_scan_events: Mutex::new(join_scan_events),
        }
    }
}

impl IfaceTree for SmeTree {}

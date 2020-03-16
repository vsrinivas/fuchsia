// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        client::{bss::BssInfo, Status as SmeStatus},
        Ssid,
    },
    fuchsia_inspect::{IntProperty, Node, Property, StringProperty, UintProperty},
    fuchsia_inspect_contrib::nodes::{BoundedListNode, NodeExt, TimeProperty},
    fuchsia_zircon as zx,
    parking_lot::Mutex,
    wlan_common::format::MacFmt,
    wlan_inspect::iface_mgr::IfaceTree,
};

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
    pub state_events: Mutex<BoundedListNode>,
    /// Inspection node to log EAPOL frames processed by supplicant and its output.
    pub rsn_events: Mutex<BoundedListNode>,
    /// Inspection node to log recent join scan results.
    pub join_scan_events: Mutex<BoundedListNode>,
    /// Inspect node to log periodic pulse check. For the most part, information logged in this
    /// node can be derived from (and is therefore redundant with) `state_events` node. This
    /// is logged  for two reasons:
    /// 1. To show a quick summary of latest status.
    /// 2. To show how up-to-date the latest status is (although pulse is logged within SME, it can
    ///    be thought similarly to an external entity periodically checking SME's status).
    pub last_pulse: Mutex<PulseNode>,
}

impl SmeTree {
    pub fn new(node: &Node) -> Self {
        let state_events =
            BoundedListNode::new(node.create_child("state_events"), STATE_EVENTS_LIMIT);
        let rsn_events = BoundedListNode::new(node.create_child("rsn_events"), RSN_EVENTS_LIMIT);
        let join_scan_events =
            BoundedListNode::new(node.create_child("join_scan_events"), JOIN_SCAN_EVENTS_LIMIT);
        let pulse = PulseNode::new(node.create_child("last_pulse"));
        Self {
            state_events: Mutex::new(state_events),
            rsn_events: Mutex::new(rsn_events),
            join_scan_events: Mutex::new(join_scan_events),
            last_pulse: Mutex::new(pulse),
        }
    }
}

impl IfaceTree for SmeTree {}

pub struct PulseNode {
    node: Node,
    _started: TimeProperty,
    last_updated: TimeProperty,
    last_link_up: Option<TimeProperty>,
    status: Option<StatusNode>,

    // Not part of Inspect node. We use it to compare new status against existing status
    last_status: Option<SmeStatus>,
}

impl PulseNode {
    fn new(node: Node) -> Self {
        let now = zx::Time::get(zx::ClockId::Monotonic);
        let started = node.create_time_at("started", now);
        let last_updated = node.create_time_at("last_updated", now);
        Self {
            node,
            _started: started,
            last_updated,
            last_link_up: None,
            status: None,
            last_status: None,
        }
    }

    pub fn update(&mut self, new_status: SmeStatus) {
        let now = zx::Time::get(zx::ClockId::Monotonic);
        self.last_updated.set_at(now);

        // This method is always called when there's a state transition, so even if the client is
        // no longer connected now, if the client was previously connected, we can conclude
        // that they were connected until now.
        let previously_connected =
            self.last_status.as_ref().map(|s| s.connected_to.is_some()).unwrap_or(false);
        if new_status.connected_to.is_some() || previously_connected {
            match &self.last_link_up {
                Some(last_link_up) => last_link_up.set_at(now),
                None => self.last_link_up = Some(self.node.create_time_at("last_link_up", now)),
            }
        }

        let old_status = self.last_status.replace(new_status);
        if old_status != self.last_status {
            // Safe to unwrap because value was inserted two lines above
            let new_status = self.last_status.as_ref().unwrap();
            match self.status.as_mut() {
                Some(status_node) => status_node.update(new_status),
                None => {
                    self.status =
                        Some(StatusNode::new(self.node.create_child("status"), new_status))
                }
            }
        }
    }
}

pub struct StatusNode {
    node: Node,
    status_str: StringProperty,
    connected_to: Option<BssInfoNode>,
    connecting_to: Option<StringProperty>,
}

impl StatusNode {
    fn new(node: Node, status: &SmeStatus) -> Self {
        let status_str = node.create_string("status_str", "idle");
        let mut status_node = Self { node, status_str, connected_to: None, connecting_to: None };
        status_node.update(status);
        status_node
    }

    pub fn update(&mut self, new_status: &SmeStatus) {
        let status_str = if new_status.connected_to.is_some() {
            "connected"
        } else if new_status.connecting_to.is_some() {
            "connecting"
        } else {
            "idle"
        };
        self.status_str.set(status_str);

        match &new_status.connected_to {
            Some(bss_info) => match self.connected_to.as_mut() {
                Some(connected_to) => connected_to.update(bss_info),
                None => {
                    self.connected_to =
                        Some(BssInfoNode::new(self.node.create_child("connected_to"), bss_info));
                }
            },
            None => {
                self.connected_to.take();
            }
        }
        match &new_status.connecting_to {
            Some(ssid) => {
                let ssid = &ssid_inspect_str(ssid);
                match self.connecting_to.as_mut() {
                    Some(connecting_to) => connecting_to.set(ssid),
                    None => {
                        self.connecting_to = Some(self.node.create_string("connecting_to", ssid))
                    }
                }
            }
            None => {
                self.connecting_to.take();
            }
        }
    }
}

pub struct BssInfoNode {
    _node: Node,
    bssid: StringProperty,
    ssid: StringProperty,
    rx_dbm: IntProperty,
    snr_db: IntProperty,
    channel: UintProperty,
    protection: StringProperty,
}

impl BssInfoNode {
    fn new(node: Node, bss_info: &BssInfo) -> Self {
        let bssid = node.create_string("bssid", bss_info.bssid.to_mac_str());
        let ssid = node.create_string("ssid", ssid_inspect_str(&bss_info.ssid));
        let rx_dbm = node.create_int("rx_dbm", bss_info.rx_dbm as i64);
        let snr_db = node.create_int("snr_db", bss_info.snr_db as i64);
        let channel = node.create_uint("channel", bss_info.channel as u64);
        let protection = node.create_string("protection", format!("{}", bss_info.protection));
        Self { _node: node, bssid, ssid, rx_dbm, snr_db, channel, protection }
    }

    fn update(&mut self, bss_info: &BssInfo) {
        self.bssid.set(&bss_info.bssid.to_mac_str());
        self.ssid.set(&ssid_inspect_str(&bss_info.ssid));
        self.rx_dbm.set(bss_info.rx_dbm as i64);
        self.snr_db.set(bss_info.snr_db as i64);
        self.channel.set(bss_info.channel as u64);
        self.protection.set(&format!("{}", bss_info.protection));
    }
}

fn ssid_inspect_str(ssid: &Ssid) -> String {
    match std::str::from_utf8(ssid) {
        Ok(ssid) => ssid.to_string(),
        Err(_) => format!("{:?}", ssid),
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::client::test_utils,
        fuchsia_inspect::{assert_inspect_tree, testing::AnyProperty, Inspector},
    };

    #[test]
    fn test_inspect_update_pulse() {
        let inspector = Inspector::new();
        let root = inspector.root();
        let mut pulse = PulseNode::new(root.create_child("last_pulse"));

        // SME is idle. Pulse node should not have any field except "last_updated" and "status"
        let status = SmeStatus { connected_to: None, connecting_to: None };
        pulse.update(status);
        assert_inspect_tree!(inspector, root: {
            last_pulse: {
                started: AnyProperty,
                last_updated: AnyProperty,
                status: { status_str: "idle" }
            }
        });

        // SME is connecting. Check that "connecting_to" field now appears, and that existing
        // fields are still kept.
        let status = SmeStatus { connected_to: None, connecting_to: Some(b"foo".to_vec()) };
        pulse.update(status);
        assert_inspect_tree!(inspector, root: {
            last_pulse: {
                started: AnyProperty,
                last_updated: AnyProperty,
                status: { status_str: "connecting", connecting_to: "foo" },
            }
        });

        // SME is connected. Aside from verifying that existing fields are kept, key things we
        // want to check are that "last_link_up" and "connected_to" are populated, and
        // "connecting_to" is cleared out.
        let status =
            SmeStatus { connected_to: Some(test_utils::fake_bss_info()), connecting_to: None };
        pulse.update(status);
        assert_inspect_tree!(inspector, root: {
            last_pulse: {
                started: AnyProperty,
                last_updated: AnyProperty,
                last_link_up: AnyProperty,
                status: {
                    status_str: "connected",
                    connected_to: contains { ssid: "foo" }
                },
            }
        });

        // SME is idle. The "connected_to" field is cleared out.
        let status = SmeStatus { connected_to: None, connecting_to: None };
        pulse.update(status);
        assert_inspect_tree!(inspector, root: {
            last_pulse: {
                started: AnyProperty,
                last_updated: AnyProperty,
                last_link_up: AnyProperty,
                status: { status_str: "idle" },
            }
        });
    }
}

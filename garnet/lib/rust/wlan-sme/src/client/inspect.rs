// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_inspect as fidl_inspect;
use std::fmt::Display;
use wlan_inspect::nodes::{BoundedListNode, NodeExt, SharedNodePtr};
use wlan_rsn::rsna::SecAssocUpdate;

/// These limits are set to capture roughly 5 to 10 recent connection attempts. An average
/// successful connection attempt would generate about 4 state events and 7 supplicant events (this
/// number may be different in error cases).
const STATE_EVENTS_LIMIT: usize = 40;
const SUPPLICANT_EVENTS_LIMIT: usize = 50;

/// Wrapper struct SME inspection nodes
pub struct SmeNode {
    /// Inspection node to log recent state transitions, or cases where an event would that would
    /// normally cause a state transition doesn't due to an error.
    states: BoundedListNode,
    /// Inspection node to log EAPOL frames processed by supplicant and its output.
    supplicant_events: BoundedListNode,
}

impl SmeNode {
    pub fn new(node: SharedNodePtr) -> Self {
        let mut node = node.lock();
        let states = BoundedListNode::new(node.create_child("states"), STATE_EVENTS_LIMIT);
        let supplicant_events =
            BoundedListNode::new(node.create_child("supplicant_events"), SUPPLICANT_EVENTS_LIMIT);
        Self { states, supplicant_events }
    }

    pub fn log_state_change<F, T, C>(&mut self, from: F, to: T, cause: Option<C>)
    where
        F: Display,
        T: Display,
        C: Display,
    {
        let msg = match cause {
            None => format!("{} -> {}", from, to),
            Some(cause) => format!("{} -> {} (why={})", from, to, cause),
        };
        let node = self.states.request_entry();
        node.lock().set_time().insert_str("msg", msg);
    }

    pub fn log_eapol_frame(
        &mut self,
        eapol_pdu: Vec<u8>,
        transmit_direction: TransmitDirection,
    ) -> SharedNodePtr {
        let key = match transmit_direction {
            TransmitDirection::Rx => "rx_eapol_frame",
            TransmitDirection::Tx => "tx_eapol_frame",
        };
        let node = self.supplicant_events.request_entry();
        node.lock().set_time().add_property(fidl_inspect::Property {
            key: key.to_string(),
            value: fidl_inspect::PropertyValue::Bytes(eapol_pdu),
        });
        node
    }

    pub fn log_supplicant_update(&mut self, update: &SecAssocUpdate) {
        match update {
            SecAssocUpdate::TxEapolKeyFrame(_) => (), // log separately
            SecAssocUpdate::Key(key) => {
                let node = self.supplicant_events.request_entry();
                node.lock().set_time().insert_str("derived_key", key.name());
            }
            SecAssocUpdate::Status(status) => {
                let node = self.supplicant_events.request_entry();
                node.lock().set_time().insert_debug("rsna_status", status);
            }
        }
    }
}

pub enum TransmitDirection {
    Rx,
    Tx,
}

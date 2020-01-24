// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::switchboard::base::{SettingAction, SettingEvent};
use futures::channel::mpsc::UnboundedReceiver;
use futures::channel::mpsc::UnboundedSender;
use futures::lock::Mutex;
use std::sync::Arc;

pub type ConduitHandle = Arc<Mutex<dyn Conduit + Send + Sync>>;

pub type ConduitReceiver = UnboundedReceiver<ConduitData>;
pub type NodeId = i32;

// The types of data that can be sent through the Conduit.
pub enum ConduitData {
    Action(SettingAction),
    Event(SettingEvent),
}

/// ConduitSender pairs ConduitData with a NodeId, an identifier used
/// by Conduit to direct data accordingly.
#[derive(Clone)]
pub struct ConduitSender {
    node_id: NodeId,
    sender: UnboundedSender<(NodeId, ConduitData)>,
}

impl ConduitSender {
    pub fn new(node_id: NodeId, sender: UnboundedSender<(NodeId, ConduitData)>) -> ConduitSender {
        return ConduitSender { node_id: node_id, sender: sender };
    }

    pub fn send(&self, data: ConduitData) {
        self.sender.unbounded_send((self.node_id, data)).ok();
    }
}

/// The Conduit trait generates waypoints in a conduit.
pub trait Conduit {
    /// Generates a waypoint within the conduit, returning the
    /// corresponding sender and receiver. It will receive actions from and send
    /// events to previously created waypoint.
    fn create_waypoint(&mut self) -> (ConduitSender, ConduitReceiver);
}

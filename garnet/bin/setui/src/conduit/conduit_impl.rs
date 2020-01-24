// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::conduit::base::{Conduit, ConduitData, ConduitReceiver, ConduitSender, NodeId};
use fuchsia_async as fasync;
use futures::channel::mpsc::UnboundedSender;
use futures::lock::Mutex;
use futures::StreamExt;
use std::sync::Arc;

/// Internal struct for correlating node id to sender.
struct Node {
    id: NodeId,
    sender: UnboundedSender<ConduitData>,
}

impl Node {
    pub fn send(&self, data: ConduitData) {
        self.sender.unbounded_send(data).ok();
    }
}

pub struct ConduitImpl {
    // The waypoints to send data to.
    nodes: Vec<Node>,
    // Sender used in minting new ConduitSenders.
    data_tx: UnboundedSender<(NodeId, ConduitData)>,
    // The next node id to issue.
    next_node_id: NodeId,
}

impl ConduitImpl {
    pub fn create() -> Arc<Mutex<ConduitImpl>> {
        let (data_tx, mut data_rx) = futures::channel::mpsc::unbounded::<(NodeId, ConduitData)>();

        let conduit =
            Arc::new(Mutex::new(ConduitImpl { nodes: vec![], data_tx: data_tx, next_node_id: 0 }));

        let conduit_clone = conduit.clone();

        fasync::spawn(async move {
            while let Some((node_id, conduit_data)) = data_rx.next().await {
                conduit_clone.lock().await.process_data(node_id, conduit_data);
            }
        });

        return conduit;
    }

    fn process_data(&mut self, node_id: NodeId, data: ConduitData) {
        // Find index of node
        if let Some(mut index) = self.nodes.iter().position(|x| x.id == node_id) {
            match &data {
                ConduitData::Event(_event) => {
                    if index == 0 {
                        return;
                    }
                    // Events are always returned upstream.
                    index = index - 1;
                }
                ConduitData::Action(_action) => {
                    if index == self.nodes.len() - 1 {
                        return;
                    }
                    // Actions are sent downstream.
                    index = index + 1;
                }
            }

            self.nodes[index].send(data);
        }
    }
}

impl Conduit for ConduitImpl {
    fn create_waypoint(&mut self) -> (ConduitSender, ConduitReceiver) {
        let (data_tx, data_rx) = futures::channel::mpsc::unbounded::<ConduitData>();
        let node_id = self.next_node_id;
        self.next_node_id = self.next_node_id + 1;

        let node = Node { id: node_id, sender: data_tx };

        self.nodes.push(node);
        let sender = ConduitSender::new(node_id, self.data_tx.clone());

        return (sender, data_rx);
    }
}

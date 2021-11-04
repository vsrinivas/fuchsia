// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_bluetooth_hfp::NetworkInformation;
use fuchsia_bluetooth::types::PeerId;
use fuchsia_inspect::{self as inspect, Property};
use fuchsia_inspect_derive::{AttachError, Inspect};
use lazy_static::lazy_static;

lazy_static! {
    static ref PEER_ID: inspect::StringReference<'static> = "peer_id".into();
}

#[derive(Default, Debug)]
pub struct HfpInspect {
    pub autoconnect: inspect::BoolProperty,
    // The inspect node for connected peers.
    pub peers: inspect::Node,
    inspect_node: inspect::Node,
}

impl Inspect for &mut HfpInspect {
    fn iattach(self, parent: &inspect::Node, name: impl AsRef<str>) -> Result<(), AttachError> {
        self.inspect_node = parent.create_child(name.as_ref());
        self.autoconnect = self.inspect_node.create_bool("autoconnect", false);
        self.peers = self.inspect_node.create_child("peers");
        Ok(())
    }
}

impl HfpInspect {
    pub fn node(&self) -> &inspect::Node {
        &self.inspect_node
    }
}

#[derive(Default, Debug, Inspect)]
pub struct CallManagerInspect {
    manager_connection_id: inspect::UintProperty,
    connected: inspect::BoolProperty,
    inspect_node: inspect::Node,
}

impl CallManagerInspect {
    pub fn new_connection(&mut self, id: usize) {
        self.connected.set(true);
        self.manager_connection_id.set(id as u64);
    }

    pub fn set_disconnected(&mut self) {
        self.connected.set(false);
    }
}

#[derive(Default, Debug, Inspect)]
pub struct NetworkInformationInspect {
    service_available: inspect::BoolProperty,
    signal_strength: inspect::StringProperty,
    roaming: inspect::BoolProperty,
    inspect_node: inspect::Node,
}

impl NetworkInformationInspect {
    pub fn update(&mut self, info: &NetworkInformation) {
        self.service_available.set(info.service_available.unwrap_or(false));
        let signal = info.signal_strength.map_or("".to_string(), |s| format!("{:?}", s));
        self.signal_strength.set(&signal);
        self.roaming.set(info.roaming.unwrap_or(false));
    }
}

#[derive(Debug)]
pub struct PeerTaskInspect {
    /// The Bluetooth identifier assigned to the peer.
    peer_id: PeerId,
    pub connected_peer_handler: inspect::BoolProperty,
    pub network: NetworkInformationInspect,
    inspect_node: inspect::Node,
}

impl Inspect for &mut PeerTaskInspect {
    fn iattach(self, parent: &inspect::Node, name: impl AsRef<str>) -> Result<(), AttachError> {
        self.inspect_node = parent.create_child(name.as_ref());
        self.inspect_node.record_string(&*PEER_ID, self.peer_id.to_string());
        self.connected_peer_handler =
            self.inspect_node.create_bool("connected_peer_handler", false);
        self.network.iattach(&self.inspect_node, "network")?;
        Ok(())
    }
}

impl PeerTaskInspect {
    pub fn new(peer_id: PeerId) -> Self {
        Self {
            peer_id,
            connected_peer_handler: Default::default(),
            network: Default::default(),
            inspect_node: Default::default(),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl_fuchsia_bluetooth_hfp::SignalStrength;
    use fuchsia_inspect::{assert_data_tree, testing::AnyProperty};
    use fuchsia_inspect_derive::WithInspect;

    #[test]
    fn peer_task_inspect_tree() {
        let inspect = inspect::Inspector::new();

        let id = PeerId(2);
        let mut peer_task = PeerTaskInspect::new(id).with_inspect(inspect.root(), "peer").unwrap();

        // Default inspect tree.
        assert_data_tree!(inspect, root: {
            peer: {
                peer_id: AnyProperty,
                connected_peer_handler: false,
                network: {
                    service_available: false,
                    signal_strength: "",
                    roaming: false,
                },
            }
        });

        let network = NetworkInformation {
            service_available: Some(true),
            signal_strength: Some(SignalStrength::Low),
            ..NetworkInformation::EMPTY
        };
        peer_task.connected_peer_handler.set(true);
        peer_task.network.update(&network);
        assert_data_tree!(inspect, root: {
            peer: {
                peer_id: AnyProperty,
                connected_peer_handler: true,
                network: {
                    service_available: true,
                    signal_strength: "Low",
                    roaming: false,
                }
            }
        });
    }
}

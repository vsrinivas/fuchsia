// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_async as fasync,
    fuchsia_bluetooth::types::PeerId,
    fuchsia_inspect as inspect,
    fuchsia_inspect_contrib::{
        inspect_log,
        nodes::{BoundedListNode, NodeExt, TimeProperty},
    },
    fuchsia_inspect_derive::{AttachError, Inspect},
};

use crate::{metrics::MetricsNode, profile::AvrcpService};

/// The maximum number of feature sets we store for a remote peer.
/// This is useful in the case of peer disconnecting/reconnecting, as we will
/// save the last `MAX_FEATURE_SETS` feature sets.
const MAX_FEATURE_SETS: usize = 3;

pub struct RemotePeerInspect {
    peer_id: PeerId,
    // Information about this peer's target and controller services.
    target_info: Option<BoundedListNode>,
    controller_info: Option<BoundedListNode>,
    /// The last known connected time.
    last_connected: Option<TimeProperty>,
    /// The shared node used to record cumulative metrics about any discovered peer.
    metrics_node: MetricsNode,
    inspect_node: inspect::Node,
}

impl Inspect for &mut RemotePeerInspect {
    fn iattach(self, parent: &inspect::Node, name: impl AsRef<str>) -> Result<(), AttachError> {
        self.inspect_node = parent.create_child(name.as_ref());
        self.inspect_node.record_string("peer_id", self.peer_id.to_string());
        self.target_info = Some(BoundedListNode::new(
            self.inspect_node.create_child("target_info"),
            MAX_FEATURE_SETS,
        ));
        self.controller_info = Some(BoundedListNode::new(
            self.inspect_node.create_child("controller_info"),
            MAX_FEATURE_SETS,
        ));
        Ok(())
    }
}

impl RemotePeerInspect {
    pub fn new(peer_id: PeerId) -> Self {
        Self {
            peer_id,
            target_info: None,
            controller_info: None,
            last_connected: None,
            metrics_node: MetricsNode::default(),
            inspect_node: inspect::Node::default(),
        }
    }

    pub fn node(&self) -> &inspect::Node {
        &self.inspect_node
    }

    /// Sets this peer's node for metrics collection.
    pub fn set_metrics_node(&mut self, metrics_node: MetricsNode) {
        self.metrics_node = metrics_node;
    }

    pub fn record_target_features(&mut self, service: AvrcpService) {
        if let AvrcpService::Target { features, protocol_version, .. } = service {
            if let Some(desc) = self.target_info.as_mut() {
                inspect_log!(
                    desc,
                    features: format!("{:?}", features),
                    version: format!("{:?}", protocol_version)
                );
            }
            self.metrics_node.target_features(self.peer_id, features);
        }
    }

    pub fn record_controller_features(&mut self, service: AvrcpService) {
        if let AvrcpService::Controller { features, protocol_version, .. } = service {
            if let Some(desc) = self.controller_info.as_mut() {
                inspect_log!(
                    desc,
                    features: format!("{:?}", features),
                    version: format!("{:?}", protocol_version)
                );
            }
            self.metrics_node.controller_features(self.peer_id, features);
        }
    }

    pub fn record_connected(&mut self, at: fasync::Time) {
        if let Some(prop) = &self.last_connected {
            prop.set_at(at.into());
        } else {
            self.last_connected =
                Some(self.inspect_node.create_time_at("last_connected", at.into()));
        }
        self.metrics_node.control_connection();
    }

    pub fn metrics(&self) -> &MetricsNode {
        &self.metrics_node
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::profile::{AvrcpControllerFeatures, AvrcpProtocolVersion, AvrcpTargetFeatures};

    use {
        fuchsia_inspect::{assert_data_tree, testing::AnyProperty},
        fuchsia_inspect_derive::WithInspect,
    };

    #[test]
    fn remote_peer_inspect_tree() {
        let inspect = inspect::Inspector::new();

        let mut peer_inspect =
            RemotePeerInspect::new(PeerId(1)).with_inspect(inspect.root(), "peer").unwrap();

        // Default inspect tree.
        assert_data_tree!(inspect, root: {
            peer: {
                peer_id: AnyProperty,
                controller_info: {},
                target_info: {},
            }
        });

        let target_info = AvrcpService::Target {
            features: AvrcpTargetFeatures::PLAYERSETTINGS,
            psm: 20,
            protocol_version: AvrcpProtocolVersion(1, 5),
        };
        let controller_info = AvrcpService::Controller {
            features: AvrcpControllerFeatures::CATEGORY1,
            psm: 10,
            protocol_version: AvrcpProtocolVersion(1, 6),
        };
        // Setting the opposite feature set has no effect.
        peer_inspect.record_target_features(controller_info);
        peer_inspect.record_controller_features(target_info);
        assert_data_tree!(inspect, root: {
            peer: {
                peer_id: AnyProperty,
                controller_info: {},
                target_info: {},
            }
        });

        peer_inspect.record_target_features(target_info);
        peer_inspect.record_controller_features(controller_info);
        assert_data_tree!(inspect, root: {
            peer: {
                peer_id: AnyProperty,
                controller_info: {
                    "0": { "@time": AnyProperty, features: "CATEGORY1", version: "1.6" },
                },
                target_info: {
                    "0": { "@time": AnyProperty, features: "PLAYERSETTINGS", version: "1.5" },
                },
            }
        });

        let time = fasync::Time::from_nanos(123_456_789);
        peer_inspect.record_connected(time);
        assert_data_tree!(inspect, root: {
            peer: {
                peer_id: AnyProperty,
                controller_info: contains {},
                target_info: contains {},
                last_connected: AnyProperty,
            }
        });
    }
}

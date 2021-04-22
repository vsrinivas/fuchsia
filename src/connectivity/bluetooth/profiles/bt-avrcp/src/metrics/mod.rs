// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_bluetooth::types::PeerId,
    fuchsia_inspect::{self as inspect, NumericProperty},
    fuchsia_inspect_derive::Inspect,
    parking_lot::Mutex,
    std::{collections::HashSet, sync::Arc},
};

use crate::profile::{AvrcpControllerFeatures, AvrcpTargetFeatures};

pub const METRICS_NODE_NAME: &str = "metrics";

/// Cumulative metrics node for the supported features of discovered peers.
#[derive(Default, Inspect)]
struct PeerSupportMetrics {
    target_peers_supporting_browsing: inspect::UintProperty,
    target_peers_supporting_cover_art: inspect::UintProperty,
    controller_peers_supporting_browsing: inspect::UintProperty,
    controller_peers_supporting_cover_art: inspect::UintProperty,

    /// Note: The distinct_* metrics are not holistic measures. Because these values are not
    /// persisted across reboots, they should not be used for more than informational purposes
    /// about a specific period of time.
    distinct_target_peers_supporting_browsing: inspect::UintProperty,
    distinct_target_peers_supporting_cover_art: inspect::UintProperty,
    distinct_controller_peers_supporting_browsing: inspect::UintProperty,
    distinct_controller_peers_supporting_cover_art: inspect::UintProperty,

    /// Internal collections to track uniqueness. These aren't included in the inspect
    /// representation.
    #[inspect(skip)]
    tg_browse_peers: HashSet<PeerId>,
    #[inspect(skip)]
    tg_cover_art_peers: HashSet<PeerId>,
    #[inspect(skip)]
    ct_browse_peers: HashSet<PeerId>,
    #[inspect(skip)]
    ct_cover_art_peers: HashSet<PeerId>,
}

#[derive(Default, Inspect)]
struct MetricsNodeInner {
    /// Total number of connection errors.
    connection_errors: inspect::UintProperty,

    /// Total number of control connections.
    control_connections: inspect::UintProperty,

    /// Total number of browse connections.
    browse_connections: inspect::UintProperty,

    /// Total number of unique peers discovered since the last reboot.
    distinct_peers: inspect::UintProperty,
    #[inspect(skip)]
    distinct_peers_set: HashSet<PeerId>,

    /// Total number of control channel connection collisions. Namely, when an inbound
    /// and outbound connection occur at roughly the same time.
    control_channel_collisions: inspect::UintProperty,

    /// Metrics for features supported by discovered peers.
    support_node: PeerSupportMetrics,

    /// The inspect node for this object.
    inspect_node: inspect::Node,
}

impl MetricsNodeInner {
    /// Checks if the `id` is a newly discovered peer and updates the metric count.
    fn check_distinct_peer(&mut self, id: PeerId) {
        if self.distinct_peers_set.insert(id) {
            self.distinct_peers.add(1);
        }
    }

    fn controller_supporting_browsing(&mut self, id: PeerId) {
        self.support_node.controller_peers_supporting_browsing.add(1);
        if self.support_node.ct_browse_peers.insert(id) {
            self.support_node.distinct_controller_peers_supporting_browsing.add(1);
        }
    }

    fn controller_supporting_cover_art(&mut self, id: PeerId) {
        self.support_node.controller_peers_supporting_cover_art.add(1);
        if self.support_node.ct_cover_art_peers.insert(id) {
            self.support_node.distinct_controller_peers_supporting_cover_art.add(1);
        }
    }

    fn target_supporting_browsing(&mut self, id: PeerId) {
        self.support_node.target_peers_supporting_browsing.add(1);
        if self.support_node.tg_browse_peers.insert(id) {
            self.support_node.distinct_target_peers_supporting_browsing.add(1);
        }
    }

    fn target_supporting_cover_art(&mut self, id: PeerId) {
        self.support_node.target_peers_supporting_cover_art.add(1);
        if self.support_node.tg_cover_art_peers.insert(id) {
            self.support_node.distinct_target_peers_supporting_cover_art.add(1);
        }
    }
}

/// An object, backed by inspect, used to track cumulative metrics for the AVRCP component.
#[derive(Clone, Default, Inspect)]
pub struct MetricsNode {
    #[inspect(forward)]
    inner: Arc<Mutex<MetricsNodeInner>>,
}

impl MetricsNode {
    pub fn new_peer(&self, id: PeerId) {
        self.inner.lock().check_distinct_peer(id);
    }

    pub fn connection_error(&self) {
        self.inner.lock().connection_errors.add(1);
    }

    pub fn control_connection(&self) {
        self.inner.lock().control_connections.add(1);
    }

    pub fn browse_connection(&self) {
        self.inner.lock().browse_connections.add(1);
    }

    pub fn control_collision(&self) {
        self.inner.lock().control_channel_collisions.add(1);
    }

    /// A peer supporting the controller role is discovered.
    pub fn controller_features(&self, id: PeerId, features: AvrcpControllerFeatures) {
        let mut inner = self.inner.lock();
        if features.contains(AvrcpControllerFeatures::SUPPORTSBROWSING) {
            inner.controller_supporting_browsing(id);
        }

        if features.supports_cover_art() {
            inner.controller_supporting_cover_art(id);
        }
    }

    /// A peer supporting the target role is discovered.
    pub fn target_features(&self, id: PeerId, features: AvrcpTargetFeatures) {
        let mut inner = self.inner.lock();
        if features.contains(AvrcpTargetFeatures::SUPPORTSBROWSING) {
            inner.target_supporting_browsing(id);
        }

        if features.contains(AvrcpTargetFeatures::SUPPORTSCOVERART) {
            inner.target_supporting_cover_art(id);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use {fuchsia_inspect::assert_data_tree, fuchsia_inspect_derive::WithInspect};

    #[test]
    fn multiple_peers_connection_updates_to_shared_node() {
        let inspect = inspect::Inspector::new();

        let metrics = MetricsNode::default().with_inspect(inspect.root(), "metrics").unwrap();

        let (id1, metrics1) = (PeerId(2220), metrics.clone());
        let (id2, metrics2) = (PeerId(7982), metrics.clone());

        // Default inspect tree.
        assert_data_tree!(inspect, root: {
            metrics: contains {
                connection_errors: 0u64,
                control_connections: 0u64,
                browse_connections: 0u64,
                distinct_peers: 0u64,
                control_channel_collisions: 0u64,
            }
        });

        // Peer #1 is discovered but encounters a connection error.
        metrics1.new_peer(id1);
        metrics1.connection_error();
        // Peer #2 successfully connects.
        metrics1.new_peer(id2);
        metrics2.control_connection();
        assert_data_tree!(inspect, root: {
            metrics: contains {
                connection_errors: 1u64,
                control_connections: 1u64,
                browse_connections: 0u64,
                distinct_peers: 2u64,
                control_channel_collisions: 0u64,
            }
        });

        // Maybe a faulty peer #1 - but eventually connects.
        metrics1.connection_error();
        metrics1.connection_error();
        metrics1.control_connection();
        assert_data_tree!(inspect, root: {
            metrics: contains {
                connection_errors: 3u64,
                control_connections: 2u64,
                browse_connections: 0u64,
                distinct_peers: 2u64,
                control_channel_collisions: 0u64,
            }
        });

        // Peer #1 re-connects, also with a browse channel - identifying the peer again
        // does not update the distinct_peers count.
        metrics1.new_peer(id1);
        metrics1.control_collision();
        metrics1.control_connection();
        metrics1.browse_connection();
        assert_data_tree!(inspect, root: {
            metrics: contains {
                connection_errors: 3u64,
                control_connections: 3u64,
                browse_connections: 1u64,
                distinct_peers: 2u64,
                control_channel_collisions: 1u64,
            }
        });
    }

    #[test]
    fn controller_peers_service_updates() {
        let inspect = inspect::Inspector::new();

        let metrics = MetricsNode::default().with_inspect(inspect.root(), "metrics").unwrap();

        // Peer #1 doesn't support anything.
        let id1 = PeerId(1102);
        let tg_service1 = AvrcpTargetFeatures::empty();
        let ct_service1 = AvrcpControllerFeatures::empty();
        metrics.controller_features(id1, ct_service1);
        metrics.target_features(id1, tg_service1);
        assert_data_tree!(inspect, root: {
            metrics: contains {
                target_peers_supporting_browsing: 0u64,
                distinct_target_peers_supporting_browsing: 0u64,
                target_peers_supporting_cover_art: 0u64,
                distinct_target_peers_supporting_cover_art: 0u64,
                controller_peers_supporting_browsing: 0u64,
                distinct_controller_peers_supporting_browsing: 0u64,
                controller_peers_supporting_cover_art: 0u64,
                distinct_controller_peers_supporting_cover_art: 0u64,
            }
        });

        // Peer #2 supports everything.
        let id2 = PeerId(1102);
        let ct_service2 = AvrcpControllerFeatures::all();
        let tg_service2 = AvrcpTargetFeatures::all();
        metrics.controller_features(id2, ct_service2);
        metrics.target_features(id2, tg_service2);
        assert_data_tree!(inspect, root: {
            metrics: contains {
                target_peers_supporting_browsing: 1u64,
                distinct_target_peers_supporting_browsing: 1u64,
                target_peers_supporting_cover_art: 1u64,
                distinct_target_peers_supporting_cover_art: 1u64,
                controller_peers_supporting_browsing: 1u64,
                distinct_controller_peers_supporting_browsing: 1u64,
                controller_peers_supporting_cover_art: 1u64,
                distinct_controller_peers_supporting_cover_art: 1u64,
            }
        });
        // Peer #2 is re-discovered. Distinct counts shouldn't change.
        metrics.controller_features(id2, ct_service2);
        metrics.target_features(id2, tg_service2);
        assert_data_tree!(inspect, root: {
            metrics: contains {
                target_peers_supporting_browsing: 2u64,
                distinct_target_peers_supporting_browsing: 1u64,
                target_peers_supporting_cover_art: 2u64,
                distinct_target_peers_supporting_cover_art: 1u64,
                controller_peers_supporting_browsing: 2u64,
                distinct_controller_peers_supporting_browsing: 1u64,
                controller_peers_supporting_cover_art: 2u64,
                distinct_controller_peers_supporting_cover_art: 1u64,
            }
        });
    }
}

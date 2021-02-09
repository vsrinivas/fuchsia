// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_async as fasync, fuchsia_inspect as inspect,
    fuchsia_inspect_contrib::{
        inspect_log,
        nodes::{BoundedListNode, NodeExt, TimeProperty},
    },
    fuchsia_inspect_derive::{AttachError, Inspect},
};

use crate::profile::AvrcpService;

/// The maximum number of feature sets we store for a remote peer.
/// This is useful in the case of peer disconnecting/reconnecting, as we will
/// save the last `MAX_FEATURE_SETS` feature sets.
const MAX_FEATURE_SETS: usize = 3;

#[derive(Default)]
pub struct RemotePeerInspect {
    target_info: Option<BoundedListNode>,
    controller_info: Option<BoundedListNode>,
    /// The last known connected time.
    last_connected: Option<TimeProperty>,
    inspect_node: inspect::Node,
}

impl Inspect for &mut RemotePeerInspect {
    fn iattach(
        self,
        parent: &fuchsia_inspect::Node,
        name: impl AsRef<str>,
    ) -> Result<(), AttachError> {
        self.inspect_node = parent.create_child(name);
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
    pub fn node(&self) -> &inspect::Node {
        &self.inspect_node
    }

    pub fn record_target_features(&mut self, service: AvrcpService) {
        if let AvrcpService::Target { features, protocol_version, .. } = service {
            self.target_info.as_mut().map(|desc| {
                inspect_log!(
                    desc,
                    features: format!("{:?}", features),
                    version: format!("{:?}", protocol_version)
                );
            });
        }
    }

    pub fn record_controller_features(&mut self, service: AvrcpService) {
        if let AvrcpService::Controller { features, protocol_version, .. } = service {
            self.controller_info.as_mut().map(|desc| {
                inspect_log!(
                    desc,
                    features: format!("{:?}", features),
                    version: format!("{:?}", protocol_version)
                );
            });
        }
    }

    pub fn record_connected(&mut self, at: fasync::Time) {
        if let Some(prop) = &self.last_connected {
            prop.set_at(at.into());
        } else {
            self.last_connected =
                Some(self.inspect_node.create_time_at("last_connected", at.into()));
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::profile::{AvcrpControllerFeatures, AvcrpTargetFeatures, AvrcpProtocolVersion};
    use {
        fuchsia_inspect::{assert_inspect_tree, testing::AnyProperty},
        fuchsia_inspect_derive::WithInspect,
    };

    #[test]
    fn remote_peer_inspect_tree() {
        let inspect = inspect::Inspector::new();

        let mut peer_inspect =
            RemotePeerInspect::default().with_inspect(inspect.root(), "peer").unwrap();

        // Default inspect tree.
        assert_inspect_tree!(inspect, root: {
            peer: {
                controller_info: {},
                target_info: {},
            }
        });

        let target_info = AvrcpService::Target {
            features: AvcrpTargetFeatures::PLAYERSETTINGS,
            psm: 20,
            protocol_version: AvrcpProtocolVersion(1, 5),
        };
        let controller_info = AvrcpService::Controller {
            features: AvcrpControllerFeatures::CATEGORY1,
            psm: 10,
            protocol_version: AvrcpProtocolVersion(1, 6),
        };
        // Setting the opposite feature set has no effect.
        peer_inspect.record_target_features(controller_info);
        peer_inspect.record_controller_features(target_info);
        assert_inspect_tree!(inspect, root: {
            peer: {
                controller_info: {},
                target_info: {},
            }
        });

        peer_inspect.record_target_features(target_info);
        peer_inspect.record_controller_features(controller_info);
        assert_inspect_tree!(inspect, root: {
            peer: {
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
        assert_inspect_tree!(inspect, root: {
            peer: {
                controller_info: contains {},
                target_info: contains {},
                last_connected: AnyProperty,
            }
        });
    }
}

// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_inspect::{self as inspect, NumericProperty},
    fuchsia_inspect_derive::{AttachError, Inspect},
    parking_lot::Mutex,
    std::sync::Arc,
};

pub const METRICS_NODE_NAME: &str = "metrics";

#[derive(Default, Inspect)]
pub struct MetricsNodeInner {
    /// Total number of connection errors.
    connection_errors: inspect::UintProperty,

    /// Total number of connections.
    total_connections: inspect::UintProperty,

    /// The inspect node for this object.
    inspect_node: inspect::Node,
}

/// An object, backed by inspect, used to track cumulative metrics for the AVRCP component.
#[derive(Clone, Default)]
pub struct MetricsNode {
    inner: Arc<Mutex<MetricsNodeInner>>,
}

impl Inspect for &mut MetricsNode {
    fn iattach(self, parent: &inspect::Node, name: impl AsRef<str>) -> Result<(), AttachError> {
        // A manual implementation is required in order to propagate the node `name`.
        self.inner.iattach(parent, name)
    }
}

impl MetricsNode {
    pub fn connection_error(&self) {
        self.inner.lock().connection_errors.add(1);
    }

    pub fn connected(&self) {
        self.inner.lock().total_connections.add(1);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use {fuchsia_inspect::assert_inspect_tree, fuchsia_inspect_derive::WithInspect};

    #[test]
    fn multiple_peers_updated_shared_metrics_node() {
        let inspect = inspect::Inspector::new();

        let metrics = MetricsNode::default().with_inspect(inspect.root(), "metrics").unwrap();

        let metrics1 = metrics.clone();
        let metrics2 = metrics.clone();

        // Default inspect tree.
        assert_inspect_tree!(inspect, root: {
            metrics: {
                connection_errors: 0u64,
                total_connections: 0u64,
            }
        });

        // Peer #1 encounters a connection error.
        metrics1.connection_error();
        // Peer #2 successfully connects.
        metrics2.connected();
        assert_inspect_tree!(inspect, root: {
            metrics: {
                connection_errors: 1u64,
                total_connections: 1u64,
            }
        });

        // Maybe a faulty peer #1 - but eventually connects.
        metrics1.connection_error();
        metrics1.connection_error();
        metrics1.connected();
        assert_inspect_tree!(inspect, root: {
            metrics: {
                connection_errors: 3u64,
                total_connections: 2u64,
            }
        });

        // Peer #1 re-connects.
        metrics1.connected();
        assert_inspect_tree!(inspect, root: {
            metrics: {
                connection_errors: 3u64,
                total_connections: 3u64,
            }
        });
    }
}

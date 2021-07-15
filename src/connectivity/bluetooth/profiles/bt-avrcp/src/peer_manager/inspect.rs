// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_inspect as inspect,
    fuchsia_inspect_derive::{AttachError, Inspect},
};

use crate::metrics::MetricsNode;

#[derive(Default)]
pub struct PeerManagerInspect {
    /// The shared node for cumulative metrics - used by peers managed by the `PeerManager`.
    metrics_node: MetricsNode,
    inspect_node: inspect::Node,
}

impl Inspect for &mut PeerManagerInspect {
    fn iattach(self, parent: &inspect::Node, name: impl AsRef<str>) -> Result<(), AttachError> {
        self.inspect_node = parent.create_child(name.as_ref());
        Ok(())
    }
}

impl PeerManagerInspect {
    pub fn node(&self) -> &inspect::Node {
        &self.inspect_node
    }

    pub fn metrics_node(&self) -> &MetricsNode {
        &self.metrics_node
    }

    pub fn set_metrics_node(&mut self, node: MetricsNode) {
        self.metrics_node = node;
    }
}

// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Used to manage the lifetime of the inspect node for `setting_proxy`.
pub struct SettingProxyNode {
    node: fuchsia_inspect::Node,
}

impl SettingProxyNode {
    /// Construct a new `setting_proxy` node under the `parent` node.
    pub fn new(parent: &fuchsia_inspect::Node) -> Self {
        Self { node: parent.create_child("setting_proxy") }
    }

    /// Retrieve the `setting_proxy` node.
    pub fn node(&self) -> &fuchsia_inspect::Node {
        &self.node
    }
}

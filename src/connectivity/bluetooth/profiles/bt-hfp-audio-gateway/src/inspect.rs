// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_inspect::{self as inspect, Property};
use fuchsia_inspect_derive::Inspect;

#[derive(Default, Debug, Inspect)]
pub struct HfpInspect {
    pub autoconnect: inspect::BoolProperty,
    inspect_node: inspect::Node,
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

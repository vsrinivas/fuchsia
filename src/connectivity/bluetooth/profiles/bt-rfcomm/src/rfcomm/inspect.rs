// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_inspect::{self as inspect, Property},
    fuchsia_inspect_derive::{AttachError, Inspect},
};

/// An inspect node that represents information about the current state of the RFCOMM Session.
#[derive(Default, Debug)]
pub struct SessionInspect {
    /// Whether the Session is currently connected to a peer.
    connected: inspect::StringProperty,
    inspect_node: inspect::Node,
}

impl Inspect for &mut SessionInspect {
    fn iattach(self, parent: &inspect::Node, name: impl AsRef<str>) -> Result<(), AttachError> {
        self.inspect_node = parent.create_child(name);
        self.connected = self.inspect_node.create_string("connected", "Connected");
        Ok(())
    }
}

impl SessionInspect {
    pub fn disconnect(&mut self) {
        self.connected.set("Disconnected");
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fuchsia_inspect::assert_inspect_tree;
    use fuchsia_inspect_derive::WithInspect;

    #[test]
    fn inspect_tree() {
        let inspect = inspect::Inspector::new();

        let mut session_inspect =
            SessionInspect::default().with_inspect(inspect.root(), "session").unwrap();

        // Default inspect tree.
        assert_inspect_tree!(inspect, root: {
            session: {
                connected: "Connected",
            }
        });

        // Inspect when disconnected.
        session_inspect.disconnect();
        assert_inspect_tree!(inspect, root: {
            session: {
                connected: "Disconnected",
            }
        });
    }
}

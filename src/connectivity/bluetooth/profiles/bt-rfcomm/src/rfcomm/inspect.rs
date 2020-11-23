// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_inspect::{self as inspect, Property},
    fuchsia_inspect_derive::{AttachError, Inspect},
};

use crate::rfcomm::{
    session::multiplexer::SessionParameters,
    types::{Role, DLCI},
};

pub(crate) const CREDIT_FLOW_CONTROL: &str = "Credit-Based";
const NO_FLOW_CONTROL: &str = "None";

/// Helper function that fulfills the role of the `Display` trait for the `Role` type.
fn role_to_display_str(role: Role) -> &'static str {
    match role {
        Role::Unassigned => "Unassigned",
        Role::Negotiating => "Negotiating",
        Role::Responder => "Responder",
        Role::Initiator => "Initiator",
    }
}

#[derive(Default, Debug, Inspect)]
pub struct SessionChannelInspect {
    /// The DLCI of the channel. Managed manually.
    #[inspect(skip)]
    dlci: inspect::UintProperty,
    inspect_node: inspect::Node,
}

impl SessionChannelInspect {
    pub fn set_dlci(&mut self, dlci: DLCI) {
        self.dlci = self.inspect_node.create_uint("dlci", u8::from(dlci) as u64);
    }
}

/// An inspect node that represents information about the current state of the Session Multiplexer.
#[derive(Default, Debug)]
pub struct SessionMultiplexerInspect {
    /// The current role of the multiplexer.
    role: inspect::StringProperty,
    /// The flow control parameter of the multiplexer.
    flow_control: inspect::StringProperty,
    /// The maximum frame size parameter of the multiplexer.
    max_frame_size: inspect::UintProperty,
    inspect_node: inspect::Node,
}

impl Inspect for &mut SessionMultiplexerInspect {
    fn iattach(self, parent: &inspect::Node, name: impl AsRef<str>) -> Result<(), AttachError> {
        self.inspect_node = parent.create_child(name);
        self.role = self.inspect_node.create_string("role", role_to_display_str(Role::Unassigned));
        Ok(())
    }
}

impl SessionMultiplexerInspect {
    /// Set the role in inspect. This should only be called when the role changes.
    pub fn set_role(&mut self, role: Role) {
        self.role.set(role_to_display_str(role));
    }

    pub fn set_parameters(&mut self, parameters: SessionParameters) {
        let flow_control =
            if parameters.credit_based_flow { CREDIT_FLOW_CONTROL } else { NO_FLOW_CONTROL };
        self.flow_control = self.inspect_node.create_string("flow_control", flow_control);
        self.max_frame_size =
            self.inspect_node.create_uint("max_frame_size", parameters.max_frame_size as u64);
    }

    pub fn node(&self) -> &inspect::Node {
        &self.inspect_node
    }
}

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
    pub fn node(&self) -> &inspect::Node {
        &self.inspect_node
    }

    pub fn disconnect(&mut self) {
        self.connected.set("Disconnected");
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fuchsia_inspect::assert_inspect_tree;
    use fuchsia_inspect_derive::WithInspect;
    use std::convert::TryFrom;

    #[test]
    fn session_inspect_tree() {
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

    #[test]
    fn session_multiplexer_inspect_tree() {
        let inspect = inspect::Inspector::new();

        let mut multiplexer = SessionMultiplexerInspect::default()
            .with_inspect(inspect.root(), "multiplexer")
            .unwrap();

        // Default inspect tree.
        assert_inspect_tree!(inspect, root: {
            multiplexer: {
                role: "Unassigned",
            }
        });

        // Inspect with a different role and parameters.
        let parameters = SessionParameters { credit_based_flow: true, max_frame_size: 99 };
        multiplexer.set_role(Role::Initiator);
        multiplexer.set_parameters(parameters);
        assert_inspect_tree!(inspect, root: {
            multiplexer: {
                role: "Initiator",
                flow_control: CREDIT_FLOW_CONTROL,
                max_frame_size: 99u64,
            }
        });
    }

    #[test]
    fn session_channel_inspect_tree() {
        let inspect = inspect::Inspector::new();

        let mut channel =
            SessionChannelInspect::default().with_inspect(inspect.root(), "channel").unwrap();

        // Default inspect tree.
        assert_inspect_tree!(inspect, root: {
            channel: {
            }
        });

        // Inspect with a DLCI.
        channel.set_dlci(DLCI::try_from(8).unwrap());
        assert_inspect_tree!(inspect, root: {
            channel: {
                dlci: 8u64,
            }
        });
    }
}

// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_async as fasync,
    fuchsia_bluetooth::inspect::DataStreamInspect,
    fuchsia_inspect::{self as inspect, Property},
    fuchsia_inspect_derive::{AttachError, IValue, Inspect},
};

use crate::rfcomm::{
    session::{channel::FlowControlMode, multiplexer::SessionParameters},
    types::{Role, DLCI},
};

pub(crate) const FLOW_CONTROLLER: &str = "flow_controller";
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

/// Tracks the data stream inspect stats for a channel.
/// Properties are tracked in both directions: data sent to the remote entity and data
/// received from the remote.
#[derive(Inspect, Default)]
pub struct DuplexDataStreamInspect {
    #[inspect(rename = "inbound_stream")]
    inbound: DataStreamInspect,
    #[inspect(rename = "outbound_stream")]
    outbound: DataStreamInspect,
}

impl DuplexDataStreamInspect {
    pub fn start(&mut self) {
        self.inbound.start();
        self.outbound.start();
    }

    pub fn record_inbound_transfer(&mut self, bytes: usize, at: fasync::Time) {
        self.inbound.record_transferred(bytes, at);
    }

    pub fn record_outbound_transfer(&mut self, bytes: usize, at: fasync::Time) {
        self.outbound.record_transferred(bytes, at);
    }
}

/// An inspect node that represents information about the current state of a Session Channel.
#[derive(Default, Debug, Inspect)]
pub struct SessionChannelInspect {
    /// The DLCI of the channel.
    dlci: inspect::UintProperty,
    /// The initial local credit amount (if applicable).
    initial_local_credits: IValue<Option<u64>>,
    /// The initial remote credit amount (if applicable).
    initial_remote_credits: IValue<Option<u64>>,
    inspect_node: inspect::Node,
}

impl SessionChannelInspect {
    pub fn node(&self) -> &inspect::Node {
        &self.inspect_node
    }

    pub fn set_dlci(&mut self, dlci: DLCI) {
        self.dlci.set(u8::from(dlci) as u64);
    }

    pub fn set_flow_control(&mut self, flow_control: FlowControlMode) {
        if let FlowControlMode::CreditBased(credits) = flow_control {
            self.initial_local_credits.iset(Some(credits.local() as u64));
            self.initial_remote_credits.iset(Some(credits.remote() as u64));
        }
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
    use fuchsia_async::DurationExt;
    use fuchsia_inspect::assert_inspect_tree;
    use fuchsia_inspect_derive::WithInspect;
    use fuchsia_zircon::DurationNum;
    use std::convert::TryFrom;

    use crate::rfcomm::session::channel::Credits;

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
                dlci: 0u64,
            }
        });

        // Inspect with a DLCI.
        channel.set_dlci(DLCI::try_from(8).unwrap());
        assert_inspect_tree!(inspect, root: {
            channel: {
                dlci: 8u64,
            }
        });

        // Flow control property set with a null flow control mode. No credits should be stored.
        channel.set_flow_control(FlowControlMode::None);
        assert_inspect_tree!(inspect, root: {
            channel: {
                dlci: 8u64,
            }
        });

        // Flow control property set with a credit based flow control mode.
        channel.set_flow_control(FlowControlMode::CreditBased(Credits::new(10, 19)));
        assert_inspect_tree!(inspect, root: {
            channel: {
                dlci: 8u64,
                initial_local_credits: 10u64,
                initial_remote_credits: 19u64,
            }
        });
    }

    #[test]
    fn duplex_data_stream_inspect_tree_updates_when_changed() {
        let exec = fasync::Executor::new_with_fake_time().unwrap();
        exec.set_fake_time(fasync::Time::from_nanos(1_234_567));

        let inspect = inspect::Inspector::new();
        let mut stream =
            DuplexDataStreamInspect::default().with_inspect(inspect.root(), "stream").unwrap();
        // Default inspect tree.
        assert_inspect_tree!(inspect, root: {
            inbound_stream: {
                bytes_per_second_current: 0u64,
                total_bytes: 0u64,
            },
            outbound_stream: {
                bytes_per_second_current: 0u64,
                total_bytes: 0u64,
            },
        });

        stream.start();
        // Both nodes should have same start_time.
        assert_inspect_tree!(inspect, root: {
            inbound_stream: {
                bytes_per_second_current: 0u64,
                start_time: 1_234_567i64,
                total_bytes: 0u64,
            },
            outbound_stream: {
                bytes_per_second_current: 0u64,
                start_time: 1_234_567i64,
                total_bytes: 0u64,
            },
        });

        exec.set_fake_time(1.seconds().after_now());
        // An inbound transfer should have no impact on the outbound stats.
        stream.record_inbound_transfer(500, fasync::Time::now());
        assert_inspect_tree!(inspect, root: {
            inbound_stream: {
                bytes_per_second_current: 500u64,
                start_time: 1_234_567i64,
                total_bytes: 500u64,
            },
            outbound_stream: {
                bytes_per_second_current: 0u64,
                start_time: 1_234_567i64,
                total_bytes: 0u64,
            },
        });

        exec.set_fake_time(1.seconds().after_now());
        stream.record_outbound_transfer(250, fasync::Time::now());
        assert_inspect_tree!(inspect, root: {
            inbound_stream: {
                bytes_per_second_current: 500u64, // 500 bytes in 1 second
                start_time: 1_234_567i64,
                total_bytes: 500u64,
            },
            outbound_stream: {
                bytes_per_second_current: 125u64, // 250 bytes in 2 seconds
                start_time: 1_234_567i64,
                total_bytes: 250u64,
            },
        });
    }
}

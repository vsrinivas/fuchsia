// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    std::convert::TryFrom,
};

use crate::rfcomm::frame::{FrameParseError, FrameType};

/// Server Channels are 5 bits wide; they are the 5 most significant bits of the
/// DLCI.
/// Server Channels 0 and 31 are reserved. See RFCOMM 5.4.
#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub struct ServerChannel(pub u8);

impl ServerChannel {
    const MAX: ServerChannel = ServerChannel(30);
    const MIN: ServerChannel = ServerChannel(1);

    /// Returns an iterator over all the Server Channels.
    pub fn all() -> impl Iterator<Item = ServerChannel> {
        (Self::MIN.0..=Self::MAX.0).map(|x| ServerChannel(x))
    }
}

/// Identifier for a direct link connection (DLC) between devices.
/// The DLCI is 6 bits wide and is split into a direction bit and a
/// 5-bit Server Channel.
/// DLCIs 1 and 62-63 are reserved and never used in RFCOMM.
/// See RFCOMM 5.4.
#[derive(Clone, Copy, Hash, Eq, Debug, PartialEq)]
pub struct DLCI(u8);

impl DLCI {
    /// The control channel for the RFCOMM Multiplexer.
    const MUX_CONTROL_DLCI: DLCI = DLCI(0);
    /// The minimum user-space DLCI.
    const MIN_USER_DLCI: DLCI = DLCI(2);
    /// The maximum user-space DLCI.
    const MAX_USER_DLCI: DLCI = DLCI(61);

    pub fn is_mux_control(&self) -> bool {
        *self == Self::MUX_CONTROL_DLCI
    }
}

impl TryFrom<u8> for DLCI {
    type Error = FrameParseError;

    fn try_from(value: u8) -> Result<DLCI, Self::Error> {
        if value != DLCI::MUX_CONTROL_DLCI.0
            && (value < DLCI::MIN_USER_DLCI.0 || value > DLCI::MAX_USER_DLCI.0)
        {
            return Err(FrameParseError::InvalidDLCI(value));
        }
        Ok(DLCI(value))
    }
}

impl From<DLCI> for u8 {
    fn from(value: DLCI) -> u8 {
        value.0
    }
}

/// The Role assigned to a device in an RFCOMM Session.
#[derive(Copy, Clone, Debug, PartialEq)]
#[allow(unused)]
pub enum Role {
    /// RFCOMM Session has not started up the start-up procedure.
    Unassigned,
    /// The start-up procedure is in progress, and so the role is being negotiated.
    Negotiating,
    /// The device that starts up the multiplexer control channel is considered
    /// the initiator.
    Initiator,
    /// The device that responds to the start-up procedure.
    Responder,
}

impl Role {
    /// Returns the Role opposite to our Role.
    #[cfg(test)]
    pub fn opposite_role(&self) -> Self {
        match self {
            Role::Unassigned => Role::Unassigned,
            Role::Initiator => Role::Responder,
            Role::Responder | Role::Negotiating => Role::Initiator,
        }
    }

    /// Returns true if the multiplexer for this session has started - namely,
    /// a role of Initiator or Responder has been assigned to the device.
    pub fn is_multiplexer_started(&self) -> bool {
        *self == Role::Initiator || *self == Role::Responder
    }
}

/// The C/R bit in RFCOMM. This is used both at the frame level and the multiplexer
/// channel command level. See RFCOMM 5.1.3 and 5.4.6, respectively.
#[derive(Debug, PartialEq)]
pub enum CommandResponse {
    Command,
    Response,
}

impl CommandResponse {
    /// Classifies a frame as a Command or Response frame.
    pub fn classify(role: Role, frame_type: FrameType, cr_bit: bool) -> Result<Self, Error> {
        // See Table 1 in GSM 5.2.1.2, which describes exactly how the C/R bit is
        // interpreted if the multiplexer has started.
        if role.is_multiplexer_started() {
            let command_response = match (role, cr_bit) {
                (Role::Initiator, true) | (Role::Responder, false) => CommandResponse::Command,
                _ => CommandResponse::Response,
            };
            return Ok(command_response);
        }

        // Otherwise, assume the frame has the role of the sender (assuming mux startup succeeds).
        let command_response = match frame_type {
            FrameType::SetAsynchronousBalancedMode => {
                if cr_bit {
                    CommandResponse::Command
                } else {
                    CommandResponse::Response
                }
            }
            FrameType::DisconnectedMode | FrameType::UnnumberedAcknowledgement => {
                if cr_bit {
                    CommandResponse::Response
                } else {
                    CommandResponse::Command
                }
            }
            frame_type => {
                return Err(format_err!("Invalid frame before mux startup: {:?}", frame_type))
            }
        };
        Ok(command_response)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use matches::assert_matches;

    #[test]
    fn test_convert_to_opposite_role() {
        let role = Role::Initiator;
        assert_eq!(role.opposite_role(), Role::Responder);

        let role = Role::Responder;
        assert_eq!(role.opposite_role(), Role::Initiator);

        let role = Role::Unassigned;
        assert_eq!(role.opposite_role(), Role::Unassigned);

        let role = Role::Negotiating;
        assert_eq!(role.opposite_role(), Role::Initiator);
    }

    #[test]
    fn test_create_dlci() {
        let v1 = 10;
        let dlci = DLCI::try_from(v1);
        assert!(dlci.is_ok());

        let v2 = 0;
        let dlci = DLCI::try_from(v2);
        assert!(dlci.is_ok());

        let v3 = 2;
        let dlci = DLCI::try_from(v3);
        assert!(dlci.is_ok());

        let v4 = 61;
        let dlci = DLCI::try_from(v4);
        assert!(dlci.is_ok());

        let v5 = 1;
        let dlci = DLCI::try_from(v5);
        assert!(dlci.is_err());

        let v6 = 62;
        let dlci = DLCI::try_from(v6);
        assert!(dlci.is_err());

        let v7 = 63;
        let dlci = DLCI::try_from(v7);
        assert!(dlci.is_err());
    }

    #[test]
    fn test_classify_command_response_multiplexer_started() {
        // Multiplexer started because a Role has been assigned.
        let role = Role::Initiator;
        let frame = FrameType::SetAsynchronousBalancedMode;
        let cr_bit = true;
        assert_matches!(
            CommandResponse::classify(role, frame, cr_bit),
            Ok(CommandResponse::Command)
        );

        let role = Role::Responder;
        let frame = FrameType::UnnumberedInfoHeaderCheck;
        let cr_bit = false;
        assert_matches!(
            CommandResponse::classify(role, frame, cr_bit),
            Ok(CommandResponse::Command)
        );

        let role = Role::Initiator;
        let frame = FrameType::SetAsynchronousBalancedMode;
        let cr_bit = false;
        assert_matches!(
            CommandResponse::classify(role, frame, cr_bit),
            Ok(CommandResponse::Response)
        );

        let role = Role::Responder;
        let frame = FrameType::SetAsynchronousBalancedMode;
        let cr_bit = true;
        assert_matches!(
            CommandResponse::classify(role, frame, cr_bit),
            Ok(CommandResponse::Response)
        );
    }

    /// Tests classifying a SABM command when the multiplexer has not started. The classification
    /// should simply be based on the CR bit.
    #[test]
    fn test_classify_command_response_multiplexer_not_started_sabm() {
        // Mux not started.
        let role = Role::Unassigned;
        let frame = FrameType::SetAsynchronousBalancedMode;
        let cr_bit = true;
        assert_matches!(
            CommandResponse::classify(role, frame, cr_bit),
            Ok(CommandResponse::Command)
        );

        // Mux not started.
        let role = Role::Unassigned;
        let frame = FrameType::SetAsynchronousBalancedMode;
        let cr_bit = false;
        assert_matches!(
            CommandResponse::classify(role, frame, cr_bit),
            Ok(CommandResponse::Response)
        );
    }

    /// Tests classifying a DM/UA command when the multiplexer has not started. The classification
    /// should simply be the opposite of the CR bit.
    #[test]
    fn test_classify_command_response_multiplexer_not_started() {
        // Mux not started.
        let role = Role::Unassigned;
        let frame = FrameType::DisconnectedMode;
        let cr_bit = true;
        assert_matches!(
            CommandResponse::classify(role, frame, cr_bit),
            Ok(CommandResponse::Response)
        );

        // Mux not started.
        let role = Role::Unassigned;
        let frame = FrameType::DisconnectedMode;
        let cr_bit = false;
        assert_matches!(
            CommandResponse::classify(role, frame, cr_bit),
            Ok(CommandResponse::Command)
        );

        // Mux not started.
        let role = Role::Unassigned;
        let frame = FrameType::UnnumberedAcknowledgement;
        let cr_bit = true;
        assert_matches!(
            CommandResponse::classify(role, frame, cr_bit),
            Ok(CommandResponse::Response)
        );

        // Mux not started.
        let role = Role::Unassigned;
        let frame = FrameType::UnnumberedAcknowledgement;
        let cr_bit = false;
        assert_matches!(
            CommandResponse::classify(role, frame, cr_bit),
            Ok(CommandResponse::Command)
        );
    }

    #[test]
    fn test_classify_command_response_invalid_frame_type() {
        // Mux not started - Disconnect can't be sent before startup.
        let role = Role::Unassigned;
        let frame = FrameType::Disconnect;
        let cr_bit = true;
        assert_matches!(CommandResponse::classify(role, frame, cr_bit), Err(_));

        // Mux not started - UIH can't be sent before startup.
        let role = Role::Unassigned;
        let frame = FrameType::UnnumberedInfoHeaderCheck;
        let cr_bit = true;
        assert_matches!(CommandResponse::classify(role, frame, cr_bit), Err(_));
    }
}

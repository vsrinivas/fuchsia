// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::format_err;

use crate::frame::{error::FrameParseError, FrameTypeMarker};
use crate::Role;

/// The C/R bit in RFCOMM. This is used both at the frame level and the multiplexer
/// channel command level. See RFCOMM 5.1.3 and 5.4.6, respectively.
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum CommandResponse {
    Command,
    Response,
}

impl CommandResponse {
    /// Classifies a frame type as a Command or Response.
    pub fn classify(
        role: Role,
        frame_type: FrameTypeMarker,
        cr_bit: bool,
    ) -> Result<Self, FrameParseError> {
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
            FrameTypeMarker::SetAsynchronousBalancedMode => {
                if cr_bit {
                    CommandResponse::Command
                } else {
                    CommandResponse::Response
                }
            }
            FrameTypeMarker::DisconnectedMode | FrameTypeMarker::UnnumberedAcknowledgement => {
                if cr_bit {
                    CommandResponse::Response
                } else {
                    CommandResponse::Command
                }
            }
            frame_type => {
                return Err(FrameParseError::Other(format_err!(
                    "Invalid frame before mux startup: {:?}",
                    frame_type
                )));
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
    fn test_classify_command_response_multiplexer_started() {
        // Multiplexer started because a Role has been assigned.
        let role = Role::Initiator;
        let frame = FrameTypeMarker::SetAsynchronousBalancedMode;
        let cr_bit = true;
        assert_matches!(
            CommandResponse::classify(role, frame, cr_bit),
            Ok(CommandResponse::Command)
        );

        let role = Role::Responder;
        let frame = FrameTypeMarker::UnnumberedInfoHeaderCheck;
        let cr_bit = false;
        assert_matches!(
            CommandResponse::classify(role, frame, cr_bit),
            Ok(CommandResponse::Command)
        );

        let role = Role::Initiator;
        let frame = FrameTypeMarker::SetAsynchronousBalancedMode;
        let cr_bit = false;
        assert_matches!(
            CommandResponse::classify(role, frame, cr_bit),
            Ok(CommandResponse::Response)
        );

        let role = Role::Responder;
        let frame = FrameTypeMarker::SetAsynchronousBalancedMode;
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
        let frame = FrameTypeMarker::SetAsynchronousBalancedMode;
        let cr_bit = true;
        assert_matches!(
            CommandResponse::classify(role, frame, cr_bit),
            Ok(CommandResponse::Command)
        );

        // Mux not started.
        let role = Role::Unassigned;
        let frame = FrameTypeMarker::SetAsynchronousBalancedMode;
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
        let frame = FrameTypeMarker::DisconnectedMode;
        let cr_bit = true;
        assert_matches!(
            CommandResponse::classify(role, frame, cr_bit),
            Ok(CommandResponse::Response)
        );

        // Mux not started.
        let role = Role::Unassigned;
        let frame = FrameTypeMarker::DisconnectedMode;
        let cr_bit = false;
        assert_matches!(
            CommandResponse::classify(role, frame, cr_bit),
            Ok(CommandResponse::Command)
        );

        // Mux not started.
        let role = Role::Unassigned;
        let frame = FrameTypeMarker::UnnumberedAcknowledgement;
        let cr_bit = true;
        assert_matches!(
            CommandResponse::classify(role, frame, cr_bit),
            Ok(CommandResponse::Response)
        );

        // Mux not started.
        let role = Role::Unassigned;
        let frame = FrameTypeMarker::UnnumberedAcknowledgement;
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
        let frame = FrameTypeMarker::Disconnect;
        let cr_bit = true;
        assert_matches!(CommandResponse::classify(role, frame, cr_bit), Err(_));

        // Mux not started - UIH can't be sent before startup.
        let role = Role::Unassigned;
        let frame = FrameTypeMarker::UnnumberedInfoHeaderCheck;
        let cr_bit = true;
        assert_matches!(CommandResponse::classify(role, frame, cr_bit), Err(_));
    }
}

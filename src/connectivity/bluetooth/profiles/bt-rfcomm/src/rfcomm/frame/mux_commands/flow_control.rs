// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::rfcomm::frame::{
    mux_commands::{Decodable, Encodable},
    FrameParseError,
};

/// The length (in bytes) of a Flow Control On Command.
/// Defined in GSM 5.4.6.3.5.
const FLOW_CONTROL_ON_COMMAND_LENGTH: usize = 0;

/// The length (in bytes) of a Flow Control Off Command.
/// Defined in GSM 5.4.6.3.6.
const FLOW_CONTROL_OFF_COMMAND_LENGTH: usize = 0;

/// The Flow Control On Command used enable aggregate flow.
/// The command contains no parameters.
/// Defined in GSM 5.4.6.3.5.
#[derive(Debug, PartialEq)]
struct FlowControlOnCommand {}

impl Decodable for FlowControlOnCommand {
    fn decode(buf: &[u8]) -> Result<Self, FrameParseError> {
        if buf.len() != FLOW_CONTROL_ON_COMMAND_LENGTH {
            return Err(FrameParseError::InvalidBufferLength(
                FLOW_CONTROL_ON_COMMAND_LENGTH,
                buf.len(),
            ));
        }
        Ok(Self {})
    }
}

impl Encodable for FlowControlOnCommand {
    fn encoded_len(&self) -> usize {
        FLOW_CONTROL_ON_COMMAND_LENGTH
    }

    fn encode(&self, buf: &mut [u8]) -> Result<(), FrameParseError> {
        if buf.len() < self.encoded_len() {
            return Err(FrameParseError::BufferTooSmall);
        }
        Ok(())
    }
}

/// The Flow Control On Command used disable aggregate flow.
/// The command contains no parameters.
/// Defined in GSM 5.4.6.3.5.
#[derive(Debug, PartialEq)]
struct FlowControlOffCommand {}

impl Decodable for FlowControlOffCommand {
    fn decode(buf: &[u8]) -> Result<Self, FrameParseError> {
        if buf.len() != FLOW_CONTROL_OFF_COMMAND_LENGTH {
            return Err(FrameParseError::InvalidBufferLength(
                FLOW_CONTROL_OFF_COMMAND_LENGTH,
                buf.len(),
            ));
        }
        Ok(Self {})
    }
}

impl Encodable for FlowControlOffCommand {
    fn encoded_len(&self) -> usize {
        FLOW_CONTROL_OFF_COMMAND_LENGTH
    }

    fn encode(&self, buf: &mut [u8]) -> Result<(), FrameParseError> {
        if buf.len() < self.encoded_len() {
            return Err(FrameParseError::BufferTooSmall);
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use matches::assert_matches;

    #[test]
    fn test_decode_flow_control_on_invalid_buf() {
        let buf = [0x00, 0x01, 0x02];
        assert_matches!(
            FlowControlOnCommand::decode(&buf[..]),
            Err(FrameParseError::InvalidBufferLength(FLOW_CONTROL_ON_COMMAND_LENGTH, 3))
        );
    }

    #[test]
    fn test_decode_flow_control_on() {
        let buf = [];
        assert_eq!(FlowControlOnCommand::decode(&buf[..]).unwrap(), FlowControlOnCommand {});
    }

    #[test]
    fn test_encode_flow_control_on() {
        let mut buf = [];
        let expected: [u8; 0] = [];
        let command = FlowControlOnCommand {};
        assert!(command.encode(&mut buf[..]).is_ok());
        assert_eq!(buf, expected);
    }

    #[test]
    fn test_decode_flow_control_off_invalid_buf() {
        let buf = [0x00, 0x01];
        assert_matches!(
            FlowControlOnCommand::decode(&buf[..]),
            Err(FrameParseError::InvalidBufferLength(FLOW_CONTROL_OFF_COMMAND_LENGTH, 2))
        );
    }

    #[test]
    fn test_decode_flow_control_off() {
        let buf = [];
        assert_eq!(FlowControlOffCommand::decode(&buf[..]).unwrap(), FlowControlOffCommand {});
    }

    #[test]
    fn test_encode_flow_control_off() {
        let mut buf = [];
        let expected: [u8; 0] = [];
        let command = FlowControlOffCommand {};
        assert!(command.encode(&mut buf[..]).is_ok());
        assert_eq!(buf, expected);
    }
}

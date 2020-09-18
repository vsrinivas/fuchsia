// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::rfcomm::frame::{
    mux_commands::{Decodable, Encodable},
    FrameParseError,
};

/// The length (in bytes) of a Flow Control Command. Both Flow Control On and Off commands
/// contain no parameters.
/// Defined in GSM 5.4.6.3.5 & 5.4.6.3.6.
const FLOW_CONTROL_COMMAND_LENGTH: usize = 0;

/// The Flow Control Command is used to enable/disable aggregate flow.
/// The command contains no parameters.
/// Defined in GSM 5.4.6.3.5 & 5.4.6.3.6.
#[derive(Debug, PartialEq)]
pub struct FlowControlParams {}

impl Decodable for FlowControlParams {
    fn decode(buf: &[u8]) -> Result<Self, FrameParseError> {
        if buf.len() != FLOW_CONTROL_COMMAND_LENGTH {
            return Err(FrameParseError::InvalidBufferLength(
                FLOW_CONTROL_COMMAND_LENGTH,
                buf.len(),
            ));
        }
        Ok(Self {})
    }
}

impl Encodable for FlowControlParams {
    fn encoded_len(&self) -> usize {
        FLOW_CONTROL_COMMAND_LENGTH
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
    fn test_decode_flow_control_invalid_buf() {
        let buf = [0x00, 0x01, 0x02];
        assert_matches!(
            FlowControlParams::decode(&buf[..]),
            Err(FrameParseError::InvalidBufferLength(FLOW_CONTROL_COMMAND_LENGTH, 3))
        );
    }

    #[test]
    fn test_decode_flow_control() {
        let buf = [];
        assert_eq!(FlowControlParams::decode(&buf[..]).unwrap(), FlowControlParams {});
    }

    #[test]
    fn test_encode_flow_control() {
        let mut buf = [];
        let expected: [u8; 0] = [];
        let command = FlowControlParams {};
        assert!(command.encode(&mut buf[..]).is_ok());
        assert_eq!(buf, expected);
    }
}

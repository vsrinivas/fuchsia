// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use bitfield::bitfield;

use crate::rfcomm::frame::{
    mux_commands::{Decodable, Encodable},
    FrameParseError,
};

/// The NonSupportedCommand Response is always 1 byte. See GSM 7.10 Section 5.4.6.3.8.
const NON_SUPPORTED_COMMAND_RESPONSE_LENGTH: usize = 1;

bitfield! {
    struct NonSupportedCommandField(u8);
    impl Debug;
    pub bool, ea_bit, set_ea_bit: 0;
    pub bool, cr_bit, set_cr_bit: 1;
    pub u8, command, set_command: 7, 2;
}

/// This response is sent whenever a command type is not supported by the device.
/// Defined in GSM 7.10 Section 5.4.6.3.8.
#[derive(Clone, Debug, PartialEq)]
pub struct NonSupportedCommandParams {
    /// The C/R bit is set to the same value as the C/R bit in the non-supported command.
    pub cr_bit: bool,
    /// The non_supported command.
    pub non_supported_command: u8,
}

impl Decodable for NonSupportedCommandParams {
    fn decode(buf: &[u8]) -> Result<Self, FrameParseError> {
        if buf.len() != NON_SUPPORTED_COMMAND_RESPONSE_LENGTH {
            return Err(FrameParseError::InvalidBufferLength(
                NON_SUPPORTED_COMMAND_RESPONSE_LENGTH,
                buf.len(),
            ));
        }

        let command_field = NonSupportedCommandField(buf[0]);
        let cr_bit = command_field.cr_bit();
        let non_supported_command = command_field.command();

        Ok(Self { cr_bit, non_supported_command })
    }
}

impl Encodable for NonSupportedCommandParams {
    fn encoded_len(&self) -> usize {
        NON_SUPPORTED_COMMAND_RESPONSE_LENGTH
    }

    fn encode(&self, buf: &mut [u8]) -> Result<(), FrameParseError> {
        if buf.len() < self.encoded_len() {
            return Err(FrameParseError::BufferTooSmall);
        }

        let mut command_fields = NonSupportedCommandField(0);
        // The E/A bit is always set for this command (GSM 7.10 Section 5.4.6.3.8.).
        command_fields.set_ea_bit(true);
        command_fields.set_cr_bit(self.cr_bit);
        command_fields.set_command(self.non_supported_command);

        buf[0] = command_fields.0;

        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use matches::assert_matches;

    #[test]
    fn test_decode_invalid_buf() {
        let empty_buf = [];
        assert_matches!(
            NonSupportedCommandParams::decode(&empty_buf[..]),
            Err(FrameParseError::InvalidBufferLength(NON_SUPPORTED_COMMAND_RESPONSE_LENGTH, 0))
        );
    }

    #[test]
    fn test_decode_valid_buf() {
        let buf = [
            0b10101011, // C/R Bit = 1, Random Command pattern = 42.
        ];
        let expected = NonSupportedCommandParams { cr_bit: true, non_supported_command: 42 };
        assert_eq!(NonSupportedCommandParams::decode(&buf[..]).unwrap(), expected);
    }

    #[test]
    fn test_encode_buffer_too_small() {
        let mut buf = [];
        let response = NonSupportedCommandParams { cr_bit: false, non_supported_command: 8 };
        assert_matches!(response.encode(&mut buf[..]), Err(FrameParseError::BufferTooSmall));
    }

    #[test]
    fn test_encode_response() {
        let mut buf = [0; 1];
        let response = NonSupportedCommandParams { cr_bit: true, non_supported_command: 8 };
        let expected = [
            0b00100011, // Command = 8, C/R = 1, E/A = 1.
        ];
        assert!(response.encode(&mut buf[..]).is_ok());
        assert_eq!(buf, expected);
    }

    #[test]
    fn test_encode_command() {
        let mut buf = [0; 1];
        let response = NonSupportedCommandParams { cr_bit: false, non_supported_command: 10 };
        let expected = [
            0b00101001, // Command = 10, C/R = 0, E/A = 1.
        ];
        assert!(response.encode(&mut buf[..]).is_ok());
        assert_eq!(buf, expected);
    }
}

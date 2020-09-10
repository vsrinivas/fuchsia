// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    bitfield::bitfield,
    log::trace,
    std::convert::TryFrom,
};

mod dlc_parameter_negotiation;

use crate::pub_decodable_enum;
use crate::rfcomm::{frame::FrameParseError, types::CommandResponse};

pub_decodable_enum! {
    /// The supported Multiplexer Commands in RFCOMM. These commands are sent/received
    /// over the Multiplexer Control Channel (DLCI 0) and are 6 bits wide.
    /// See RFCOMM 4.3.
    MuxCommandType<u8, Error> {
        DLCParameterNegotiation => 0b100000,
        TestCommand => 0b001000,
        FlowControlOnCommand => 0b101000,
        FlowControlOffCommand => 0b011000,
        ModemStatusCommand => 0b111000,
        NonSupportedCommandResponse => 0b000100,
        RemotePortNegotiationCommand => 0b100100,
        RemoteLineStatusCommand => 0b010100,
    }
}

/// A decodable type can be created from a byte buffer.
/// The type returned is separate (copied) from the buffer once decoded.
pub(crate) trait Decodable: Sized {
    /// Decodes into a new object, or returns an error.
    fn decode(buf: &[u8]) -> Result<Self, FrameParseError>;
}

/// A encodable type can write itself into a byte buffer.
pub(crate) trait Encodable: Sized {
    /// Returns the number of bytes necessary to encode |self|
    fn encoded_len(&self) -> usize;
    /// Writes the encoded version of |self| at the start of |buf|
    /// |buf| must be at least size() length.
    fn encode(&self, buf: &mut [u8]) -> Result<(), FrameParseError>;
}

/// The minimum size (in bytes) of a MuxCommand. 1 Byte for the Type field,
/// and at least 1 byte for the Length field. See GSM 5.4.6.1.
const MIN_MUX_COMMAND_SIZE: usize = 2;

/// The MuxCommand Type Field is the first byte in the payload.
/// See GSM 5.4.6.1.
const MUX_COMMAND_TYPE_IDX: usize = 0;
bitfield! {
    struct TypeField(u8);
    impl Debug;
    pub bool, ea_bit, set_ea_bit: 0;
    pub bool, command_response_raw, _: 1;
    pub u8, command_type_raw, _: 7, 2;
}

impl TypeField {
    fn command_type(&self) -> Result<MuxCommandType, Error> {
        MuxCommandType::try_from(self.command_type_raw())
    }

    fn command_response(&self) -> CommandResponse {
        if self.command_response_raw() {
            CommandResponse::Command
        } else {
            CommandResponse::Response
        }
    }
}

/// The MuxCommand Length Field starts at the second byte in the payload.
/// See GSM 5.4.6.1.
const MUX_COMMAND_LENGTH_IDX: usize = 1;
bitfield! {
    pub struct LengthField(u8);
    impl Debug;
    pub bool, ea_bit, _: 0;
    pub u8, length, _: 7, 1;
}

/// Represents an RFCOMM multiplexer command.
// TODO(58681): Update to store the parameters for the various command types.
#[derive(Debug, PartialEq)]
struct MuxCommand {
    /// The type of this MuxCommand - see RFCOMM 4.3 for the supported commands.
    pub command_type: MuxCommandType,

    /// Whether this is a command or response Mux frame.
    pub command_response: CommandResponse,
}

impl Decodable for MuxCommand {
    fn decode(buf: &[u8]) -> Result<Self, FrameParseError> {
        if buf.len() < MIN_MUX_COMMAND_SIZE {
            return Err(FrameParseError::BufferTooSmall);
        }

        // Parse the Type Field from the buffer.
        let type_field = TypeField(buf[MUX_COMMAND_TYPE_IDX]);
        let command_response = type_field.command_response();
        let command_type =
            type_field.command_type().or(Err(FrameParseError::UnsupportedMuxCommandType))?;

        // Parse the Length Field from the buffer. There can be multiple octets.
        // This implementation supports lengths that can fit into a u64 (8 octets).
        const MAX_LENGTH_OCTETS: usize = 8;
        let mut length: u64 = 0;
        let mut num_length_octets: usize = 0;

        for i in MUX_COMMAND_LENGTH_IDX..buf.len() {
            let length_field = LengthField(buf[i]);
            let length_octet: u64 = length_field.length().into();
            length |= length_octet << (7 * num_length_octets);
            num_length_octets += 1;

            // A set EA bit indicates last octet of the length.
            if length_field.ea_bit() {
                break;
            }

            if num_length_octets > MAX_LENGTH_OCTETS {
                return Err(FrameParseError::InvalidFrame);
            }
        }
        trace!("The MuxCommand Length is: {:?}", length);

        // Validate that the buffer is large enough given the variable length.
        // 1 byte for Type field, `num_length_octets` bytes for the length field, and `length`
        // bytes for the data.
        let calculated_buf_size: u64 = 1 + num_length_octets as u64 + length;
        if (buf.len() as u64) < calculated_buf_size {
            return Err(FrameParseError::BufferTooSmall);
        }

        // TODO(58681): Parse the payload of the mux command frame depending on `command_type`.

        Ok(Self { command_type, command_response })
    }
}

// TODO(58681): Update tests with parsed commands when implemented.
#[cfg(test)]
mod tests {
    use super::*;

    use matches::assert_matches;

    #[test]
    fn test_parse_mux_command_empty_buf() {
        let empty_buf = [];
        assert_matches!(MuxCommand::decode(&empty_buf[..]), Err(FrameParseError::BufferTooSmall));
    }

    #[test]
    fn test_parse_mux_command_invalid_command_type() {
        let buf = [
            0b11111101, // Type field, unrecognized command type.
            0b00000001, // Length field, 0 length.
        ];
        assert_matches!(
            MuxCommand::decode(&buf[..]),
            Err(FrameParseError::UnsupportedMuxCommandType)
        );
    }

    #[test]
    fn test_parse_mux_command_too_large_length() {
        let buf = [
            0b10000001, // Type field - DLCParameterNegotiation.
            0b00000010, // Length field, 9 octets - this is too large.
            0b00000010, // Length field, octet 2.
            0b00000010, // Length field, octet 3.
            0b00000010, // Length field, octet 4.
            0b00000010, // Length field, octet 5.
            0b00000010, // Length field, octet 6.
            0b00000010, // Length field, octet 7.
            0b00000010, // Length field, octet 8.
            0b00000010, // Length field, octet 9.
            0b00000000, // Length values - shouldn't matter since we error early.
        ];
        assert_matches!(MuxCommand::decode(&buf[..]), Err(FrameParseError::InvalidFrame));
    }

    #[test]
    fn test_parse_mux_command_missing_length_octets() {
        let buf = [
            0b10000001, // Type field - DLCParameterNegotiation.
            0b00000101, // Length field, Length = 2.
            0b00000010, // Length data octet 1.
                        // Missing octet 2 of length.
        ];
        assert_matches!(MuxCommand::decode(&buf[..]), Err(FrameParseError::BufferTooSmall));
    }

    #[test]
    fn test_parse_dlc_parameter_negotiation_command() {
        let buf = [
            0b10000011, // Type field - DLCParameterNegotiation, C/R = Command.
            0b00010001, // Length field - 1 octet. Length = 8.
            0b00000000, // Length data octet 1.
            0b00000000, // Length data octet 2.
            0b00000000, // Length data octet 3.
            0b00000000, // Length data octet 4.
            0b00000000, // Length data octet 5.
            0b00000000, // Length data octet 6.
            0b00000000, // Length data octet 7.
            0b00000000, // Length data octet 8.
        ];
        let expected = MuxCommand {
            command_type: MuxCommandType::DLCParameterNegotiation,
            command_response: CommandResponse::Command,
        };
        assert_eq!(MuxCommand::decode(&buf[..]).unwrap(), expected);
    }

    #[test]
    fn test_parse_test_command() {
        let buf = [
            0b00100011, // Type field - Test Command, C/R = Command.
            0b00001001, // Length field - 1 octet. Length = 4. Doesn't matter.
            0b00000000, // Length data octet 1.
            0b00000000, // Length data octet 2.
            0b00000000, // Length data octet 3.
            0b00000000, // Length data octet 4.
        ];
        let expected = MuxCommand {
            command_type: MuxCommandType::TestCommand,
            command_response: CommandResponse::Command,
        };
        assert_eq!(MuxCommand::decode(&buf[..]).unwrap(), expected);
    }

    #[test]
    fn test_parse_flow_control_on_command() {
        let buf = [
            0b10100001, // Type field - Test Command, C/R = Response.
            0b00000001, // Length field - 1 octet. Length = 0.
        ];
        let expected = MuxCommand {
            command_type: MuxCommandType::FlowControlOnCommand,
            command_response: CommandResponse::Response,
        };
        assert_eq!(MuxCommand::decode(&buf[..]).unwrap(), expected);
    }

    #[test]
    fn test_parse_flow_control_off_command() {
        let buf = [
            0b01100011, // Type field - Test Command, C/R = Command.
            0b00000001, // Length field - 1 octet. Length = 0.
        ];
        let expected = MuxCommand {
            command_type: MuxCommandType::FlowControlOffCommand,
            command_response: CommandResponse::Command,
        };
        assert_eq!(MuxCommand::decode(&buf[..]).unwrap(), expected);
    }

    #[test]
    fn test_parse_modem_status_command() {
        let buf = [
            0b11100011, // Type field - Test Command, C/R = Command.
            0b00000101, // Length field - 1 octet. Length = 2.
            0b00000000, // Length Data 1.
            0b00000000, // Length Data 2.
        ];
        let expected = MuxCommand {
            command_type: MuxCommandType::ModemStatusCommand,
            command_response: CommandResponse::Command,
        };
        assert_eq!(MuxCommand::decode(&buf[..]).unwrap(), expected);
    }

    #[test]
    fn test_parse_not_supported_command_response() {
        let buf = [
            0b00010001, // Type field - Test Command, C/R = Response.
            0b00000011, // Length field - 1 octet. Length = 1.
            0b00000000, // Length Data 1.
        ];
        let expected = MuxCommand {
            command_type: MuxCommandType::NonSupportedCommandResponse,
            command_response: CommandResponse::Response,
        };
        assert_eq!(MuxCommand::decode(&buf[..]).unwrap(), expected);
    }

    #[test]
    fn test_parse_rpn_command() {
        let buf = [
            0b10010011, // Type field - Test Command, C/R = Command.
            0b00000011, // Length field - 1 octet. Length = 1.
            0b00000000, // Length Data 1.
        ];
        let expected = MuxCommand {
            command_type: MuxCommandType::RemotePortNegotiationCommand,
            command_response: CommandResponse::Command,
        };
        assert_eq!(MuxCommand::decode(&buf[..]).unwrap(), expected);
    }

    #[test]
    fn test_parse_rls_command() {
        let buf = [
            0b01010011, // Type field - Test Command, C/R = Command.
            0b00000101, // Length field - 1 octet. Length = 2.
            0b00000000, // Length Data 1.
            0b00000000, // Length Data 2.
        ];
        let expected = MuxCommand {
            command_type: MuxCommandType::RemoteLineStatusCommand,
            command_response: CommandResponse::Command,
        };
        assert_eq!(MuxCommand::decode(&buf[..]).unwrap(), expected);
    }
}

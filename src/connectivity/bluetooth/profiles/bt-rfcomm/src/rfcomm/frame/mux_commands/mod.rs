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
mod flow_control;
mod modem_status;
mod non_supported;
mod remote_line_status;
mod remote_port_negotiation;
mod test_command;

pub use self::{
    dlc_parameter_negotiation::{CreditBasedFlowHandshake, ParameterNegotiationParams},
    flow_control::FlowControlParams,
    modem_status::ModemStatusParams,
    non_supported::NonSupportedCommandParams,
    remote_line_status::RemoteLineStatusParams,
    remote_port_negotiation::RemotePortNegotiationParams, test_command::TestCommandParams,
};
use crate::pub_decodable_enum;
use crate::rfcomm::{
    frame::{Decodable, Encodable, FrameParseError},
    types::CommandResponse,
};

pub_decodable_enum! {
    /// The supported Multiplexer Commands in RFCOMM. These commands are sent/received
    /// over the Multiplexer Control Channel (DLCI 0) and are 6 bits wide.
    /// See RFCOMM 4.3.
    MuxCommandMarker<u8, Error> {
        ParameterNegotiation => 0b100000,
        Test => 0b001000,
        FlowControlOn => 0b101000,
        FlowControlOff => 0b011000,
        ModemStatus => 0b111000,
        NonSupportedCommand => 0b000100,
        RemotePortNegotiation => 0b100100,
        RemoteLineStatus => 0b010100,
    }
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
    pub bool, cr_bit, set_cr_bit: 1;
    pub u8, command_type_raw, set_command_type: 7, 2;
}

impl TypeField {
    fn command_type(&self) -> Result<MuxCommandMarker, FrameParseError> {
        MuxCommandMarker::try_from(self.command_type_raw())
            .or(Err(FrameParseError::UnsupportedMuxCommandType))
    }

    fn command_response(&self) -> CommandResponse {
        // See GSM 5.4.6.2 on how the cr_bit translates to CommandResponse.
        if self.cr_bit() {
            CommandResponse::Command
        } else {
            CommandResponse::Response
        }
    }
}

/// The MuxCommand Length Field starts at the second byte in the payload.
/// See GSM 5.4.6.1.
const MUX_COMMAND_LENGTH_IDX: usize = 1;

/// The length field is represented as multiple E/A padded octets, each 7-bits wide.
/// See GSM 5.2.1.4.
const MUX_COMMAND_LENGTH_SHIFT: usize = 7;

bitfield! {
    pub struct LengthField(u8);
    impl Debug;
    pub bool, ea_bit, set_ea_bit: 0;
    pub u8, length, set_length: 7, 1;
}

/// The parameters associated with a Mux Command.
#[derive(Debug, PartialEq)]
pub enum MuxCommandParams {
    ParameterNegotiation(ParameterNegotiationParams),
    FlowControlOn(FlowControlParams),
    FlowControlOff(FlowControlParams),
    ModemStatus(ModemStatusParams),
    NonSupported(NonSupportedCommandParams),
    RemoteLineStatus(RemoteLineStatusParams),
    RemotePortNegotiation(RemotePortNegotiationParams),
    Test(TestCommandParams),
}

impl MuxCommandParams {
    fn marker(&self) -> MuxCommandMarker {
        match self {
            Self::ParameterNegotiation(_) => MuxCommandMarker::ParameterNegotiation,
            Self::FlowControlOn(_) => MuxCommandMarker::FlowControlOn,
            Self::FlowControlOff(_) => MuxCommandMarker::FlowControlOff,
            Self::ModemStatus(_) => MuxCommandMarker::ModemStatus,
            Self::NonSupported(_) => MuxCommandMarker::NonSupportedCommand,
            Self::RemoteLineStatus(_) => MuxCommandMarker::RemoteLineStatus,
            Self::RemotePortNegotiation(_) => MuxCommandMarker::RemotePortNegotiation,
            Self::Test(_) => MuxCommandMarker::Test,
        }
    }

    fn decode(command_type: &MuxCommandMarker, buf: &[u8]) -> Result<Self, FrameParseError> {
        let params = match command_type {
            MuxCommandMarker::ParameterNegotiation => {
                Self::ParameterNegotiation(ParameterNegotiationParams::decode(buf)?)
            }
            MuxCommandMarker::FlowControlOn => Self::FlowControlOn(FlowControlParams::decode(buf)?),
            MuxCommandMarker::FlowControlOff => {
                Self::FlowControlOff(FlowControlParams::decode(buf)?)
            }
            MuxCommandMarker::ModemStatus => Self::ModemStatus(ModemStatusParams::decode(buf)?),
            MuxCommandMarker::NonSupportedCommand => {
                Self::NonSupported(NonSupportedCommandParams::decode(buf)?)
            }
            MuxCommandMarker::RemoteLineStatus => {
                Self::RemoteLineStatus(RemoteLineStatusParams::decode(buf)?)
            }
            MuxCommandMarker::RemotePortNegotiation => {
                Self::RemotePortNegotiation(RemotePortNegotiationParams::decode(buf)?)
            }
            MuxCommandMarker::Test => Self::Test(TestCommandParams::decode(buf)?),
        };
        Ok(params)
    }
}

impl Encodable for MuxCommandParams {
    fn encoded_len(&self) -> usize {
        match self {
            Self::ParameterNegotiation(cmd) => cmd.encoded_len(),
            Self::FlowControlOn(cmd) => cmd.encoded_len(),
            Self::FlowControlOff(cmd) => cmd.encoded_len(),
            Self::ModemStatus(cmd) => cmd.encoded_len(),
            Self::NonSupported(cmd) => cmd.encoded_len(),
            Self::RemoteLineStatus(cmd) => cmd.encoded_len(),
            Self::RemotePortNegotiation(cmd) => cmd.encoded_len(),
            Self::Test(cmd) => cmd.encoded_len(),
        }
    }

    fn encode(&self, buf: &mut [u8]) -> Result<(), FrameParseError> {
        match self {
            Self::ParameterNegotiation(cmd) => cmd.encode(buf),
            Self::FlowControlOn(cmd) => cmd.encode(buf),
            Self::FlowControlOff(cmd) => cmd.encode(buf),
            Self::ModemStatus(cmd) => cmd.encode(buf),
            Self::NonSupported(cmd) => cmd.encode(buf),
            Self::RemoteLineStatus(cmd) => cmd.encode(buf),
            Self::RemotePortNegotiation(cmd) => cmd.encode(buf),
            Self::Test(cmd) => cmd.encode(buf),
        }
    }
}

/// Converts the `length` into a vector of E/A padded octets in conformance with the message
/// format defined in GSM 5.4.6.1.
fn length_to_ea_format(mut length: usize) -> Vec<u8> {
    if length == 0 {
        // Return single octet with 0 length.
        let mut length_field = LengthField(0);
        length_field.set_ea_bit(true);
        return vec![length_field.0];
    }

    let mut octets = vec![];
    let mask = 0b01111111;
    // Process the length field in chunks of 7 - E/A bit = 0 for all non-last octets.
    while length != 0 {
        let mut length_field = LengthField(0);
        let length_octet = (length & mask) as u8;
        length_field.set_length(length_octet);
        length_field.set_ea_bit(false);
        octets.push(length_field);
        length >>= MUX_COMMAND_LENGTH_SHIFT;
    }
    // Tag the last length octet with E/A = 1, signifying last octet.
    let last_idx = octets.len() - 1;
    octets[last_idx].set_ea_bit(true);

    octets.iter().map(|l| l.0).collect()
}

/// Represents an RFCOMM multiplexer command.
#[derive(Debug, PartialEq)]
pub struct MuxCommand {
    /// The parameters associated with this MuxCommand - see RFCOMM 4.3 for the supported commands.
    pub params: MuxCommandParams,

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
        let command_type = type_field.command_type()?;

        // Parse the Length Field from the buffer. There can be multiple octets.
        // This implementation supports lengths that can fit into a u64 (8 octets) - in practice,
        // most lengths should fit in 1 octet. The Test Command is the only variable-length command.
        const MAX_LENGTH_OCTETS: usize = 8;
        let mut length: u64 = 0;
        let mut num_length_octets: usize = 0;

        for i in MUX_COMMAND_LENGTH_IDX..buf.len() {
            let length_field = LengthField(buf[i]);
            let length_octet: u64 = length_field.length().into();
            length |= length_octet << (MUX_COMMAND_LENGTH_SHIFT * num_length_octets);
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
        let header_len = 1 + num_length_octets;
        let calculated_buf_size: u64 = header_len as u64 + length;
        if (buf.len() as u64) < calculated_buf_size {
            return Err(FrameParseError::BufferTooSmall);
        }

        let params_payload = &buf[header_len..calculated_buf_size as usize];
        let params = MuxCommandParams::decode(&command_type, params_payload)?;

        Ok(Self { params, command_response })
    }
}

impl Encodable for MuxCommand {
    /// Returns the encoded length of the command.
    /// - 1 Byte for Type field + n bytes to encode the length + `length` bytes for the payload.
    fn encoded_len(&self) -> usize {
        let length = self.params.encoded_len();
        1 + length_to_ea_format(length).len() + length
    }

    fn encode(&self, buf: &mut [u8]) -> Result<(), FrameParseError> {
        if buf.len() < self.encoded_len() {
            return Err(FrameParseError::BufferTooSmall);
        }

        // The CommandResponse bit for the type field is set according to GSM 5.4.6.2.
        let cr_bit = if self.command_response == CommandResponse::Command { true } else { false };

        // Type Field: E/A = 1 always.
        let mut type_field = TypeField(0);
        type_field.set_ea_bit(true);
        type_field.set_cr_bit(cr_bit);
        type_field.set_command_type(u8::from(&self.params.marker()));
        buf[0] = type_field.0;

        // Length Field.
        let length = self.params.encoded_len();
        let length_octets = length_to_ea_format(length);
        let length_end_idx = MUX_COMMAND_LENGTH_IDX + length_octets.len();
        buf[MUX_COMMAND_LENGTH_IDX..length_end_idx].copy_from_slice(&length_octets);

        // Payload of the MuxCommand.
        self.params.encode(&mut buf[length_end_idx..])?;

        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use matches::assert_matches;

    use self::dlc_parameter_negotiation::CreditBasedFlowHandshake;
    use self::modem_status::ModemStatusSignals;
    use self::remote_line_status::RlsError;
    use crate::rfcomm::types::DLCI;

    #[test]
    fn test_decode_mux_command_empty_buf() {
        let empty_buf = [];
        assert_matches!(MuxCommand::decode(&empty_buf[..]), Err(FrameParseError::BufferTooSmall));
    }

    #[test]
    fn test_decode_mux_command_invalid_command_type() {
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
    fn test_decode_mux_command_too_large_length() {
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
    fn test_decode_mux_command_missing_length_octets() {
        let buf = [
            0b10000001, // Type field - DLCParameterNegotiation.
            0b00000101, // Length field, Length = 2.
            0b00000010, // Length data octet 1.
                        // Missing octet 2 of length.
        ];
        assert_matches!(MuxCommand::decode(&buf[..]), Err(FrameParseError::BufferTooSmall));
    }

    #[test]
    fn test_decode_dlc_parameter_negotiation_command() {
        let buf = [
            0b10000011, // Type field - DLCParameterNegotiation, C/R = Command.
            0b00010001, // Length field - 1 octet. Length = 8.
            0b00000000, // Data Octet1: DLCI of 0 is OK.
            0b11110000, // Data Octet2: SupportedRequest.
            0b00001100, // Data Octet3: Priority = 12.
            0b00000000, // Data Octet4: Ignored.
            0b00010100, // Data Octet5: Max Frame Size Octet 1 = 20.
            0b00000000, // Data Octet6: Max Frame Size Octet 2 = 0.
            0b00000000, // Data Octet7: Ignored
            0b00000001, // Data Octet8: Initial Credits = 1.
        ];
        let expected = MuxCommand {
            params: MuxCommandParams::ParameterNegotiation(ParameterNegotiationParams {
                dlci: DLCI::try_from(0).unwrap(),
                credit_based_flow_handshake: CreditBasedFlowHandshake::SupportedRequest,
                priority: 12,
                max_frame_size: 20,
                initial_credits: 1,
            }),
            command_response: CommandResponse::Command,
        };
        assert_eq!(MuxCommand::decode(&buf[..]).unwrap(), expected);
    }

    #[test]
    fn test_decode_test_command() {
        let buf = [
            0b00100011, // Type field - Test Command, C/R = Command, E/A = 1.
            0b00001001, // Length field - 1 octet. Length = 4.
            0b00000000, // Data Octet1: Test pattern.
            0b00000000, // Data Octet2: Test pattern.
            0b00000000, // Data Octet3: Test pattern.
            0b00000000, // Data Octet4: Test pattern.
        ];
        let expected = MuxCommand {
            params: MuxCommandParams::Test(TestCommandParams {
                test_pattern: vec![0x00, 0x00, 0x00, 0x00],
            }),
            command_response: CommandResponse::Command,
        };
        assert_eq!(MuxCommand::decode(&buf[..]).unwrap(), expected);
    }

    #[test]
    fn test_decode_flow_control_on_command() {
        let buf = [
            0b10100001, // Type field - Test Command, C/R = Response.
            0b00000001, // Length field - 1 octet. Length = 0.
        ];
        let expected = MuxCommand {
            params: MuxCommandParams::FlowControlOn(FlowControlParams {}),
            command_response: CommandResponse::Response,
        };
        assert_eq!(MuxCommand::decode(&buf[..]).unwrap(), expected);
    }

    #[test]
    fn test_decode_flow_control_off_command() {
        let buf = [
            0b01100011, // Type field - Test Command, C/R = Command.
            0b00000001, // Length field - 1 octet. Length = 0.
        ];
        let expected = MuxCommand {
            params: MuxCommandParams::FlowControlOff(FlowControlParams {}),
            command_response: CommandResponse::Command,
        };
        assert_eq!(MuxCommand::decode(&buf[..]).unwrap(), expected);
    }

    #[test]
    fn test_decode_modem_status_command() {
        let buf = [
            0b11100011, // Type field - Test Command, C/R = Command.
            0b00000111, // Length field - 1 octet. Length = 3.
            0b00001111, // Data Octet1: DLCI = 3, E/A = 1, Bit2 = 1 always.
            0b00000000, // Data Octet2: Signals = 0, E/A = 0 -> Break.
            0b00000001, // Data Octet3: E/A = 1, B1 = 0 -> no break.
        ];
        let expected = MuxCommand {
            params: MuxCommandParams::ModemStatus(ModemStatusParams {
                dlci: DLCI::try_from(3).unwrap(),
                signals: ModemStatusSignals(0),
                break_value: None,
            }),
            command_response: CommandResponse::Command,
        };
        assert_eq!(MuxCommand::decode(&buf[..]).unwrap(), expected);
    }

    #[test]
    fn test_decode_not_supported_command_response() {
        let buf = [
            0b00010001, // Type field - Test Command, C/R = 0 (Response).
            0b00000011, // Length field - 1 octet. Length = 1.
            0b00000001, // Data octet 1: Command Type = 0 (unknown).
        ];
        let expected = MuxCommand {
            params: MuxCommandParams::NonSupported(NonSupportedCommandParams {
                cr_bit: false,
                non_supported_command: 0,
            }),
            command_response: CommandResponse::Response,
        };
        assert_eq!(MuxCommand::decode(&buf[..]).unwrap(), expected);
    }

    #[test]
    fn test_decode_rpn_command() {
        let buf = [
            0b10010011, // Type field - Test Command, C/R = Command.
            0b00000011, // Length field - 1 octet. Length = 1.
            0b00011111, // Data octet1: DLCI = 7 is OK, E/A = 1, Bit2 = 1 always.
        ];
        let expected = MuxCommand {
            params: MuxCommandParams::RemotePortNegotiation(RemotePortNegotiationParams {
                dlci: DLCI::try_from(7).unwrap(),
                port_values: None,
            }),
            command_response: CommandResponse::Command,
        };
        assert_eq!(MuxCommand::decode(&buf[..]).unwrap(), expected);
    }

    #[test]
    fn test_decode_rls_command() {
        let buf = [
            0b01010011, // Type field - Test Command, C/R = Command.
            0b00000101, // Length field - 1 octet. Length = 2.
            0b00011111, // Data octet1: DLCI = 7, E/A = 1, Bit2 = 1 always.
            0b00000000, // Data octet2: Bit1 = 0 -> No Status.
        ];
        let expected = MuxCommand {
            params: MuxCommandParams::RemoteLineStatus(RemoteLineStatusParams {
                dlci: DLCI::try_from(7).unwrap(),
                status: RlsError(0b0000),
            }),
            command_response: CommandResponse::Command,
        };
        assert_eq!(MuxCommand::decode(&buf[..]).unwrap(), expected);
    }

    #[test]
    fn test_encode_invalid_buffer() {
        let command = MuxCommand {
            params: MuxCommandParams::RemotePortNegotiation(RemotePortNegotiationParams {
                dlci: DLCI::try_from(5).unwrap(),
                port_values: None,
            }),
            command_response: CommandResponse::Command,
        };
        let mut buf = vec![];
        assert_matches!(command.encode(&mut buf[..]), Err(FrameParseError::BufferTooSmall));
    }

    #[test]
    fn test_encode_rpn_response() {
        let command = MuxCommand {
            params: MuxCommandParams::RemotePortNegotiation(RemotePortNegotiationParams {
                dlci: DLCI::try_from(5).unwrap(),
                port_values: None,
            }),
            command_response: CommandResponse::Response,
        };
        let mut buf = vec![0; command.encoded_len()];
        assert!(command.encode(&mut buf[..]).is_ok());
        let expected = vec![
            0b10010001, // CommandType = RPN, C/R = 0 (Response), E/A = 1.
            0b00000011, // Length = 1, E/A = 1.
            0b00010111, // Payload = RPN Command Params.
        ];
        assert_eq!(buf, expected);
    }

    /// Test Command is the only command with a variable payload length for the parameters.
    /// Tests encoding a Test Command with a very long payload.
    #[test]
    fn test_encode_long_test_command() {
        // 128 byte pattern - requires two octets for length.
        let test_pattern = vec![
            0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d,
            0x0e, 0x0f, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b,
            0x0c, 0x0d, 0x0e, 0x0f, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09,
            0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
            0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
            0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x00, 0x01, 0x02, 0x03,
            0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x00, 0x01,
            0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
            0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d,
            0x0e, 0x0f,
        ];
        let command = MuxCommand {
            params: MuxCommandParams::Test(TestCommandParams {
                test_pattern: test_pattern.clone(),
            }),
            command_response: CommandResponse::Command,
        };
        let mut buf = vec![0; command.encoded_len()];
        assert!(command.encode(&mut buf[..]).is_ok());
        let expected = vec![
            0b00100011, // CommandType = Test, C/R = 1 (Command), E/A = 1.
            0b00000000, // Length Octet1 = 0, E/A = 0.
            0b00000011, // Length Octet2 = 128, E/A = 1.
        ];
        let expected = vec![expected, test_pattern].concat();
        assert_eq!(buf, expected);
    }

    #[test]
    fn test_zero_length_to_ea_format() {
        let length = 0;
        let expected = [0b00000001];
        assert_eq!(length_to_ea_format(length), expected);
    }

    #[test]
    fn test_small_length_to_ea_format() {
        let small_length = 8;
        let expected = [0b00010001];
        assert_eq!(length_to_ea_format(small_length), expected);
    }

    #[test]
    fn test_two_octet_length_to_ea_format() {
        let two_octet_length = 245;
        let expected = [0b11101010, 0b00000011];
        assert_eq!(length_to_ea_format(two_octet_length), expected);
    }

    #[test]
    fn test_multi_octet_length_to_ea_format() {
        let multi_octet_length = 0b100000001100001001000;
        let expected = [0b10010000, 0b01100000, 0b10000001];
        assert_eq!(length_to_ea_format(multi_octet_length), expected);
    }
}

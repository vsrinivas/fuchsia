// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    bitfield::bitfield,
    std::convert::TryFrom,
};

use crate::pub_decodable_enum;
use crate::rfcomm::{
    frame::{
        mux_commands::{Decodable, Encodable},
        FrameParseError,
    },
    types::DLCI,
};

/// The length (in bytes) of a DLC Parameter Negotiation command.
/// Defined in GSM 5.4.6.3.1.
const DLC_PARAMETER_NEGOTIATION_LENGTH: usize = 8;

pub_decodable_enum! {
    /// The Credit Based Flow Handshake variants defined in RFCOMM Table 5.3.
    CreditBasedFlowHandshake<u8, Error> {
        Unsupported => 0x0,
        SupportedRequest => 0xF,
        SupportedResponse => 0xE,
    }
}

bitfield! {
    struct DLCParameterNegotiationFields([u8]);
    impl Debug;
    pub u8, dlci_raw, set_dlci: 5,0;
    pub u8, credit_handshake, set_credit_handshake: 15, 12;
    pub u8, priority, set_priority: 21, 16;
    pub u16, max_frame_size, set_max_frame_size: 47, 32;
    pub u8, initial_credits, set_initial_credits: 58, 56;
}

impl DLCParameterNegotiationFields<[u8; DLC_PARAMETER_NEGOTIATION_LENGTH]> {
    fn dlci(&self) -> Result<DLCI, FrameParseError> {
        DLCI::try_from(self.dlci_raw())
    }

    fn credit_based_flow_handshake(&self) -> Result<CreditBasedFlowHandshake, Error> {
        CreditBasedFlowHandshake::try_from(self.credit_handshake())
    }
}

/// The DLC Parameter Negotiation command - used to negotiate parameters for a given DLC.
/// See GSM 5.4.6.3.1 for the fields and RFCOMM 5.5.3 for modifications.
#[derive(Clone, Debug, PartialEq)]
pub struct ParameterNegotiationParams {
    pub dlci: DLCI,
    pub credit_based_flow_handshake: CreditBasedFlowHandshake,
    pub priority: u8,
    pub max_frame_size: u16,
    // TODO(58668): Update to explicit type when Credit Based Flow is implemented.
    pub initial_credits: u8,
}

impl Decodable for ParameterNegotiationParams {
    fn decode(buf: &[u8]) -> Result<Self, FrameParseError> {
        if buf.len() != DLC_PARAMETER_NEGOTIATION_LENGTH {
            return Err(FrameParseError::InvalidBufferLength(
                DLC_PARAMETER_NEGOTIATION_LENGTH,
                buf.len(),
            ));
        }

        let mut fixed_buf = [0; DLC_PARAMETER_NEGOTIATION_LENGTH];
        fixed_buf.copy_from_slice(&buf[..]);
        let parameters = DLCParameterNegotiationFields(fixed_buf);

        let dlci = parameters.dlci()?;
        let credit_based_flow_handshake =
            parameters.credit_based_flow_handshake().or(Err(FrameParseError::InvalidFrame))?;
        let priority = parameters.priority();
        let max_frame_size = parameters.max_frame_size();
        let initial_credits = parameters.initial_credits();

        Ok(ParameterNegotiationParams {
            dlci,
            credit_based_flow_handshake,
            priority,
            max_frame_size,
            initial_credits,
        })
    }
}

impl Encodable for ParameterNegotiationParams {
    fn encoded_len(&self) -> usize {
        DLC_PARAMETER_NEGOTIATION_LENGTH
    }

    fn encode(&self, buf: &mut [u8]) -> Result<(), FrameParseError> {
        if buf.len() < self.encoded_len() {
            return Err(FrameParseError::BufferTooSmall);
        }

        let mut params = DLCParameterNegotiationFields([0; DLC_PARAMETER_NEGOTIATION_LENGTH]);
        params.set_dlci(u8::from(self.dlci));
        params.set_credit_handshake(u8::from(&self.credit_based_flow_handshake));
        params.set_priority(self.priority);
        params.set_max_frame_size(self.max_frame_size);
        params.set_initial_credits(self.initial_credits);

        let params_bytes = params.0;
        buf.copy_from_slice(&params_bytes[..]);

        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use matches::assert_matches;

    #[test]
    fn test_parse_too_small_buf() {
        let buf = [0x00, 0x00, 0x00]; // Too small.
        assert_matches!(
            ParameterNegotiationParams::decode(&buf[..]),
            Err(FrameParseError::InvalidBufferLength(DLC_PARAMETER_NEGOTIATION_LENGTH, 3))
        );
    }

    #[test]
    fn test_parse_too_large_buf() {
        let buf = [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]; // Too large.
        assert!(ParameterNegotiationParams::decode(&buf[..]).is_err());
    }

    #[test]
    fn test_parse_invalid_dlci() {
        let buf = [
            0b00000001, // DLCI of 1 is invalid.
            0b11110000, // SupportedRequest.
            0b00000010, // Priority = 2.
            0b00000000, // Ignored.
            0b00000010, // Max Frame Size Octet 1 = 4.
            0b00000000, // Max Frame Size Octet 2 = 0.
            0b00000000, // Ignored
            0b00000001, // Initial Credits = 1.
        ];
        assert_matches!(
            ParameterNegotiationParams::decode(&buf[..]),
            Err(FrameParseError::InvalidDLCI(1))
        );
    }

    #[test]
    fn test_parse_invalid_credit_handshake() {
        let buf = [
            0b00000000, // DLCI of 0 is OK.
            0b10010000, // Invalid handshake value.
            0b00000010, // Priority = 2.
            0b00000000, // Ignored.
            0b00000010, // Max Frame Size Octet 1 = 4.
            0b00000000, // Max Frame Size Octet 2 = 0.
            0b00000000, // Ignored
            0b00000001, // Initial Credits = 1.
        ];
        assert_matches!(
            ParameterNegotiationParams::decode(&buf[..]),
            Err(FrameParseError::InvalidFrame)
        );
    }

    #[test]
    fn test_parse_valid_command() {
        let buf = [
            0b00000000, // DLCI of 0 is OK.
            0b11110000, // SupportedRequest.
            0b00001000, // Priority = 8.
            0b00000000, // Ignored.
            0b00000100, // Max Frame Size Octet 1 = 4.
            0b00000000, // Max Frame Size Octet 2 = 0.
            0b00000000, // Ignored
            0b00000001, // Initial Credits = 1.
        ];

        let expected = ParameterNegotiationParams {
            dlci: DLCI::try_from(0).unwrap(),
            credit_based_flow_handshake: CreditBasedFlowHandshake::SupportedRequest,
            priority: 8,
            max_frame_size: 4,
            initial_credits: 1,
        };
        assert_eq!(ParameterNegotiationParams::decode(&buf[..]).unwrap(), expected);
    }

    #[test]
    fn test_parse_two_octet_frame_size_command() {
        let buf = [
            0b00000000, // DLCI of 0 is OK.
            0b11110000, // SupportedRequest.
            0b00001000, // Priority = 8.
            0b00000000, // Ignored.
            0b10000100, // Max Frame Size Octet 1 = 132.
            0b00000001, // Max Frame Size Octet 2 = 256.
            0b00011100, // Ignored.
            0b00001100, // Initial Credits = 4, stray bit should be ignored.
        ];

        let expected = ParameterNegotiationParams {
            dlci: DLCI::try_from(0).unwrap(),
            credit_based_flow_handshake: CreditBasedFlowHandshake::SupportedRequest,
            priority: 8,
            max_frame_size: 388,
            initial_credits: 4,
        };
        assert_eq!(ParameterNegotiationParams::decode(&buf[..]).unwrap(), expected);
    }

    #[test]
    fn test_encode_invalid_buf_error() {
        let mut small_buf = [];
        let dlc_pn_command = ParameterNegotiationParams {
            dlci: DLCI::try_from(0).unwrap(),
            credit_based_flow_handshake: CreditBasedFlowHandshake::SupportedRequest,
            priority: 8,
            max_frame_size: 260,
            initial_credits: 4,
        };
        assert_matches!(
            dlc_pn_command.encode(&mut small_buf[..]),
            Err(FrameParseError::BufferTooSmall)
        );
    }

    #[test]
    fn test_encode_command_success() {
        let dlc_pn_command = ParameterNegotiationParams {
            dlci: DLCI::try_from(5).unwrap(),
            credit_based_flow_handshake: CreditBasedFlowHandshake::SupportedRequest,
            priority: 8,
            max_frame_size: 260,
            initial_credits: 6,
        };
        let mut buf = vec![0; dlc_pn_command.encoded_len()];

        assert!(dlc_pn_command.encode(&mut buf[..]).is_ok());
        let expected_buf = [
            0b00000101, // DLCI of 5 is OK.
            0b11110000, // SupportedRequest.
            0b00001000, // Priority = 8.
            0b00000000, // Ignored.
            0b00000100, // Max Frame Size Octet 1 = 4.
            0b00000001, // Max Frame Size Octet 2 = 256.
            0b00000000, // Ignored
            0b00000110, // Initial Credits = 6.
        ];
        assert_eq!(buf, expected_buf);
    }
}

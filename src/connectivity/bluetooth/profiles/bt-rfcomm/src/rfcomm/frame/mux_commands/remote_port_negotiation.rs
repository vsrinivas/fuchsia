// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {bitfield::bitfield, std::convert::TryFrom};

use crate::rfcomm::{
    frame::{
        mux_commands::{Decodable, Encodable},
        FrameParseError,
    },
    types::DLCI,
};

/// The length (in bytes) of a "short" RPN command - contains only 1 octet for the DLCI.
/// A short RPN command is used to request the remote port settings.
const REMOTE_PORT_NEGOTIATION_SHORT_LENGTH: usize = 1;

/// The length (in bytes) of a "long" RPN command containing 1 octet for the DLCI
/// and 7 octets for the parameter values.
/// A long RPN command is used to negotiate the remote port settings.
const REMOTE_PORT_NEGOTIATION_LONG_LENGTH: usize = 8;

/// The length (in bytes) of the values associated with an RPN command.
/// Defined in GSM 5.4.6.3.9 Table 11.
const REMOTE_PORT_NEGOTIATION_VALUES_LENGTH: usize = 7;

bitfield! {
    struct RPNAddressField(u8);
    impl Debug;
    pub bool, ea_bit, set_ea_bit: 0;
    pub bool, cr_bit, set_cr_bit: 1;
    pub u8, dlci_raw, set_dlci: 7, 2;
}

impl RPNAddressField {
    fn dlci(&self) -> Result<DLCI, FrameParseError> {
        DLCI::try_from(self.dlci_raw())
    }
}

// TODO(59582): Per RFCOMM 5.5.1, the handling of the command is implementation specific. It may be
// beneficial to add better accessors & strong types for some of these fields if we plan on
// supporting these customizations in the future.
bitfield! {
    struct PortValues([u8]);
    impl Debug;
    pub u8, baud_rate, _: 7, 0;
    pub u8, data_bits, _: 9, 8;
    pub bool, stop_bit, _: 10;
    pub bool, parity, _: 11;
    pub u8, parity_type, _: 13, 12;
    pub u8, flow_control, _: 21, 16;
    pub u8, xon_character, _: 31, 24;
    pub u8, xoff_character, _: 39, 32;
    pub u16, mask, _: 55, 40;
}

impl PartialEq for PortValues<[u8; REMOTE_PORT_NEGOTIATION_VALUES_LENGTH]> {
    fn eq(&self, other: &Self) -> bool {
        self.0 == other.0
    }
}

/// The Remote Port Negotiation (RPN) command is used whenever the port settings change.
/// Defined in GSM 5.4.6.3.9, with RFCOMM notes in RFCOMM 5.5.1.
#[derive(Debug, PartialEq)]
struct RemotePortNegotiationCommand {
    dlci: DLCI,
    /// The optional port values to be used when negotiation the parameters.
    port_values: Option<PortValues<[u8; REMOTE_PORT_NEGOTIATION_VALUES_LENGTH]>>,
}

impl Decodable for RemotePortNegotiationCommand {
    fn decode(buf: &[u8]) -> Result<Self, FrameParseError> {
        if buf.len() != REMOTE_PORT_NEGOTIATION_SHORT_LENGTH
            && buf.len() != REMOTE_PORT_NEGOTIATION_LONG_LENGTH
        {
            return Err(FrameParseError::InvalidBufferLength(
                REMOTE_PORT_NEGOTIATION_SHORT_LENGTH,
                buf.len(),
            ));
        }

        // Address field.
        let address_field = RPNAddressField(buf[0]);
        let dlci = address_field.dlci()?;

        // Port Values field.
        let port_values = if buf.len() == REMOTE_PORT_NEGOTIATION_SHORT_LENGTH {
            None
        } else {
            let mut b = [0; REMOTE_PORT_NEGOTIATION_VALUES_LENGTH];
            b.copy_from_slice(&buf[1..REMOTE_PORT_NEGOTIATION_LONG_LENGTH]);
            Some(PortValues(b))
        };

        Ok(RemotePortNegotiationCommand { dlci, port_values })
    }
}

impl Encodable for RemotePortNegotiationCommand {
    fn encoded_len(&self) -> usize {
        if self.port_values.is_some() {
            REMOTE_PORT_NEGOTIATION_LONG_LENGTH
        } else {
            REMOTE_PORT_NEGOTIATION_SHORT_LENGTH
        }
    }

    fn encode(&self, buf: &mut [u8]) -> Result<(), FrameParseError> {
        if buf.len() < self.encoded_len() {
            return Err(FrameParseError::BufferTooSmall);
        }

        // E/A bit = 1, C/R bit = 1. See GSM 5.4.6.3.9 Table 10.
        let mut address_field = RPNAddressField(0);
        address_field.set_ea_bit(true);
        address_field.set_cr_bit(true);
        address_field.set_dlci(u8::from(self.dlci));
        buf[0] = address_field.0;

        if let Some(port_values) = &self.port_values {
            buf[1..REMOTE_PORT_NEGOTIATION_LONG_LENGTH].copy_from_slice(&port_values.0);
        }

        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use matches::assert_matches;

    #[test]
    fn test_decode_rpn_invalid_buf() {
        let buf = [0x00, 0x01, 0x02]; // Length = 3, invalid.
        assert_matches!(
            RemotePortNegotiationCommand::decode(&buf[..]),
            Err(FrameParseError::InvalidBufferLength(REMOTE_PORT_NEGOTIATION_SHORT_LENGTH, 3))
        );
    }

    #[test]
    fn test_decode_rpn_invalid_dlci() {
        let buf = [
            0b00000111, // DLCI = 1 is invalid, E/A = 1, Bit2 = 1 always.
        ];
        assert_matches!(
            RemotePortNegotiationCommand::decode(&buf[..]),
            Err(FrameParseError::InvalidDLCI(1))
        );
    }

    #[test]
    fn test_decode_rpn_short_command() {
        let buf = [
            0b00011111, // DLCI = 7 is OK, E/A = 1, Bit2 = 1 always.
        ];
        let expected =
            RemotePortNegotiationCommand { dlci: DLCI::try_from(7).unwrap(), port_values: None };
        assert_eq!(RemotePortNegotiationCommand::decode(&buf[..]).unwrap(), expected);
    }

    #[test]
    fn test_decode_rpn_long_command() {
        let port_values_buf: [u8; REMOTE_PORT_NEGOTIATION_VALUES_LENGTH] = [
            0b00000001, // Octet 1: Baud Rate = 1.
            0b00101011, // Octet 2: Data Bits = 0b11, Stop = false, Parity = true, Type = 0b10.
            0b00011010, // Octet 3: Flow Control = 26.
            0b00000001, // Octet 4: XOn = 1.
            0b00000011, // Octet 5: XOff = 3.
            0b10000001, // Octet 6: Mask = 385 (Octet1 = 129).
            0b00000001, // Octet 7: Mask = 385 (Octet2 = 256).
        ];
        let buf = vec![
            0b00011111, // DLCI = 7 is OK, E/A = 1, Bit2 = 1 always.
        ];
        let buf = [buf, port_values_buf.to_vec()].concat();
        let expected = RemotePortNegotiationCommand {
            dlci: DLCI::try_from(7).unwrap(),
            port_values: Some(PortValues(port_values_buf)),
        };
        let decoded = RemotePortNegotiationCommand::decode(&buf[..]).unwrap();
        assert_eq!(decoded, expected);

        let decoded_port_values = decoded.port_values.unwrap();
        assert_eq!(decoded_port_values.baud_rate(), 1);
        assert_eq!(decoded_port_values.data_bits(), 0b11);
        assert_eq!(decoded_port_values.stop_bit(), false);
        assert_eq!(decoded_port_values.parity(), true);
        assert_eq!(decoded_port_values.parity_type(), 2);
        assert_eq!(decoded_port_values.flow_control(), 26);
        assert_eq!(decoded_port_values.xon_character(), 1);
        assert_eq!(decoded_port_values.xoff_character(), 3);
        assert_eq!(decoded_port_values.mask(), 385);
    }

    #[test]
    fn test_encode_rpn_invalid_buf() {
        let port_values_buf: [u8; REMOTE_PORT_NEGOTIATION_VALUES_LENGTH] =
            [0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000];
        let command = RemotePortNegotiationCommand {
            dlci: DLCI::try_from(7).unwrap(),
            port_values: Some(PortValues(port_values_buf)),
        };
        let mut buf = [];
        assert_matches!(command.encode(&mut buf), Err(FrameParseError::BufferTooSmall));
    }

    #[test]
    fn test_encode_rpn_short_command() {
        let command =
            RemotePortNegotiationCommand { dlci: DLCI::try_from(2).unwrap(), port_values: None };
        let mut buf = vec![0; command.encoded_len()];
        let expected = [0b00001011]; // DLCI = 2.
        assert!(command.encode(&mut buf).is_ok());
        assert_eq!(buf, expected);
    }

    #[test]
    fn test_encode_rpn_long_command() {
        let port_values_buf: [u8; REMOTE_PORT_NEGOTIATION_VALUES_LENGTH] = [
            0b00010000, // Octet 1: Baud Rate = 16.
            0b00111100, // Octet 2: Data Bits = 0b00, Stop = true, Parity = true, Type = 0b11.
            0b00000010, // Octet 3: Flow Control = 2.
            0b00000001, // Octet 4: XOn = 1.
            0b00000011, // Octet 5: XOff = 3.
            0b00000000, // Octet 6: Mask = 0 (Octet1 = 0).
            0b00000000, // Octet 7: Mask = 0 (Octet2 = 0).
        ];
        let command = RemotePortNegotiationCommand {
            dlci: DLCI::try_from(9).unwrap(),
            port_values: Some(PortValues(port_values_buf)),
        };
        let mut buf = vec![0; command.encoded_len()];
        assert!(command.encode(&mut buf).is_ok());

        let expected = vec![0b00100111]; // DLCI = 9.
        let expected = [expected, port_values_buf.to_vec()].concat();
        assert_eq!(buf, expected);
    }
}

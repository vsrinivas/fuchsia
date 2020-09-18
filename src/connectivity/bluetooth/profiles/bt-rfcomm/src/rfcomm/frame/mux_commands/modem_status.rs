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

/// Length (in bytes) of a Modem Status Command with no break value.
const MODEM_STATUS_COMMAND_WITHOUT_BREAK_LENGTH: usize = 2;

/// Length (in bytes) of a Modem Status Command with break value.
const MODEM_STATUS_COMMAND_WITH_BREAK_LENGTH: usize = 3;

bitfield! {
    /// The Modem Status Address field defined in GSM 5.4.6.3.7 Figure 9.
    struct ModemStatusAddressField(u8);
    impl Debug;
    bool;
    pub ea_bit, set_ea_bit: 0;
    pub cr_bit, set_cr_bit: 1;
    pub u8, dlci_raw, set_dlci: 7,2;
}

impl ModemStatusAddressField {
    fn dlci(&self) -> Result<DLCI, FrameParseError> {
        DLCI::try_from(self.dlci_raw())
    }
}

bitfield! {
    /// The Modem Status Signals defined in GSM 5.4.6.3.7 Figure 10.
    pub struct ModemStatusSignals(u8);
    impl Debug;
    bool;
    pub ea_bit, set_ea_bit: 0;
    pub flow_control, _: 1;
    pub ready_to_communicate, _: 2;
    pub ready_to_receive, _: 3;
    pub incoming_call, _: 6;
    pub data_valid, _: 7;
}

bitfield! {
    /// The Modem Status Break value defined in GSM 5.4.6.3.7 Figure 11.
    struct ModemStatusBreakField(u8);
    impl Debug;
    bool;
    pub ea_bit, set_ea_bit: 0;
    pub contains_break_value, set_contains_break_value: 1;
    pub u8, break_value, set_break_value: 7,4;
}

impl PartialEq for ModemStatusSignals {
    fn eq(&self, other: &Self) -> bool {
        self.0 == other.0
    }
}

/// Modem Status Command is used to convey V .24 control signals to the DLC.
/// Defined in GSM 6.4.6.3.7.
#[derive(Debug, PartialEq)]
pub struct ModemStatusParams {
    pub dlci: DLCI,
    pub signals: ModemStatusSignals,
    // Break signal in data stream. In units of 200ms as defined in GSM 5.4.6.3.7.
    pub break_value: Option<u8>,
}

impl Decodable for ModemStatusParams {
    fn decode(buf: &[u8]) -> Result<Self, FrameParseError> {
        if buf.len() != MODEM_STATUS_COMMAND_WITH_BREAK_LENGTH
            && buf.len() != MODEM_STATUS_COMMAND_WITHOUT_BREAK_LENGTH
        {
            return Err(FrameParseError::InvalidBufferLength(
                MODEM_STATUS_COMMAND_WITH_BREAK_LENGTH,
                buf.len(),
            ));
        }

        // Address field.
        let address_field = ModemStatusAddressField(buf[0]);
        let dlci = address_field.dlci()?;

        // Signals field.
        let signals = ModemStatusSignals(buf[1]);

        // Optional Break Value field.
        let mut break_value = None;
        if buf.len() == MODEM_STATUS_COMMAND_WITH_BREAK_LENGTH {
            let value = ModemStatusBreakField(buf[2]);
            if value.contains_break_value() {
                break_value = Some(value.break_value());
            }
        }

        Ok(Self { dlci, signals, break_value })
    }
}

impl Encodable for ModemStatusParams {
    fn encoded_len(&self) -> usize {
        if self.break_value.is_some() {
            MODEM_STATUS_COMMAND_WITH_BREAK_LENGTH
        } else {
            MODEM_STATUS_COMMAND_WITHOUT_BREAK_LENGTH
        }
    }

    fn encode(&self, buf: &mut [u8]) -> Result<(), FrameParseError> {
        if buf.len() < self.encoded_len() {
            return Err(FrameParseError::BufferTooSmall);
        }

        // Address field. E/A bit = 1, C/R bit = 1. See GSM 5.4.6.3.7 Figure 9.
        let mut address_field = ModemStatusAddressField(0);
        address_field.set_ea_bit(true);
        address_field.set_cr_bit(true);
        address_field.set_dlci(u8::from(self.dlci));
        buf[0] = address_field.0;

        // Status Signals. E/A = 1 if no break value.
        let mut status_signals_field = ModemStatusSignals(self.signals.0);
        let ea_bit = self.break_value.is_none();
        status_signals_field.set_ea_bit(ea_bit);
        buf[1] = status_signals_field.0;

        // (Optional) Break signal. E/A = 1 since last octet. Set break value.
        if self.break_value.is_some() {
            let mut break_value_field = ModemStatusBreakField(0);
            break_value_field.set_ea_bit(true);
            break_value_field.set_contains_break_value(true);
            break_value_field.set_break_value(self.break_value.unwrap());
            buf[2] = break_value_field.0;
        }

        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use matches::assert_matches;

    #[test]
    fn test_decode_modem_status_invalid_buf() {
        let buf = [];
        assert_matches!(
            ModemStatusParams::decode(&buf[..]),
            Err(FrameParseError::InvalidBufferLength(MODEM_STATUS_COMMAND_WITH_BREAK_LENGTH, 0))
        );
    }

    #[test]
    fn test_decode_modem_status_invalid_dlci() {
        let buf = [
            0b00000111, // DLCI = 1 is invalid, E/A = 1, Bit2 = 1 always.
            0b00000001, // Signals = 0, E/A = 1 -> No break.
        ];
        assert_matches!(ModemStatusParams::decode(&buf[..]), Err(FrameParseError::InvalidDLCI(1)));
    }

    #[test]
    fn test_decode_modem_status_without_break_value() {
        let buf = [
            0b00001111, // DLCI = 3, E/A = 1, Bit2 = 1 always.
            0b00000001, // Signals = 0, E/A = 1 -> No break.
        ];
        let expected = ModemStatusParams {
            dlci: DLCI::try_from(3).unwrap(),
            signals: ModemStatusSignals(1),
            break_value: None,
        };
        assert_eq!(ModemStatusParams::decode(&buf[..]).unwrap(), expected);
    }

    #[test]
    fn test_decode_modem_status_with_empty_break_value() {
        let buf = [
            0b00001111, // DLCI = 3, E/A = 1, Bit2 = 1 always.
            0b00000000, // Signals = 0, E/A = 0 -> Break.
            0b00000001, // E/A = 1, B1 = 0 -> no break.
        ];
        let expected = ModemStatusParams {
            dlci: DLCI::try_from(3).unwrap(),
            signals: ModemStatusSignals(0),
            break_value: None,
        };
        assert_eq!(ModemStatusParams::decode(&buf[..]).unwrap(), expected);
    }

    #[test]
    fn test_decode_modem_status_with_break_value() {
        let buf = [
            0b00011111, // DLCI = 7, E/A = 1, Bit2 = 1 always.
            0b11000011, // Signals set, E/A = 0 -> Break.
            0b10100011, // E/A = 1, B1 = 1 -> Break = 10.
        ];
        let expected = ModemStatusParams {
            dlci: DLCI::try_from(7).unwrap(),
            signals: ModemStatusSignals(195),
            break_value: Some(10),
        };
        assert_eq!(ModemStatusParams::decode(&buf[..]).unwrap(), expected);
    }

    #[test]
    fn test_encode_modem_status_invalid_buf() {
        let mut buf = [];
        let command = ModemStatusParams {
            dlci: DLCI::try_from(5).unwrap(),
            signals: ModemStatusSignals(100),
            break_value: Some(11),
        };
        assert_matches!(command.encode(&mut buf[..]), Err(FrameParseError::BufferTooSmall));
    }

    #[test]
    fn test_encode_modem_status_no_break_signal() {
        let mut buf = [0; 2];
        let command = ModemStatusParams {
            dlci: DLCI::try_from(5).unwrap(),
            signals: ModemStatusSignals(1),
            break_value: None,
        };
        let expected = [
            0b00010111, // DLCI = 5, E/A = 1, Bit2 = 1 always.
            0b00000001, // Signals = 0, E/A = 1 -> No break.
        ];
        assert!(command.encode(&mut buf[..]).is_ok());
        assert_eq!(buf, expected);
    }

    #[test]
    fn test_encode_modem_status_with_break_signal() {
        let mut buf = [0; 3];
        let command = ModemStatusParams {
            dlci: DLCI::try_from(6).unwrap(),
            signals: ModemStatusSignals(201),
            break_value: Some(3),
        };
        let expected = [
            0b00011011, // DLCI = 6, E/A = 1, Bit2 = 1 always.
            0b11001000, // Signals = 0, E/A = 0 -> Break value.
            0b00110011, // E/A = 1, B1 = 1 -> Break = 3.
        ];
        assert!(command.encode(&mut buf[..]).is_ok());
        assert_eq!(buf, expected);
    }
}

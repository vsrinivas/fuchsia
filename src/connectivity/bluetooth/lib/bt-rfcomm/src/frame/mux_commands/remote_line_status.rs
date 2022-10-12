// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use bitfield::bitfield;
use packet_encoding::{decodable_enum, Decodable, Encodable};
use std::convert::TryFrom;

use crate::frame::FrameParseError;
use crate::DLCI;

/// The length (in bytes) of the RLS command.
/// Defined in GSM 7.10 Section 5.4.6.3.10.
const REMOTE_LINE_STATUS_COMMAND_LENGTH: usize = 2;

bitfield! {
    struct RlsAddressField(u8);
    impl Debug;
    pub bool, ea_bit, set_ea_bit: 0;
    pub bool, cr_bit, set_cr_bit: 1;
    pub u8, dlci_raw, set_dlci: 7, 2;
}

impl RlsAddressField {
    fn dlci(&self) -> Result<DLCI, FrameParseError> {
        DLCI::try_from(self.dlci_raw())
    }
}

decodable_enum! {
    /// The error types supported in the Remote Line Status command.
    /// See GSM 07.10 Section 5.4.6.3.10 for the defined variants.
    pub enum RlsError<u8, FrameParseError, OutOfRange> {
        /// Received character overwrote an unread character.
        Overrun = 0b001,
        /// Received character's parity was incorrect.
        Parity = 0b010,
        /// Received character did not terminate with a stop bit.
        Framing = 0b100,
    }
}

bitfield! {
    pub struct RlsErrorField(u8);
    impl Debug;
    pub bool, error_occurred, set_error_occurred: 0;
    pub u8, error, set_error: 3,1;
}

impl RlsErrorField {
    fn from_error(error: RlsError) -> Self {
        let mut field = Self(0);
        field.set_error_occurred(true);
        field.set_error(u8::from(&error));
        field
    }

    const fn no_error() -> Self {
        Self(0)
    }
}

impl PartialEq for RlsErrorField {
    fn eq(&self, other: &Self) -> bool {
        self.0 == other.0
    }
}

impl Clone for RlsErrorField {
    fn clone(&self) -> Self {
        Self(self.0)
    }
}

/// The Remote Line Status Command is used to indicate the status of the Remote Port Line.
/// It is used whenever the port settings change.
/// Defined in GSM 7.10 Section 5.4.6.3.10, with RFCOMM specifics in RFCOMM 5.5.2.
#[derive(Clone, Debug, PartialEq)]
pub struct RemoteLineStatusParams {
    pub dlci: DLCI,
    /// The status associated with the remote port line.
    pub status: RlsErrorField,
}

impl RemoteLineStatusParams {
    pub fn new(dlci: DLCI, status: Option<RlsError>) -> Self {
        let status = if let Some(e) = status {
            RlsErrorField::from_error(e)
        } else {
            RlsErrorField::no_error()
        };
        Self { dlci, status }
    }
}

impl Decodable for RemoteLineStatusParams {
    type Error = FrameParseError;

    fn decode(buf: &[u8]) -> Result<Self, FrameParseError> {
        if buf.len() != REMOTE_LINE_STATUS_COMMAND_LENGTH {
            return Err(FrameParseError::InvalidBufferLength(
                REMOTE_LINE_STATUS_COMMAND_LENGTH,
                buf.len(),
            ));
        }

        // Address field.
        let address_field = RlsAddressField(buf[0]);
        let dlci = address_field.dlci()?;

        // Status field.
        let status = RlsErrorField(buf[1]);

        Ok(RemoteLineStatusParams { dlci, status })
    }
}

impl Encodable for RemoteLineStatusParams {
    type Error = FrameParseError;

    fn encoded_len(&self) -> usize {
        REMOTE_LINE_STATUS_COMMAND_LENGTH
    }

    fn encode(&self, buf: &mut [u8]) -> Result<(), FrameParseError> {
        if buf.len() < self.encoded_len() {
            return Err(FrameParseError::BufferTooSmall);
        }

        // E/A bit = 1, C/R bit = 1 (always). See GSM 7.10 Section 5.4.6.3.10 Table 14.
        let mut address_field = RlsAddressField(0);
        address_field.set_ea_bit(true);
        address_field.set_cr_bit(true);
        address_field.set_dlci(u8::from(self.dlci));
        buf[0] = address_field.0;
        buf[1] = self.status.0;

        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use assert_matches::assert_matches;

    #[test]
    fn test_decode_rls_invalid_buf() {
        let buf = [0x00, 0x01, 0x02]; // Length = 3, invalid.
        assert_matches!(
            RemoteLineStatusParams::decode(&buf[..]),
            Err(FrameParseError::InvalidBufferLength(REMOTE_LINE_STATUS_COMMAND_LENGTH, 3))
        );
    }

    #[test]
    fn test_decode_rls_invalid_dlci() {
        let buf = [
            0b00000111, // DLCI = 1 is invalid, E/A = 1, Bit2 = 1 always.
            0b00000000, // Bit1 = 0 -> No status.
        ];
        assert_matches!(
            RemoteLineStatusParams::decode(&buf[..]),
            Err(FrameParseError::InvalidDLCI(1))
        );
    }

    #[test]
    fn test_decode_rls_no_status() {
        let buf = [
            0b00001011, // DLCI = 2, E/A = 1, Bit2 = 1 always.
            0b00000000, // Bit1 = 0 -> No status.
        ];
        let expected =
            RemoteLineStatusParams { dlci: DLCI::try_from(2).unwrap(), status: RlsErrorField(0) };
        let res = RemoteLineStatusParams::decode(&buf[..]).unwrap();
        assert_eq!(res, expected);
        assert_eq!(res.status.error_occurred(), false);
    }

    #[test]
    fn test_decode_rls_with_status() {
        let buf = [
            0b00001011, // DLCI = 2, E/A = 1, Bit2 = 1 always.
            0b00000101, // Bit1 = 1 -> Status. Status = 010 (Parity Error).
        ];
        let expected =
            RemoteLineStatusParams { dlci: DLCI::try_from(2).unwrap(), status: RlsErrorField(5) };
        let res = RemoteLineStatusParams::decode(&buf[..]).unwrap();
        assert_eq!(res, expected);
        assert_eq!(res.status.error_occurred(), true);
        assert_eq!(res.status.error(), 0b010);
    }

    #[test]
    fn test_encode_rls_invalid_buf() {
        let command = RemoteLineStatusParams::new(DLCI::try_from(7).unwrap(), None);
        let mut buf = [0x01]; // Too small.
        assert_matches!(command.encode(&mut buf), Err(FrameParseError::BufferTooSmall));
    }

    #[test]
    fn test_encode_rls_with_no_status() {
        let command = RemoteLineStatusParams::new(DLCI::try_from(7).unwrap(), None);
        let mut buf = vec![0; command.encoded_len()];
        assert!(command.encode(&mut buf).is_ok());
        let expected = [
            0b00011111, // DLCI = 7, E/A = 1, Bit2 = 1 always.
            0b00000000, // Bit1 = 0 -> No error status.
        ];
        assert_eq!(buf, expected);
    }

    #[test]
    fn test_encode_rls_with_error_status() {
        let errors = vec![RlsError::Overrun, RlsError::Parity, RlsError::Framing];
        // Bit1 = 1 indicates error status. Bits 2-4 specify the error. Bits 5-8 are reserved.
        let expected_error_bits = vec![0b00000011, 0b00000101, 0b00001001];

        for (error_status, expected_bits) in errors.into_iter().zip(expected_error_bits.into_iter())
        {
            let command =
                RemoteLineStatusParams::new(DLCI::try_from(7).unwrap(), Some(error_status));
            let mut buf = vec![0; command.encoded_len()];
            assert!(command.encode(&mut buf).is_ok());

            let mut expected = vec![
                0b00011111, // DLCI = 7, Bit2 = 1 always, E/A = 1.
            ];
            expected.push(expected_bits);
            assert_eq!(buf, expected);
        }
    }
}

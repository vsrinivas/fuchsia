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

/// The length (in bytes) of the RLS command.
/// Defined in GSM 5.4.6.3.10.
const REMOTE_LINE_STATUS_COMMAND_LENGTH: usize = 2;

bitfield! {
    struct RLSAddressField(u8);
    impl Debug;
    pub bool, ea_bit, set_ea_bit: 0;
    pub bool, cr_bit, set_cr_bit: 1;
    pub u8, dlci_raw, set_dlci: 7, 2;
}

impl RLSAddressField {
    fn dlci(&self) -> Result<DLCI, FrameParseError> {
        DLCI::try_from(self.dlci_raw())
    }
}

// TODO(fxbug.dev/59582): Handling of the RLS command is implementation specific. It may be beneficial
// to provide better accessors/types for the RlsError. However, for now, we envision simply
// acknowledging the command, in which case the fields will never be used.
bitfield! {
    pub struct RlsError(u8);
    impl Debug;
    pub bool, error_occurred, _: 0;
    pub u8, error, _: 3,1;
}

impl PartialEq for RlsError {
    fn eq(&self, other: &Self) -> bool {
        self.0 == other.0
    }
}

/// The Remote Line Status Command is used to indicate the status of the Remote Port Line.
/// It is used whenever the port settings change.
/// Defined in GSM 5.4.6.3.10, with RFCOMM specifics in RFCOMM 5.5.2.
#[derive(Debug, PartialEq)]
pub struct RemoteLineStatusParams {
    pub dlci: DLCI,
    /// The status associated with the remote port line.
    pub status: RlsError,
}

impl Decodable for RemoteLineStatusParams {
    fn decode(buf: &[u8]) -> Result<Self, FrameParseError> {
        if buf.len() != REMOTE_LINE_STATUS_COMMAND_LENGTH {
            return Err(FrameParseError::InvalidBufferLength(
                REMOTE_LINE_STATUS_COMMAND_LENGTH,
                buf.len(),
            ));
        }

        // Address field.
        let address_field = RLSAddressField(buf[0]);
        let dlci = address_field.dlci()?;

        // Status field.
        let status = RlsError(buf[1]);

        Ok(RemoteLineStatusParams { dlci, status })
    }
}

impl Encodable for RemoteLineStatusParams {
    fn encoded_len(&self) -> usize {
        REMOTE_LINE_STATUS_COMMAND_LENGTH
    }

    fn encode(&self, buf: &mut [u8]) -> Result<(), FrameParseError> {
        if buf.len() < self.encoded_len() {
            return Err(FrameParseError::BufferTooSmall);
        }

        // E/A bit = 1, C/R bit = 1 (always). See GSM 5.4.6.3.10 Table 14.
        let mut address_field = RLSAddressField(0);
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

    use matches::assert_matches;

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
            RemoteLineStatusParams { dlci: DLCI::try_from(2).unwrap(), status: RlsError(0) };
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
            RemoteLineStatusParams { dlci: DLCI::try_from(2).unwrap(), status: RlsError(5) };
        let res = RemoteLineStatusParams::decode(&buf[..]).unwrap();
        assert_eq!(res, expected);
        assert_eq!(res.status.error_occurred(), true);
        assert_eq!(res.status.error(), 0b010);
    }

    #[test]
    fn test_encode_rls_invalid_buf() {
        let command =
            RemoteLineStatusParams { dlci: DLCI::try_from(7).unwrap(), status: RlsError(0) };
        let mut buf = [0x01]; // Too small.
        assert_matches!(command.encode(&mut buf), Err(FrameParseError::BufferTooSmall));
    }

    #[test]
    fn test_encode_rls_with_no_status() {
        let command =
            RemoteLineStatusParams { dlci: DLCI::try_from(7).unwrap(), status: RlsError(0b0000) };
        let mut buf = vec![0; command.encoded_len()];
        assert!(command.encode(&mut buf).is_ok());
        let expected = [
            0b00011111, // DLCI = 7, E/A = 1, Bit2 = 1 always.
            0b00000000, // Bit1 = 0 -> No Status.
        ];
        assert_eq!(buf, expected);
    }

    #[test]
    fn test_encode_rls_with_status() {
        let command =
            RemoteLineStatusParams { dlci: DLCI::try_from(7).unwrap(), status: RlsError(0b0011) };
        let mut buf = vec![0; command.encoded_len()];
        assert!(command.encode(&mut buf).is_ok());
        let expected = [
            0b00011111, // DLCI = 7, E/A = 1, Bit2 = 1 always.
            0b00000011, // Bit1 = 1 -> Status.
        ];
        assert_eq!(buf, expected);
    }
}

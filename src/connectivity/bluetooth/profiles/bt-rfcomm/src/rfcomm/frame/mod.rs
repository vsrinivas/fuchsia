// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(49073): Remove this once the Frame object is used by the RFCOMM Session.
#![allow(unused)]
use {
    anyhow::{format_err, Error},
    bitfield::bitfield,
    log::trace,
    std::convert::TryFrom,
    thiserror::Error,
};

/// Frame Check Sequence calculations.
mod fcs;

use crate::pub_decodable_enum;
use crate::rfcomm::{
    frame::fcs::verify_fcs,
    types::{CommandResponse, Role, DLCI},
};

/// Generates an enum value where each variant can be converted into a constant in the given
/// raw_type.
#[macro_export]
macro_rules! pub_decodable_enum {
    ($(#[$meta:meta])* $name:ident<$raw_type:ty,$error_type:ident> {
        $($(#[$variant_meta:meta])* $variant:ident => $val:expr),*,
    }) => {
        $(#[$meta])*
        #[derive(Debug, Eq, Hash, PartialEq, Copy, Clone)]
        pub enum $name {
            $($(#[$variant_meta])* $variant = $val),*
        }

        $crate::tofrom_decodable_enum! {
            $name<$raw_type, $error_type> {
                $($variant => $val),*,
            }
        }
    }
}

/// A From<&$name> for $raw_type implementation and
/// TryFrom<$raw_type> for $name implementation, used by pub_decodable_enum
#[macro_export]
macro_rules! tofrom_decodable_enum {
    ($name:ident<$raw_type:ty, $error_type:ident> {
        $($variant:ident => $val:expr),*,
    }) => {
        impl From<&$name> for $raw_type {
            fn from(v: &$name) -> $raw_type {
                match v {
                    $($name::$variant => $val),*,
                }
            }
        }

        impl TryFrom<$raw_type> for $name {
            type Error = $error_type;
            fn try_from(value: $raw_type) -> std::result::Result<Self, $error_type> {
                match value {
                    $($val => Ok($name::$variant)),*,
                    _ => Err(format_err!("OutOfRange")),
                }
            }
        }
    }
}

/// Errors associated with parsing an RFCOMM Frame.
#[derive(Error, Debug, PartialEq)]
pub enum FrameParseError {
    #[error("Provided buffer is too small")]
    BufferTooSmall,
    #[error("FCS check for the Frame failed")]
    FCSCheckFailed,
    #[error("DLCI ({:?}) is invalid", .0)]
    InvalidDLCI(u8),
    #[error("Frame is invalid")]
    InvalidFrame,
    #[error("Frame type is unsupported")]
    UnsupportedFrameType,
}

pub_decodable_enum! {
    /// The type of frame provided in the Control field.
    /// The P/F bit is set to 0 for all frame types.
    /// See table 2, GSM 07.10 5.2.1.3 and RFCOMM 4.2.
    FrameType<u8, Error> {
        SetAsynchronousBalancedMode => 0b00101111,
        UnnumberedAcknowledgement => 0b01100011,
        DisconnectedMode => 0b00001111,
        Disconnect => 0b01000011,
        UnnumberedInfoHeaderCheck => 0b11101111,
    }
}

impl FrameType {
    /// Returns true if the frame type is a valid multiplexer start-up frame.
    //
    /// Valid multiplexer start-up frames are the only frames which are allowed to be
    /// sent before the multiplexer starts, and must be sent over the Mux Control Channel.
    fn is_mux_startup_frame(&self, dlci: &DLCI) -> bool {
        dlci.is_mux_control()
            && (*self == FrameType::SetAsynchronousBalancedMode
                || *self == FrameType::UnnumberedAcknowledgement
                || *self == FrameType::DisconnectedMode)
    }

    /// Returns the number of octets needed when calculating the FCS.
    fn fcs_octets(&self) -> usize {
        // For UIH frames, the first 2 bytes of the buffer are used to calculate the FCS.
        // Otherwise, the first 3. Defined in RFCOMM 5.1.1.
        if *self == FrameType::UnnumberedInfoHeaderCheck {
            2
        } else {
            3
        }
    }
}

/// The minimum frame size (bytes) for an RFCOMM Frame - Address, Control, Length, FCS.
/// See RFCOMM 5.1.
const MIN_FRAME_SIZE: usize = 4;

/// The Address field is the first byte in the frame. See GSM 5.2.1.2.
const FRAME_ADDRESS_IDX: usize = 0;
bitfield! {
    pub struct AddressField(u8);
    impl Debug;
    pub bool, ea_bit, set_ea_bit: 0;
    pub bool, cr_bit, set_cr_bit: 1;
    pub u8, dlci_raw, set_dlci: 7, 2;
}

impl AddressField {
    fn dlci(&self) -> Result<DLCI, FrameParseError> {
        DLCI::try_from(self.dlci_raw())
    }
}

/// The Control field is the second byte in the frame. See GSM 5.2.1.3.
const FRAME_CONTROL_IDX: usize = 1;
bitfield! {
    pub struct ControlField(u8);
    impl Debug;
    pub bool, poll_final, set_poll_final: 4;
    pub u8, frame_type_raw, set_frame_type: 7, 0;
}

impl ControlField {
    fn frame_type(&self) -> Result<FrameType, Error> {
        // The P/F bit is ignored when determining Frame Type. See RFCOMM 4.2 and GSM 5.2.1.3.
        const FRAME_TYPE_MASK: u8 = 0b11101111;
        FrameType::try_from(self.frame_type_raw() & FRAME_TYPE_MASK)
    }
}

/// The Information field is the third byte in the frame. See GSM 5.2.1.4.
const FRAME_INFORMATION_IDX: usize = 2;

/// The information field can be represented as two E/A padded octets, each 7-bits wide.
/// This shift is used to access the upper bits of a two-octet field.
/// See GSM 5.2.1.4.
const INFORMATION_SECOND_OCTET_SHIFT: usize = 7;

bitfield! {
    pub struct InformationField(u8);
    impl Debug;
    pub bool, ea_bit, set_ea_bit: 0;
    pub u8, length, set_length: 7, 1;
}

/// The highest-level unit of data that is passed around in RFCOMM.
#[derive(Debug, PartialEq)]
pub struct Frame {
    /// The role of the device associated with this frame.
    role: Role,
    /// The DLCI associated with this frame.
    dlci: DLCI,
    /// The type of this frame. See RFCOMM 4.2 for the supported frame types.
    frame_type: FrameType,
    /// The P/F bit for this frame. See RFCOMM 5.2.1 which describes the usages
    /// of the P/F bit in RFCOMM.
    poll_final: bool,
    /// Whether this frame is a Command or Response frame.
    command_response: CommandResponse,
    /// The Information Length associated with this frame. This is only applicable for
    /// UIH frames. This will be 0 for non-UIH frames.
    length: u16,
}

impl Frame {
    /// Attempts to parse the provided `buf` into a Frame.
    // TODO(58668): Take Credit Based Flow into account when parsing UIH frames.
    pub fn parse(role: Role, buf: &[u8]) -> Result<Self, FrameParseError> {
        if buf.len() < MIN_FRAME_SIZE {
            return Err(FrameParseError::BufferTooSmall);
        }

        // Parse the Address Field of the frame.
        let address_field = AddressField(buf[FRAME_ADDRESS_IDX]);
        let dlci: DLCI = address_field.dlci()?;
        let cr_bit: bool = address_field.cr_bit();

        // Parse the Control Field of the frame.
        let control_field = ControlField(buf[FRAME_CONTROL_IDX]);
        let frame_type: FrameType =
            control_field.frame_type().or(Err(FrameParseError::UnsupportedFrameType))?;
        let poll_final = control_field.poll_final();

        // If the Session multiplexer hasn't started, then the `frame_type` must be a
        // multiplexer startup frame.
        if !role.is_multiplexer_started() && !frame_type.is_mux_startup_frame(&dlci) {
            return Err(FrameParseError::InvalidFrame);
        }

        // Classify the frame as either a Command or Response depending on the role, type of frame,
        // and the C/R bit of the Address Field.
        let command_response = CommandResponse::classify(role, frame_type, cr_bit)
            .or(Err(FrameParseError::InvalidFrame))?;

        // Parse the Information field of the Frame. If the EA bit is 0, then we need to construct
        // the InformationLength using two bytes.
        let information_field = InformationField(buf[FRAME_INFORMATION_IDX]);
        let is_two_octet_length = !information_field.ea_bit();
        let mut length = information_field.length() as u16;
        if is_two_octet_length {
            let second_octet = InformationField(buf[FRAME_INFORMATION_IDX + 1]);
            length |= (second_octet.length() as u16) << INFORMATION_SECOND_OCTET_SHIFT;
        }
        trace!("Frame InformationLength is {:?}", length);

        // TODO(58668): Check for credits.

        // The header size depends on the Information Length size and the Credit Based Flow.
        // Address (1) + Control (1) + Length (1 or 2)
        // TODO(58668): Factor in credits for this calculation.
        let header_size = 2 + if is_two_octet_length { 2 } else { 1 };

        // Check the FCS before parsing the body of the packet.
        let fcs_index: usize = (header_size + length).into();
        if buf.len() <= fcs_index {
            return Err(FrameParseError::BufferTooSmall);
        }
        let fcs = buf[fcs_index];
        if !verify_fcs(fcs, &buf[..frame_type.fcs_octets()]) {
            return Err(FrameParseError::FCSCheckFailed);
        }

        // TODO(58681): Parse user data fields of Frame if UIH.

        Ok(Self { role, dlci, frame_type, poll_final, command_response, length })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use crate::rfcomm::frame::fcs::calculate_fcs;

    #[test]
    fn test_is_mux_startup_frame() {
        let control_dlci = DLCI::try_from(0).unwrap();
        let user_dlci = DLCI::try_from(5).unwrap();

        let frame_type = FrameType::SetAsynchronousBalancedMode;
        assert!(frame_type.is_mux_startup_frame(&control_dlci));
        assert!(!frame_type.is_mux_startup_frame(&user_dlci));

        let frame_type = FrameType::Disconnect;
        assert!(!frame_type.is_mux_startup_frame(&control_dlci));
        assert!(!frame_type.is_mux_startup_frame(&user_dlci));
    }

    #[test]
    fn test_parse_too_small_frame() {
        let role = Role::Unassigned;
        let buf: &[u8] = &[0x00];
        assert_eq!(Frame::parse(role, buf), Err(FrameParseError::BufferTooSmall));
    }

    #[test]
    fn test_parse_invalid_dlci() {
        let role = Role::Unassigned;
        let buf: &[u8] = &[
            0b00000101, // Address Field - EA = 1, C/R = 0, DLCI = 1.
            0b00101111, // Control Field - SABM command with P/F = 0.
            0b00000001, // Length Field - Bit0 = 1: Indicates one octet length.
            0x00,       // Random FCS.
        ];
        assert_eq!(Frame::parse(role, buf), Err(FrameParseError::InvalidDLCI(1)));
    }

    /// It's possible that a remote device sends a packet with an invalid frame.
    /// In this case, we should error gracefully.
    #[test]
    fn test_parse_invalid_frame_type() {
        let role = Role::Unassigned;
        let buf: &[u8] = &[
            0b00000001, // Address Field - EA = 1, C/R = 0, DLCI = 0.
            0b10101010, // Control Field - Invalid command with P/F = 0.
            0b00000001, // Length Field - Bit1 = 0 indicates 1 octet length.
            0x00,       // Random FCS.
        ];
        assert_eq!(Frame::parse(role, buf), Err(FrameParseError::UnsupportedFrameType));
    }

    /// It's possible that the remote peer sends a packet for a valid frame, but the session
    /// multiplexer has not started. In this case, we should error gracefully.
    #[test]
    fn test_parse_invalid_frame_type_sent_before_mux_startup() {
        let role = Role::Unassigned;
        let buf: &[u8] = &[
            0b00000001, // Address Field - EA = 1, C/R = 0, DLCI = 0.
            0b11101111, // Control Field - UnnumberedInfoHeaderCheck with P/F = 0.
            0b00000001, // Length Field - Bit1 = 0 indicates 1 octet length.
            0x00,       // Random FCS.
        ];
        assert_eq!(Frame::parse(role, buf), Err(FrameParseError::InvalidFrame));
    }

    #[test]
    fn test_parse_invalid_frame_missing_fcs() {
        let role = Role::Unassigned;
        let buf: &[u8] = &[
            0b00000011, // Address Field - EA = 1, C/R = 1, DLCI = 0.
            0b00101111, // Control Field - SABM command with P/F = 0.
            0b00000000, // Length Field - Bit1 = 0 Indicates two octet length.
            0b00000001, // Second octet of length.
                        // Missing FCS.
        ];
        assert_eq!(Frame::parse(role, buf), Err(FrameParseError::BufferTooSmall));
    }

    #[test]
    fn test_parse_valid_frame_over_mux_dlci() {
        let role = Role::Unassigned;
        let frame_type = FrameType::SetAsynchronousBalancedMode;
        let mut buf = vec![
            0b00000011, // Address Field - EA = 1, C/R = 1, DLCI = 0.
            0b00101111, // Control Field - SABM command with P/F = 0.
            0b00000001, // Length Field - Bit1 = 1 Indicates one octet length - no info.
        ];
        // Calculate the FCS and tack it on to the end.
        let fcs = calculate_fcs(&buf[..frame_type.fcs_octets()]);
        buf.push(fcs);

        let res = Frame::parse(role, &buf[..]).unwrap();
        let expected_frame = Frame {
            role,
            dlci: DLCI::try_from(0).unwrap(),
            frame_type: FrameType::SetAsynchronousBalancedMode,
            poll_final: false,
            command_response: CommandResponse::Command,
            length: 0,
        };
        assert_eq!(res, expected_frame);
    }

    #[test]
    fn test_parse_valid_frame_over_user_dlci() {
        let role = Role::Responder;
        let frame_type = FrameType::SetAsynchronousBalancedMode;
        let mut buf = vec![
            0b00001111, // Address Field - EA = 1, C/R = 1, User DLCI = 3.
            0b00101111, // Control Field - SABM command with P/F = 0.
            0b00000001, // Length Field - Bit1 = 1 Indicates one octet length - no info.
        ];
        // Calculate the FCS for the first three bytes, since non-UIH frame.
        let fcs = calculate_fcs(&buf[..frame_type.fcs_octets()]);
        buf.push(fcs);

        let res = Frame::parse(role, &buf[..]).unwrap();
        let expected_frame = Frame {
            role,
            dlci: DLCI::try_from(3).unwrap(),
            frame_type: FrameType::SetAsynchronousBalancedMode,
            poll_final: false,
            command_response: CommandResponse::Response,
            length: 0,
        };
        assert_eq!(res, expected_frame);
    }

    #[test]
    fn test_parse_frame_with_information_length_invalid_buf_size() {
        let role = Role::Responder;
        let frame_type = FrameType::UnnumberedInfoHeaderCheck;
        let mut buf = vec![
            0b00001111, // Address Field - EA = 1, C/R = 1, User DLCI = 3.
            0b11101111, // Control Field - UIH command with P/F = 0.
            0b00000111, // Length Field - Bit1 = 1 Indicates one octet length = 3.
            0b00000000, // Data octet #1 - missing octets 2,3.
        ];
        // Calculate the FCS for the first two bytes, since UIH frame.
        let fcs = calculate_fcs(&buf[..frame_type.fcs_octets()]);
        buf.push(fcs);

        assert_eq!(Frame::parse(role, &buf[..]), Err(FrameParseError::BufferTooSmall));
    }

    #[test]
    fn test_parse_valid_frame_with_information_length() {
        let role = Role::Responder;
        let frame_type = FrameType::UnnumberedInfoHeaderCheck;
        let mut buf = vec![
            0b00001111, // Address Field - EA = 1, C/R = 1, User DLCI = 3.
            0b11101111, // Control Field - UIH command with P/F = 0.
            0b00000101, // Length Field - Bit1 = 1 Indicates one octet length = 2.
            0b00000000, // Data octet #1,
            0b00000000, // Data octet #2,
        ];
        // Calculate the FCS for the first two bytes, since UIH frame.
        let fcs = calculate_fcs(&buf[..frame_type.fcs_octets()]);
        buf.push(fcs);

        let res = Frame::parse(role, &buf[..]).unwrap();
        let expected_frame = Frame {
            role,
            dlci: DLCI::try_from(3).unwrap(),
            frame_type: FrameType::UnnumberedInfoHeaderCheck,
            poll_final: false,
            command_response: CommandResponse::Response,
            length: 2,
        };
        assert_eq!(res, expected_frame);
    }

    #[test]
    fn test_parse_valid_frame_with_two_octet_information_length() {
        let role = Role::Responder;
        let frame_type = FrameType::UnnumberedInfoHeaderCheck;
        let length = 129;
        let length_data = vec![0; length];

        // Concatenate the header, `length_data` payload, and FCS.
        let buf = vec![
            0b00001111, // Address Field - EA = 1, C/R = 1, User DLCI = 3.
            0b11101111, // Control Field - UIH command with P/F = 0.
            0b00000010, // Length Field0 - E/A = 0. Length = 1.
            0b00000011, // Length Field1 - E/A = 1. Length = 128.
        ];
        // Calculate the FCS for the first two bytes, since UIH frame.
        let fcs = calculate_fcs(&buf[..frame_type.fcs_octets()]);
        let buf = [buf, length_data, vec![fcs]].concat();

        let res = Frame::parse(role, &buf[..]).unwrap();
        let expected_frame = Frame {
            role,
            dlci: DLCI::try_from(3).unwrap(),
            frame_type: FrameType::UnnumberedInfoHeaderCheck,
            poll_final: false,
            command_response: CommandResponse::Response,
            length: length as u16,
        };
        assert_eq!(res, expected_frame);
    }
}

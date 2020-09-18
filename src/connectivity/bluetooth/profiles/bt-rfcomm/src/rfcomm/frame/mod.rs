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

/// Multiplexer Commands.
mod mux_commands;

use crate::pub_decodable_enum;
use crate::rfcomm::{
    frame::{
        fcs::{calculate_fcs, verify_fcs},
        mux_commands::MuxCommand,
    },
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

/// Errors associated with parsing an RFCOMM Frame.
#[derive(Error, Debug)]
pub enum FrameParseError {
    #[error("Provided buffer is too small")]
    BufferTooSmall,
    #[error("Invalid buffer size provided. Expected: {}, Actual: {}", .0, .1)]
    InvalidBufferLength(usize, usize),
    #[error("FCS check for the Frame failed")]
    FCSCheckFailed,
    #[error("DLCI ({:?}) is invalid", .0)]
    InvalidDLCI(u8),
    #[error("Frame is invalid")]
    InvalidFrame,
    #[error("Frame type is unsupported")]
    UnsupportedFrameType,
    #[error("Mux Command type is unsupported")]
    UnsupportedMuxCommandType,
    #[error(transparent)]
    Other(#[from] anyhow::Error),
}

pub_decodable_enum! {
    /// The type of frame provided in the Control field.
    /// The P/F bit is set to 0 for all frame types.
    /// See table 2, GSM 07.10 5.2.1.3 and RFCOMM 4.2.
    FrameTypeMarker<u8, Error> {
        SetAsynchronousBalancedMode => 0b00101111,
        UnnumberedAcknowledgement => 0b01100011,
        DisconnectedMode => 0b00001111,
        Disconnect => 0b01000011,
        UnnumberedInfoHeaderCheck => 0b11101111,
    }
}

impl FrameTypeMarker {
    /// Returns true if the frame type is a valid multiplexer start-up frame.
    //
    /// Valid multiplexer start-up frames are the only frames which are allowed to be
    /// sent before the multiplexer starts, and must be sent over the Mux Control Channel.
    fn is_mux_startup_frame(&self, dlci: &DLCI) -> bool {
        dlci.is_mux_control()
            && (*self == FrameTypeMarker::SetAsynchronousBalancedMode
                || *self == FrameTypeMarker::UnnumberedAcknowledgement
                || *self == FrameTypeMarker::DisconnectedMode)
    }

    /// Returns the number of octets needed when calculating the FCS.
    fn fcs_octets(&self) -> usize {
        // For UIH frames, the first 2 bytes of the buffer are used to calculate the FCS.
        // Otherwise, the first 3. Defined in RFCOMM 5.1.1.
        if *self == FrameTypeMarker::UnnumberedInfoHeaderCheck {
            2
        } else {
            3
        }
    }

    /// Returns true if the `frame_type` is expected to contain a credit octet.
    ///
    /// `credit_based_flow` indicates whether credit flow control is enabled for the session.
    /// `poll_final` is the P/F bit associated with the Control Field of the frame.
    /// `dlci` is the DLCI associated with the frame.
    ///
    /// RFCOMM 6.5.2 describes the RFCOMM specifics for credit-based flow control. Namely,
    /// "...It does not apply to DLCI 0 or to non-UIH frames."
    fn has_credit_octet(&self, credit_based_flow: bool, poll_final: bool, dlci: DLCI) -> bool {
        *self == FrameTypeMarker::UnnumberedInfoHeaderCheck
            && !dlci.is_mux_control()
            && credit_based_flow
            && poll_final
    }
}

/// A UIH Frame that contains user data.
#[derive(Debug, PartialEq)]
pub struct UserData {
    pub information: Vec<u8>,
}

impl Decodable for UserData {
    fn decode(buf: &[u8]) -> Result<Self, FrameParseError> {
        Ok(Self { information: buf.to_vec() })
    }
}

impl Encodable for UserData {
    fn encoded_len(&self) -> usize {
        self.information.len()
    }

    fn encode(&self, buf: &mut [u8]) -> Result<(), FrameParseError> {
        if buf.len() < self.encoded_len() {
            return Err(FrameParseError::BufferTooSmall);
        }
        buf.copy_from_slice(&self.information);
        Ok(())
    }
}

/// The data associated with a UIH Frame.
#[derive(Debug, PartialEq)]
pub enum UIHData {
    /// A UIH Frame with user data.
    User(UserData),
    /// A UIH Frame with a Mux Command.
    Mux(MuxCommand),
}

impl Encodable for UIHData {
    fn encoded_len(&self) -> usize {
        match self {
            UIHData::User(data) => data.encoded_len(),
            UIHData::Mux(command) => command.encoded_len(),
        }
    }

    fn encode(&self, buf: &mut [u8]) -> Result<(), FrameParseError> {
        if buf.len() < self.encoded_len() {
            return Err(FrameParseError::BufferTooSmall);
        }

        match self {
            UIHData::User(data) => data.encode(buf),
            UIHData::Mux(command) => command.encode(buf),
        }
    }
}

/// The types of frames supported in RFCOMM.
/// See RFCOMM 4.2 for the supported frame types.
#[derive(Debug, PartialEq)]
pub enum FrameData {
    SetAsynchronousBalancedMode,
    UnnumberedAcknowledgement,
    DisconnectedMode,
    Disconnect,
    UnnumberedInfoHeaderCheck(UIHData),
}

impl FrameData {
    fn marker(&self) -> FrameTypeMarker {
        match self {
            FrameData::SetAsynchronousBalancedMode => FrameTypeMarker::SetAsynchronousBalancedMode,
            FrameData::UnnumberedAcknowledgement => FrameTypeMarker::UnnumberedAcknowledgement,
            FrameData::DisconnectedMode => FrameTypeMarker::DisconnectedMode,
            FrameData::Disconnect => FrameTypeMarker::Disconnect,
            FrameData::UnnumberedInfoHeaderCheck(_) => FrameTypeMarker::UnnumberedInfoHeaderCheck,
        }
    }

    fn decode(
        frame_type: &FrameTypeMarker,
        dlci: &DLCI,
        buf: &[u8],
    ) -> Result<Self, FrameParseError> {
        let data = match frame_type {
            FrameTypeMarker::SetAsynchronousBalancedMode => FrameData::SetAsynchronousBalancedMode,
            FrameTypeMarker::UnnumberedAcknowledgement => FrameData::UnnumberedAcknowledgement,
            FrameTypeMarker::DisconnectedMode => FrameData::DisconnectedMode,
            FrameTypeMarker::Disconnect => FrameData::Disconnect,
            FrameTypeMarker::UnnumberedInfoHeaderCheck => {
                let uih_data = if dlci.is_mux_control() {
                    UIHData::Mux(MuxCommand::decode(buf)?)
                } else {
                    UIHData::User(UserData::decode(buf)?)
                };
                FrameData::UnnumberedInfoHeaderCheck(uih_data)
            }
        };
        Ok(data)
    }
}

impl Encodable for FrameData {
    fn encoded_len(&self) -> usize {
        match self {
            FrameData::SetAsynchronousBalancedMode
            | FrameData::UnnumberedAcknowledgement
            | FrameData::DisconnectedMode
            | FrameData::Disconnect => 0,
            FrameData::UnnumberedInfoHeaderCheck(data) => data.encoded_len(),
        }
    }

    fn encode(&self, buf: &mut [u8]) -> Result<(), FrameParseError> {
        if buf.len() < self.encoded_len() {
            return Err(FrameParseError::BufferTooSmall);
        }

        match self {
            FrameData::SetAsynchronousBalancedMode
            | FrameData::UnnumberedAcknowledgement
            | FrameData::DisconnectedMode
            | FrameData::Disconnect => Ok(()),
            FrameData::UnnumberedInfoHeaderCheck(data) => data.encode(buf),
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
    fn frame_type(&self) -> Result<FrameTypeMarker, FrameParseError> {
        // The P/F bit is ignored when determining Frame Type. See RFCOMM 4.2 and GSM 5.2.1.3.
        const FRAME_TYPE_MASK: u8 = 0b11101111;
        FrameTypeMarker::try_from(self.frame_type_raw() & FRAME_TYPE_MASK)
            .or(Err(FrameParseError::UnsupportedFrameType))
    }
}

/// The Information field is the third byte in the frame. See GSM 5.2.1.4.
const FRAME_INFORMATION_IDX: usize = 2;

/// The information field can be represented as two E/A padded octets, each 7-bits wide.
/// This shift is used to access the upper bits of a two-octet field.
/// See GSM 5.2.1.4.
const INFORMATION_SECOND_OCTET_SHIFT: usize = 7;

/// The maximum length that can be represented in a single E/A padded octet.
const MAX_SINGLE_OCTET_LENGTH: usize = 127;

bitfield! {
    pub struct InformationField(u8);
    impl Debug;
    pub bool, ea_bit, set_ea_bit: 0;
    pub u8, length, set_length_inner: 7, 1;
}

impl InformationField {
    fn set_length(&mut self, length: u8) {
        // The length is only 7 bits wide.
        let mask = 0b1111111;
        self.set_length_inner(length & mask);
    }
}

/// Returns true if the provided `length` needs to be represented as 2 octets.
fn is_two_octet_length(length: usize) -> bool {
    length > MAX_SINGLE_OCTET_LENGTH
}

/// The highest-level unit of data that is passed around in RFCOMM.
#[derive(Debug, PartialEq)]
pub struct Frame {
    /// The role of the device associated with this frame.
    role: Role,
    /// The DLCI associated with this frame.
    dlci: DLCI,
    /// The data associated with this frame.
    data: FrameData,
    /// The P/F bit for this frame. See RFCOMM 5.2.1 which describes the usages
    /// of the P/F bit in RFCOMM.
    poll_final: bool,
    /// Whether this frame is a Command or Response frame.
    command_response: CommandResponse,
    /// The credits associated with this frame. Credits are only applicable to UIH frames
    /// when credit-based flow control is enabled. See RFCOMM 6.5.
    credits: Option<u8>,
}

impl Frame {
    /// Attempts to parse the provided `buf` into a Frame.
    ///
    /// `role` is the current Role of the RFCOMM Session.
    /// `credit_based_flow` indicates whether credit-based flow control is turned on for this
    /// Session.
    pub fn parse(role: Role, credit_based_flow: bool, buf: &[u8]) -> Result<Self, FrameParseError> {
        if buf.len() < MIN_FRAME_SIZE {
            return Err(FrameParseError::BufferTooSmall);
        }

        // Parse the Address Field of the frame.
        let address_field = AddressField(buf[FRAME_ADDRESS_IDX]);
        let dlci: DLCI = address_field.dlci()?;
        let cr_bit: bool = address_field.cr_bit();

        // Parse the Control Field of the frame.
        let control_field = ControlField(buf[FRAME_CONTROL_IDX]);
        let frame_type: FrameTypeMarker = control_field.frame_type()?;
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

        // The header size depends on the Information Length size and the (optional) credits octet.
        // Address (1) + Control (1) + Length (1 or 2)
        let mut header_size = 2 + if is_two_octet_length { 2 } else { 1 };
        let mut credits = None;
        if frame_type.has_credit_octet(credit_based_flow, poll_final, dlci) {
            if buf.len() < header_size {
                return Err(FrameParseError::BufferTooSmall);
            }
            credits = Some(buf[header_size]);
            header_size += 1;
        }

        // Check the FCS before parsing the body of the packet.
        let fcs_index = header_size + usize::from(length);
        if buf.len() <= fcs_index {
            return Err(FrameParseError::BufferTooSmall);
        }
        let fcs = buf[fcs_index];
        if !verify_fcs(fcs, &buf[..frame_type.fcs_octets()]) {
            return Err(FrameParseError::FCSCheckFailed);
        }

        let data = &buf[header_size..fcs_index];
        let data = FrameData::decode(&frame_type, &dlci, data)?;

        Ok(Self { role, dlci, data, poll_final, command_response, credits })
    }
}

impl Encodable for Frame {
    fn encoded_len(&self) -> usize {
        // Address + Control + FCS + (optional) Credits + 1 or 2 octets for Length + Frame data.
        3 + self.credits.map_or(0, |_| 1)
            + if is_two_octet_length(self.data.encoded_len()) { 2 } else { 1 }
            + self.data.encoded_len()
    }

    fn encode(&self, buf: &mut [u8]) -> Result<(), FrameParseError> {
        if buf.len() != self.encoded_len() {
            return Err(FrameParseError::BufferTooSmall);
        }

        // If the multiplexer has started, the C/R bit of the Address Field is set based on
        // GSM 5.2.1.2 Table 1.
        // Otherwise, the frame must be a Mux Startup frame, in which case the C/R bit is always
        // set - see GSM 5.4.4.1 and 5.4.4.2.
        let cr_bit = if self.role.is_multiplexer_started() {
            match (&self.role, &self.command_response) {
                (Role::Initiator, CommandResponse::Command)
                | (Role::Responder, CommandResponse::Response) => true,
                _ => false,
            }
        } else {
            if !self.data.marker().is_mux_startup_frame(&self.dlci) {
                return Err(FrameParseError::InvalidFrame);
            }
            true
        };

        // Set the Address Field, E/A = 1 since there is only one octet.
        let mut address_field = AddressField(0);
        address_field.set_ea_bit(true);
        address_field.set_cr_bit(cr_bit);
        address_field.set_dlci(u8::from(self.dlci));
        buf[FRAME_ADDRESS_IDX] = address_field.0;

        // Control Field.
        let mut control_field = ControlField(0);
        control_field.set_frame_type(u8::from(&self.data.marker()));
        control_field.set_poll_final(self.poll_final);
        buf[FRAME_CONTROL_IDX] = control_field.0;

        // Information Field.
        let data_length = self.data.encoded_len();
        let is_two_octet_length = is_two_octet_length(data_length);
        let mut first_octet_length = InformationField(0);
        first_octet_length.set_length(data_length as u8);
        first_octet_length.set_ea_bit(!is_two_octet_length);
        buf[FRAME_INFORMATION_IDX] = first_octet_length.0;
        // If the length is two octets, get the upper 7 bits and tag on E/A = 1.
        if is_two_octet_length {
            let mut second_octet_length = InformationField(0);
            second_octet_length.set_length((data_length >> INFORMATION_SECOND_OCTET_SHIFT) as u8);
            second_octet_length.set_ea_bit(true);
            buf[FRAME_INFORMATION_IDX + 1] = second_octet_length.0;
        }

        // Address + Control + Information.
        let mut header_size = 2 + if is_two_octet_length { 2 } else { 1 };

        // Encode the credits for this frame, if applicable.
        let credit_based_flow = self.credits.is_some();
        if self.data.marker().has_credit_octet(credit_based_flow, self.poll_final, self.dlci) {
            buf[header_size] = self.credits.unwrap();
            header_size += 1;
        }

        let fcs_idx = header_size + data_length as usize;

        // Frame data.
        self.data.encode(&mut buf[header_size..fcs_idx])?;

        // FCS that is computed based on `frame_type`.
        buf[fcs_idx] = calculate_fcs(&buf[..self.data.marker().fcs_octets()]);

        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use self::mux_commands::{MuxCommandParams, RemotePortNegotiationParams};
    use crate::rfcomm::frame::fcs::calculate_fcs;
    use matches::assert_matches;

    #[test]
    fn test_is_mux_startup_frame() {
        let control_dlci = DLCI::try_from(0).unwrap();
        let user_dlci = DLCI::try_from(5).unwrap();

        let frame_type = FrameTypeMarker::SetAsynchronousBalancedMode;
        assert!(frame_type.is_mux_startup_frame(&control_dlci));
        assert!(!frame_type.is_mux_startup_frame(&user_dlci));

        let frame_type = FrameTypeMarker::Disconnect;
        assert!(!frame_type.is_mux_startup_frame(&control_dlci));
        assert!(!frame_type.is_mux_startup_frame(&user_dlci));
    }

    #[test]
    fn test_has_credit_octet() {
        let frame_type = FrameTypeMarker::UnnumberedInfoHeaderCheck;
        let pf = true;
        let credit_based_flow = true;
        let dlci = DLCI::try_from(3).unwrap();
        assert!(frame_type.has_credit_octet(credit_based_flow, pf, dlci));

        let pf = false;
        let credit_based_flow = true;
        assert!(!frame_type.has_credit_octet(credit_based_flow, pf, dlci));

        let pf = true;
        let credit_based_flow = false;
        assert!(!frame_type.has_credit_octet(credit_based_flow, pf, dlci));

        let pf = true;
        let credit_based_flow = true;
        let dlci = DLCI::try_from(0).unwrap(); // Mux DLCI.
        assert!(!frame_type.has_credit_octet(credit_based_flow, pf, dlci));

        let pf = false;
        let credit_based_flow = false;
        assert!(!frame_type.has_credit_octet(credit_based_flow, pf, dlci));

        let frame_type = FrameTypeMarker::SetAsynchronousBalancedMode;
        let pf = true;
        let credit_based_flow = true;
        let dlci = DLCI::try_from(5).unwrap();
        assert!(!frame_type.has_credit_octet(credit_based_flow, pf, dlci));
    }

    #[test]
    fn test_parse_too_small_frame() {
        let role = Role::Unassigned;
        let buf: &[u8] = &[0x00];
        assert_matches!(Frame::parse(role, false, buf), Err(FrameParseError::BufferTooSmall));
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
        assert_matches!(Frame::parse(role, false, buf), Err(FrameParseError::InvalidDLCI(1)));
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
        assert_matches!(Frame::parse(role, false, buf), Err(FrameParseError::UnsupportedFrameType));
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
        assert_matches!(Frame::parse(role, false, buf), Err(FrameParseError::InvalidFrame));
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
        assert_matches!(Frame::parse(role, false, buf), Err(FrameParseError::BufferTooSmall));
    }

    #[test]
    fn test_parse_valid_frame_over_mux_dlci() {
        let role = Role::Unassigned;
        let frame_type = FrameTypeMarker::SetAsynchronousBalancedMode;
        let mut buf = vec![
            0b00000011, // Address Field - EA = 1, C/R = 1, DLCI = 0.
            0b00101111, // Control Field - SABM command with P/F = 0.
            0b00000001, // Length Field - Bit1 = 1 Indicates one octet length - no info.
        ];
        // Calculate the FCS and tack it on to the end.
        let fcs = calculate_fcs(&buf[..frame_type.fcs_octets()]);
        buf.push(fcs);

        let res = Frame::parse(role, false, &buf[..]).unwrap();
        let expected_frame = Frame {
            role,
            dlci: DLCI::try_from(0).unwrap(),
            data: FrameData::SetAsynchronousBalancedMode,
            poll_final: false,
            command_response: CommandResponse::Command,
            credits: None,
        };
        assert_eq!(res, expected_frame);
    }

    #[test]
    fn test_parse_valid_frame_over_user_dlci() {
        let role = Role::Responder;
        let frame_type = FrameTypeMarker::SetAsynchronousBalancedMode;
        let mut buf = vec![
            0b00001111, // Address Field - EA = 1, C/R = 1, User DLCI = 3.
            0b00101111, // Control Field - SABM command with P/F = 0.
            0b00000001, // Length Field - Bit1 = 1 Indicates one octet length - no info.
        ];
        // Calculate the FCS for the first three bytes, since non-UIH frame.
        let fcs = calculate_fcs(&buf[..frame_type.fcs_octets()]);
        buf.push(fcs);

        let res = Frame::parse(role, false, &buf[..]).unwrap();
        let expected_frame = Frame {
            role,
            dlci: DLCI::try_from(3).unwrap(),
            data: FrameData::SetAsynchronousBalancedMode,
            poll_final: false,
            command_response: CommandResponse::Response,
            credits: None,
        };
        assert_eq!(res, expected_frame);
    }

    #[test]
    fn test_parse_frame_with_information_length_invalid_buf_size() {
        let role = Role::Responder;
        let frame_type = FrameTypeMarker::UnnumberedInfoHeaderCheck;
        let mut buf = vec![
            0b00001111, // Address Field - EA = 1, C/R = 1, User DLCI = 3.
            0b11101111, // Control Field - UIH command with P/F = 0.
            0b00000111, // Length Field - Bit1 = 1 Indicates one octet length = 3.
            0b00000000, // Data octet #1 - missing octets 2,3.
        ];
        // Calculate the FCS for the first two bytes, since UIH frame.
        let fcs = calculate_fcs(&buf[..frame_type.fcs_octets()]);
        buf.push(fcs);

        assert_matches!(Frame::parse(role, false, &buf[..]), Err(FrameParseError::BufferTooSmall));
    }

    #[test]
    fn test_parse_valid_frame_with_information_length() {
        let role = Role::Responder;
        let frame_type = FrameTypeMarker::UnnumberedInfoHeaderCheck;
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

        let res = Frame::parse(role, false, &buf[..]).unwrap();
        let expected_frame = Frame {
            role,
            dlci: DLCI::try_from(3).unwrap(),
            data: FrameData::UnnumberedInfoHeaderCheck(UIHData::User(UserData {
                information: vec![
                    0b00000000, // Data octet #1.
                    0b00000000, // Data octet #2.
                ],
            })),
            poll_final: false,
            command_response: CommandResponse::Response,
            credits: None,
        };
        assert_eq!(res, expected_frame);
    }

    #[test]
    fn test_parse_valid_frame_with_two_octet_information_length() {
        let role = Role::Responder;
        let frame_type = FrameTypeMarker::UnnumberedInfoHeaderCheck;
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
        let buf = [buf, length_data.clone(), vec![fcs]].concat();

        let res = Frame::parse(role, false, &buf[..]).unwrap();
        let expected_frame = Frame {
            role,
            dlci: DLCI::try_from(3).unwrap(),
            data: FrameData::UnnumberedInfoHeaderCheck(UIHData::User(UserData {
                information: length_data,
            })),
            poll_final: false,
            command_response: CommandResponse::Response,
            credits: None,
        };
        assert_eq!(res, expected_frame);
    }

    #[test]
    fn test_parse_uih_frame_with_mux_command() {
        let role = Role::Responder;
        let frame_type = FrameTypeMarker::UnnumberedInfoHeaderCheck;
        let mut buf = vec![
            0b00000011, // Address Field - EA = 1, C/R = 1, Mux DLCI = 0.
            0b11111111, // Control Field - UIH command with P/F = 1.
            0b00000111, // Length Field - Bit1 = 1 Indicates one octet length = 3.
            0b10010001, // Data octet #1 - RPN command.
            0b00000011, // Data octet #2 - RPN Command length = 1.
            0b00011111, // Data octet #3 - RPN Data, DLCI = 7.
        ];
        // Calculate the FCS for the first two bytes, since UIH frame.
        let fcs = calculate_fcs(&buf[..frame_type.fcs_octets()]);
        buf.push(fcs);

        let res = Frame::parse(role, false, &buf[..]).unwrap();
        let expected_mux_command = MuxCommand {
            params: MuxCommandParams::RemotePortNegotiation(RemotePortNegotiationParams {
                dlci: DLCI::try_from(7).unwrap(),
                port_values: None,
            }),
            command_response: CommandResponse::Response,
        };
        let expected_frame = Frame {
            role,
            dlci: DLCI::try_from(0).unwrap(),
            data: FrameData::UnnumberedInfoHeaderCheck(UIHData::Mux(expected_mux_command)),
            poll_final: true,
            command_response: CommandResponse::Response,
            credits: None,
        };
        assert_eq!(res, expected_frame);
    }

    #[test]
    fn test_parse_uih_frame_with_credits() {
        let role = Role::Responder;
        let frame_type = FrameTypeMarker::UnnumberedInfoHeaderCheck;
        let credit_based_flow = true;
        let mut buf = vec![
            0b00011111, // Address Field - EA = 1, C/R = 1, User DLCI = 7.
            0b11111111, // Control Field - UIH command with P/F = 1.
            0b00000111, // Length Field - Bit1 = 1 Indicates one octet length = 3.
            0b00000101, // Credits Field = 5.
            0b00000000, // UserData octet #1.
            0b00000001, // UserData octet #2.
            0b00000010, // UserData octet #3.
        ];
        // Calculate the FCS for the first two bytes, since UIH frame.
        let fcs = calculate_fcs(&buf[..frame_type.fcs_octets()]);
        buf.push(fcs);

        let res = Frame::parse(role, credit_based_flow, &buf[..]).unwrap();
        let expected_user_data = UserData { information: vec![0x00, 0x01, 0x02] };
        let expected_frame = Frame {
            role,
            dlci: DLCI::try_from(7).unwrap(),
            data: FrameData::UnnumberedInfoHeaderCheck(UIHData::User(expected_user_data)),
            poll_final: true,
            command_response: CommandResponse::Response,
            credits: Some(5),
        };
        assert_eq!(res, expected_frame);
    }

    #[test]
    fn test_encode_frame_invalid_buf() {
        let frame = Frame {
            role: Role::Unassigned,
            dlci: DLCI::try_from(0).unwrap(),
            data: FrameData::SetAsynchronousBalancedMode,
            poll_final: false,
            command_response: CommandResponse::Command,
            credits: None,
        };
        let mut buf = [];
        assert_matches!(frame.encode(&mut buf[..]), Err(FrameParseError::BufferTooSmall));
    }

    /// Tests that attempting to encode a Mux Startup frame over a user DLCI is rejected.
    #[test]
    fn test_encode_mux_startup_frame_over_user_dlci_fails() {
        let frame = Frame {
            role: Role::Unassigned,
            dlci: DLCI::try_from(3).unwrap(),
            data: FrameData::SetAsynchronousBalancedMode,
            poll_final: false,
            command_response: CommandResponse::Command,
            credits: None,
        };
        let mut buf = vec![0; frame.encoded_len()];
        assert_matches!(frame.encode(&mut buf[..]), Err(FrameParseError::InvalidFrame));
    }

    #[test]
    fn test_encode_mux_startup_frame_succeeds() {
        let frame = Frame {
            role: Role::Unassigned,
            dlci: DLCI::try_from(0).unwrap(),
            data: FrameData::SetAsynchronousBalancedMode,
            poll_final: true,
            command_response: CommandResponse::Command,
            credits: None,
        };
        let mut buf = vec![0; frame.encoded_len()];
        assert!(frame.encode(&mut buf[..]).is_ok());
        let expected = vec![
            0b00000011, // Address Field: DLCI = 0, C/R = 1, E/A = 1.
            0b00111111, // Control Field: SABM, P/F = 1.
            0b00000001, // Length Field: Length = 0, E/A = 1.
            0b00011100, // FCS - precomputed.
        ];
        assert_eq!(buf, expected);
    }

    #[test]
    fn test_encode_user_data_frame_succeeds() {
        let frame = Frame {
            role: Role::Initiator,
            dlci: DLCI::try_from(3).unwrap(),
            data: FrameData::UnnumberedInfoHeaderCheck(UIHData::User(UserData {
                information: vec![
                    0b00000001, // Data octet #1.
                    0b00000010, // Data octet #2.
                ],
            })),
            poll_final: true,
            command_response: CommandResponse::Command,
            credits: Some(8),
        };
        let mut buf = vec![0; frame.encoded_len()];
        assert!(frame.encode(&mut buf[..]).is_ok());
        let expected = vec![
            0b00001111, // Address Field: DLCI = 3, C/R = 1, E/A = 1.
            0b11111111, // Control Field - UIH command with P/F = 1.
            0b00000101, // Length Field - Bit1 = 1 Indicates one octet length = 2.
            0b00001000, // Credit Field - Credits = 8.
            0b00000001, // Data octet #1.
            0b00000010, // Data octet #2.
            0b11110011, // FCS - precomputed.
        ];
        assert_eq!(buf, expected);
    }

    #[test]
    fn test_encode_mux_command_frame_succeeds() {
        let mux_command = MuxCommand {
            params: MuxCommandParams::RemotePortNegotiation(RemotePortNegotiationParams {
                dlci: DLCI::try_from(7).unwrap(),
                port_values: None,
            }),
            command_response: CommandResponse::Command,
        };
        let frame = Frame {
            role: Role::Responder,
            dlci: DLCI::try_from(0).unwrap(),
            data: FrameData::UnnumberedInfoHeaderCheck(UIHData::Mux(mux_command)),
            poll_final: false,
            command_response: CommandResponse::Command,
            credits: None,
        };
        let mut buf = vec![0; frame.encoded_len()];
        assert!(frame.encode(&mut buf[..]).is_ok());
        let expected = vec![
            0b00000001, // Address Field: DLCI = 0, C/R = 0, E/A = 1.
            0b11101111, // Control Field - UIH command with P/F = 1.
            0b00000111, // Length Field - Bit1 = 1 Indicates one octet length = 3.
            0b10010011, // Data octet #1 - RPN command, C/R = 1, E/A = 1.
            0b00000011, // Data octet #2 - RPN Command length = 1.
            0b00011111, // Data octet #3 - RPN Data, DLCI = 7.
            0b10101010, // FCS - precomputed.
        ];
        assert_eq!(buf, expected);
    }
}

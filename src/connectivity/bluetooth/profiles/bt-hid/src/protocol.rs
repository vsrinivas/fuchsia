// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use bitfield::bitfield;
use num_derive::FromPrimitive;
use num_traits::FromPrimitive;
use std::convert::TryFrom;

#[derive(Debug, PartialEq)]
pub enum MessageHeaderParseErrorType {
    // The field value was marked reserved in HFP v1.1.1.
    Reserved,
    // The field value was marked deprecated in HFP v1.1.1.
    Deprecated,
    // The field value should not be representable with the bitwidth of the field as defined in HFP v1.1.1
    Invalid,
}

#[derive(Debug, PartialEq)]
pub struct MessageHeaderParseError {
    error_type: MessageHeaderParseErrorType,
    value: u8,
    field: MessageHeaderField,
}

#[derive(Debug, PartialEq)]
pub enum MessageHeaderField {
    MessageType,
    HandshakeParameter,
    HidControlParameter,
    ReportType,
    ProtocolMode,
    DataReportType,
}

trait ParseableField: FromPrimitive {
    const FIELD: MessageHeaderField;
    const RESERVED_VALUES: &'static [u8] = &[];
    const DEPRECATED_VALUES: &'static [u8] = &[];

    fn parse_field(value: u8) -> Result<Self, MessageHeaderParseError> {
        Self::from_u8(value).ok_or_else(|| {
            if Self::RESERVED_VALUES.contains(&value) {
                MessageHeaderParseError {
                    error_type: MessageHeaderParseErrorType::Reserved,
                    value,
                    field: Self::FIELD,
                }
            } else if Self::DEPRECATED_VALUES.contains(&value) {
                MessageHeaderParseError {
                    error_type: MessageHeaderParseErrorType::Deprecated,
                    value,
                    field: Self::FIELD,
                }
            } else {
                MessageHeaderParseError {
                    error_type: MessageHeaderParseErrorType::Invalid,
                    value,
                    field: Self::FIELD,
                }
            }
        })
    }
}

// Type definitions for HIDP command headers follow.

// HIDP v1.1 Table 3.1
#[derive(Debug, FromPrimitive)]
enum MessageType {
    Handshake = 0x0,
    HidControl = 0x1,
    // Reserved 0x2 to 0x3
    GetReport = 0x4,
    SetReport = 0x5,
    GetProtocol = 0x6,
    SetProtocol = 0x7,
    // Deprecated GetIdle 0x8
    // Deprecated SetIdle 0x9
    Data = 0xa,
    // Deprecated DatC 0xb
    // Reserved 0xc to 0xf
}

impl ParseableField for MessageType {
    const FIELD: MessageHeaderField = MessageHeaderField::MessageType;
    const RESERVED_VALUES: &'static [u8] = &[0x2, 0x3, 0xc, 0xd, 0xe, 0xf];
    const DEPRECATED_VALUES: &'static [u8] = &[0x8, 0x9, 0xb];
}

// HIDP v1.1 3.1.1
bitfield! {
    struct RawMessageHeader(u8);
    u8;
    impl Debug;

    // Fields are specified MSB to LSB
    u8, message_type, set_message_type: 7, 4;
    u8, parameter, set_parameter: 3, 0;
}

// HIDP v1.1 3.1.2.1
#[derive(Clone, Copy, Debug, FromPrimitive, PartialEq)]
pub enum HandshakeParameter {
    Successful = 0x0,
    NotReady = 0x1,
    ErrInvalidReportId = 0x2,
    ErrUnsupportedRequest = 0x3,
    ErrInvalidParameter = 0x4,
    // Reserved 0x5 to 0xd
    ErrUnknown = 0xe,
    ErrFatal = 0xf,
}

impl ParseableField for HandshakeParameter {
    const FIELD: MessageHeaderField = MessageHeaderField::HandshakeParameter;
    const RESERVED_VALUES: &'static [u8] = &[0x5, 0x6, 0x7, 0x8, 0x9, 0xa, 0xb, 0xc];
    const DEPRECATED_VALUES: &'static [u8] = &[];
}

// HIDP v2.1 3.1.2.2
#[derive(Clone, Copy, Debug, FromPrimitive, PartialEq)]
pub enum HidControlParameter {
    // Deprecated Nop 0x0,
    // Deprecated Hard Reset 0x1
    // Deprecated Soft Reset 0x2
    Suspend = 0x3,
    ExitSuspend = 0x4,
    VirtualCableUnplug = 0x5,
    // Reserved 0x6 to 0xf
}

impl ParseableField for HidControlParameter {
    const FIELD: MessageHeaderField = MessageHeaderField::HidControlParameter;
    const RESERVED_VALUES: &'static [u8] = &[0x6, 0x7, 0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf];
    const DEPRECATED_VALUES: &'static [u8] = &[0x0, 0x1, 0x2];
}

// HIDP v1.1 3.1.2.3, 3.1.2.4
#[derive(Clone, Copy, Debug, FromPrimitive, PartialEq)]
pub enum ReportType {
    // Reserved 0x0
    Input = 0x1,
    Output = 0x2,
    Feature = 0x3,
}

impl ParseableField for ReportType {
    const FIELD: MessageHeaderField = MessageHeaderField::ReportType;
    const RESERVED_VALUES: &'static [u8] = &[0x0];
}

// HIDP v1.1 3.1.2.3
bitfield! {
    struct GetReportParameter(u8);
    impl Debug;

    u8, has_size, set_has_size: 3;
    // Bit 2 is reserved
    u8, report_type, set_report_type: 1, 0;
}

// HIDP v1.1 3.1.2.4
bitfield! {
    struct SetReportParameter(u8);
    impl Debug;

    // Bits 3-2 are reserved
    u8, report_type, set_report_type: 1, 0;
}

// HIDP v1.1 3.1.2.5
// GET_PROTOCOL parameter is reserved

// HIDP v1.1 3.1.2.6
#[derive(Clone, Copy, Debug, FromPrimitive, PartialEq)]
pub enum ProtocolMode {
    Boot = 0x0,
    Report = 0x1,
}

impl ParseableField for ProtocolMode {
    const FIELD: MessageHeaderField = MessageHeaderField::ProtocolMode;
}

// HIDP v1.1 3.1.2.6
bitfield! {
    struct SetProtocolParameter(u8);
    impl Debug;

    // Bits 3-1 are reserved
    u8, protocol_mode, set_protocol_mode: 0, 0;
}

// HIDP v1.1 3.1.2.9
#[derive(Clone, Copy, Debug, FromPrimitive, PartialEq)]
pub enum DataReportType {
    Other = 0x0,
    Input = 0x1,
    Output = 0x2,
    Feature = 0x3,
}

impl ParseableField for DataReportType {
    const FIELD: MessageHeaderField = MessageHeaderField::DataReportType;
}

// HIDP v1.1 3.1.2.9
bitfield! {
    struct DataParameter(u8);
    impl Debug;

    // Bits 3-2 are reserved
    u8, report_type, set_report_type: 1, 0;
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub enum MessageHeader {
    Handshake { parameter: HandshakeParameter },
    HidControl { parameter: HidControlParameter },
    GetReport { has_size: bool, report_type: ReportType },
    SetReport { report_type: ReportType },
    GetProtocol,
    SetProtocol { protocol_mode: ProtocolMode },
    Data { report_type: DataReportType },
}

impl TryFrom<u8> for MessageHeader {
    type Error = MessageHeaderParseError;

    fn try_from(byte: u8) -> Result<MessageHeader, MessageHeaderParseError> {
        let bitfield = RawMessageHeader(byte);
        let message_type = MessageType::parse_field(bitfield.message_type())?;
        let parameter = bitfield.parameter();

        let message_header = match message_type {
            MessageType::Handshake => {
                let parameter = HandshakeParameter::parse_field(parameter)?;
                Self::Handshake { parameter }
            }
            MessageType::HidControl => {
                let parameter = HidControlParameter::parse_field(parameter)?;
                Self::HidControl { parameter }
            }
            MessageType::GetReport => {
                let parameter_bitfield = GetReportParameter(parameter);
                let has_size = parameter_bitfield.has_size();
                let report_type = ReportType::parse_field(parameter_bitfield.report_type())?;
                Self::GetReport { has_size, report_type }
            }
            MessageType::SetReport => {
                let parameter_bitfield = SetReportParameter(parameter);
                let report_type = ReportType::parse_field(parameter_bitfield.report_type())?;
                Self::SetReport { report_type }
            }
            MessageType::GetProtocol => Self::GetProtocol,
            MessageType::SetProtocol => {
                let parameter_bitfield = SetProtocolParameter(parameter);
                let protocol_mode = ProtocolMode::parse_field(parameter_bitfield.protocol_mode())?;
                Self::SetProtocol { protocol_mode }
            }
            MessageType::Data => {
                let parameter_bitfield = DataParameter(parameter);
                let report_type = DataReportType::parse_field(parameter_bitfield.report_type())?;
                Self::Data { report_type }
            }
        };

        Ok(message_header)
    }
}

impl From<MessageHeader> for u8 {
    fn from(message_header: MessageHeader) -> u8 {
        let mut bitfield = RawMessageHeader(0);

        match message_header {
            MessageHeader::Handshake { parameter } => {
                bitfield.set_message_type(MessageType::Handshake as u8);
                bitfield.set_parameter(parameter as u8);
            }
            MessageHeader::HidControl { parameter } => {
                bitfield.set_message_type(MessageType::HidControl as u8);
                bitfield.set_parameter(parameter as u8);
            }
            MessageHeader::GetReport { has_size, report_type } => {
                bitfield.set_message_type(MessageType::GetReport as u8);
                let mut parameter_bitfield = GetReportParameter(0);
                parameter_bitfield.set_has_size(has_size);
                parameter_bitfield.set_report_type(report_type as u8);
                bitfield.set_parameter(parameter_bitfield.0);
            }
            MessageHeader::SetReport { report_type } => {
                bitfield.set_message_type(MessageType::SetReport as u8);
                let mut parameter_bitfield = SetReportParameter(0);
                parameter_bitfield.set_report_type(report_type as u8);
                bitfield.set_parameter(parameter_bitfield.0);
            }
            MessageHeader::GetProtocol => {
                bitfield.set_message_type(MessageType::GetProtocol as u8);
            }
            MessageHeader::SetProtocol { protocol_mode } => {
                bitfield.set_message_type(MessageType::SetProtocol as u8);
                let mut parameter_bitfield = SetProtocolParameter(0);
                parameter_bitfield.set_protocol_mode(protocol_mode as u8);
                bitfield.set_parameter(parameter_bitfield.0);
            }
            MessageHeader::Data { report_type } => {
                bitfield.set_message_type(MessageType::Data as u8);
                let mut parameter_bitfield = DataParameter(0);
                parameter_bitfield.set_report_type(report_type as u8);
                bitfield.set_parameter(parameter_bitfield.0);
            }
        };

        bitfield.0
    }
}

#[cfg(test)]
mod test {
    use super::*;

    fn test_roundtrip(byte: u8, message_header: MessageHeader) {
        let message_header_from_byte_result: Result<MessageHeader, MessageHeaderParseError> =
            MessageHeader::try_from(byte);
        assert_eq!(Ok(message_header), message_header_from_byte_result);

        let byte_from_message_header: u8 = message_header.into();
        assert_eq!(byte, byte_from_message_header);
    }

    fn test_parse_failure(byte: u8, error: MessageHeaderParseError) {
        let error_from_byte_result: Result<MessageHeader, MessageHeaderParseError> =
            MessageHeader::try_from(byte);
        assert_eq!(Err(error), error_from_byte_result);
    }

    #[fuchsia::test]
    fn reserved_message_type() {
        test_parse_failure(
            0b_0010_0000,
            MessageHeaderParseError {
                error_type: MessageHeaderParseErrorType::Reserved,
                field: MessageHeaderField::MessageType,
                value: 0b_0010,
            },
        )
    }

    #[fuchsia::test]
    fn deprecated_message_type() {
        test_parse_failure(
            0b_1000_0000,
            MessageHeaderParseError {
                error_type: MessageHeaderParseErrorType::Deprecated,
                field: MessageHeaderField::MessageType,
                value: 0b_1000,
            },
        )
    }

    #[fuchsia::test]
    fn handshake() {
        test_roundtrip(
            0b_0000_0011,
            MessageHeader::Handshake { parameter: HandshakeParameter::ErrUnsupportedRequest },
        )
    }

    #[fuchsia::test]
    fn handshake_reserved_parameter() {
        test_parse_failure(
            0b_0000_0101,
            MessageHeaderParseError {
                error_type: MessageHeaderParseErrorType::Reserved,
                field: MessageHeaderField::HandshakeParameter,
                value: 0b_0101,
            },
        )
    }

    #[fuchsia::test]
    fn hid_control() {
        test_roundtrip(
            0b_0001_0011,
            MessageHeader::HidControl { parameter: HidControlParameter::Suspend },
        )
    }

    #[fuchsia::test]
    fn hid_control_deprecated_parameter() {
        test_parse_failure(
            0b_0001_0000,
            MessageHeaderParseError {
                error_type: MessageHeaderParseErrorType::Deprecated,
                field: MessageHeaderField::HidControlParameter,
                value: 0b_0000,
            },
        )
    }

    #[fuchsia::test]
    fn hid_control_reserved_parameter() {
        test_parse_failure(
            0b_0001_0110,
            MessageHeaderParseError {
                error_type: MessageHeaderParseErrorType::Reserved,
                field: MessageHeaderField::HidControlParameter,
                value: 0b_0110,
            },
        )
    }

    #[fuchsia::test]
    fn get_report() {
        test_roundtrip(
            0b_0100_0101,
            MessageHeader::GetReport { has_size: true, report_type: ReportType::Input },
        )
    }

    #[fuchsia::test]
    fn get_report_reserved_report_type() {
        test_parse_failure(
            0b_0100_0100,
            MessageHeaderParseError {
                error_type: MessageHeaderParseErrorType::Reserved,
                field: MessageHeaderField::ReportType,
                value: 0b_00,
            },
        )
    }

    #[fuchsia::test]
    fn set_report() {
        test_roundtrip(0b_0101_0001, MessageHeader::SetReport { report_type: ReportType::Input })
    }

    #[fuchsia::test]
    fn set_report_reserved_report_type() {
        test_parse_failure(
            0b_0100_0100,
            MessageHeaderParseError {
                error_type: MessageHeaderParseErrorType::Reserved,
                field: MessageHeaderField::ReportType,
                value: 0b_00,
            },
        )
    }

    #[fuchsia::test]
    fn get_protocol() {
        test_roundtrip(0b_0110_0000, MessageHeader::GetProtocol)
    }

    #[fuchsia::test]
    fn set_protocol() {
        test_roundtrip(
            0b_0111_0001,
            MessageHeader::SetProtocol { protocol_mode: ProtocolMode::Report },
        )
    }

    #[fuchsia::test]
    fn data() {
        test_roundtrip(0b_1010_0001, MessageHeader::SetReport { report_type: ReportType::Input })
    }
}

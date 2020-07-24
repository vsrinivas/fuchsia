// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Implements the Zedmon USB protocol, defined by
//! https://fuchsia.googlesource.com/zedmon/+/HEAD/docs/usb_proto.md.

// TODO(claridge): Remove once the protocol is in use. This is needed temporarily while building
// up this crate in isolation.
#![allow(dead_code)]

use {
    byteorder::{LittleEndian as LE, ReadBytesExt},
    num::FromPrimitive,
    num_derive::FromPrimitive,
    std::io::{BufRead, Cursor},
    std::str,
};

/// Types of packets that may be sent to or received from Zedmon.
#[repr(u8)]
#[derive(Debug, FromPrimitive, PartialEq)]
pub enum PacketType {
    QueryReportFormat = 0x0,
    QueryTime = 0x01,
    QueryParameter = 0x2,
    EnableReporting = 0x10,
    DisableReporting = 0x11,
    SetOutput = 0x20,
    ReportFormat = 0x80,
    Report = 0x81,
    Timestamp = 0x82,
    ParameterValue = 0x83,
}

/// Types for scalars sent over the network.
#[repr(u8)]
#[derive(Debug, FromPrimitive, PartialEq)]
pub enum ScalarType {
    U8 = 0x0,
    U16 = 0x1,
    U32 = 0x3,
    U64 = 0x4,
    I8 = 0x10,
    I16 = 0x11,
    I32 = 0x13,
    I64 = 0x14,
    Bool = 0x20,
    F32 = 0x40,
}

/// Possible units for scalar vales.
#[repr(u8)]
#[derive(Debug, FromPrimitive, PartialEq)]
pub enum Unit {
    Amperes = 0x0,
    Volts = 0x1,
}

/// A value corresponding to a ScalarType.
#[derive(Debug, PartialEq)]
pub enum Value {
    U8(u8),
    U16(u16),
    U32(u32),
    U64(u64),
    I8(i8),
    I16(i16),
    I32(i32),
    I64(i64),
    Bool(bool),
    F32(f32),
}

/// A fixed parameter of the Zedmon device.
#[derive(Debug, PartialEq)]
pub struct ParameterValue {
    pub name: String,
    pub value: Value,
}

/// Format of a field in a Report packet.
#[derive(Debug, PartialEq)]
pub struct ReportFormat {
    pub index: u8,
    pub field_type: ScalarType,
    pub unit: Unit,
    pub scale: f32,
    pub name: String,
}

#[derive(Debug, thiserror::Error, PartialEq)]
pub enum Error {
    #[error("Short data: required {required}, got {received}")]
    ShortData { required: usize, received: usize },
    #[error("Invalid string")]
    InvalidString,
    #[error("Wrong packet type; expected {expected:?}, got {received:?}")]
    WrongPacketType { expected: PacketType, received: PacketType },
    #[error("Unknown primitive value {primitive} for enum type {enum_name}")]
    UnknownEnumValue { primitive: u8, enum_name: &'static str },
}

/// Result alias for fallible calls in this crate.
type Result<T> = std::result::Result<T, Error>;

/// Extra reader methods for a Cursor into a byte slice.
trait CursorExtras {
    /// Retrieves a byte slice of the specified length, returning `Error::ShortData` if
    /// insufficient data is available. *Does not* advance the cursor position.
    fn get_byte_slice(&mut self, len: usize) -> Result<&[u8]>;

    /// Reads a UTF-8 string of the specifed `len`. Forwards errors from `get_byte_slice`,
    /// and returns `Error::InvalidString` if invalid UTF-8 characters are detected.
    fn read_string(&mut self, len: usize) -> Result<String>;

    /// Reads a byte and casts it to `EnumType`. Forwards errors from `get_byte_slice`,
    /// and returns `Error::UnknownEnumValue` if the cast fails.
    fn read_enum<EnumType: FromPrimitive>(&mut self) -> Result<EnumType>;
}

impl<T: AsRef<[u8]>> CursorExtras for Cursor<T> {
    fn get_byte_slice(&mut self, len: usize) -> Result<&[u8]> {
        // fill_buf() is not fallible for Cursor<T: AsRef<[u8]>>, so unwrap() is safe.
        let full_slice = &self.fill_buf().unwrap();
        if full_slice.len() < len {
            return Err(Error::ShortData { required: len, received: full_slice.len() });
        }
        Ok(&full_slice[..len])
    }

    fn read_string(&mut self, len: usize) -> Result<String> {
        let string = {
            let raw = self.get_byte_slice(len)?;
            let null_pos = raw.iter().position(|c| *c == b'\0').ok_or(Error::InvalidString)?;
            let slice = str::from_utf8(&raw[..null_pos]).map_err(|_| Error::InvalidString)?;
            slice.trim().to_string()
        };

        self.consume(len);
        Ok(string)
    }

    fn read_enum<EnumType: FromPrimitive>(&mut self) -> Result<EnumType> {
        // Use `get_byte_slice` rather than `read_u8` to clarify possible errors.
        let primitive = self.get_byte_slice(1)?[0];
        self.consume(1);

        EnumType::from_u8(primitive).ok_or(Error::UnknownEnumValue {
            primitive,
            enum_name: std::any::type_name::<EnumType>(),
        })
    }
}

/// Validates that `packet` has the required length and `PacketType`. Returns a cursor to the
/// remainder of the packet's data.
fn validate_packet<T: AsRef<[u8]>>(
    packet: &T,
    len: usize,
    expected_type: PacketType,
) -> Result<Cursor<&T>> {
    let packet_len = packet.as_ref().len();
    if packet_len < len {
        return Err(Error::ShortData { required: len, received: packet_len });
    }

    let mut cursor = Cursor::new(packet);
    let packet_type = cursor.read_enum::<PacketType>()?;
    if packet_type != expected_type {
        return Err(Error::WrongPacketType { expected: expected_type, received: packet_type });
    }
    Ok(cursor)
}

const PARAMETER_VALUE_NAME_LENGTH: usize = 54;

/// Parses a `PacketType::ParameterValue` packet.
pub fn parse_parameter_value<T: AsRef<[u8]>>(packet: &T) -> Result<ParameterValue> {
    let mut cursor = validate_packet(&packet, 64, PacketType::ParameterValue)?;

    let name = cursor.read_string(PARAMETER_VALUE_NAME_LENGTH)?;
    let field_type = cursor.read_enum::<ScalarType>()?;

    let value = match field_type {
        ScalarType::U8 => Value::U8(cursor.read_u8().unwrap()),
        ScalarType::U16 => Value::U16(cursor.read_u16::<LE>().unwrap()),
        ScalarType::U32 => Value::U32(cursor.read_u32::<LE>().unwrap()),
        ScalarType::U64 => Value::U64(cursor.read_u64::<LE>().unwrap()),
        ScalarType::I8 => Value::I8(cursor.read_i8().unwrap()),
        ScalarType::I16 => Value::I16(cursor.read_i16::<LE>().unwrap()),
        ScalarType::I32 => Value::I32(cursor.read_i32::<LE>().unwrap()),
        ScalarType::I64 => Value::I64(cursor.read_i64::<LE>().unwrap()),
        ScalarType::Bool => Value::Bool(cursor.read_u8().unwrap() != 0x00),
        ScalarType::F32 => Value::F32(cursor.read_f32::<LE>().unwrap()),
    };

    Ok(ParameterValue { name, value })
}

const REPORT_FIELD_NAME_LENGTH: usize = 56;

/// Parses a `PacketType::ReportFormat` packet.
pub fn parse_report_format<T: AsRef<[u8]>>(packet: &T) -> Result<ReportFormat> {
    let mut cursor = validate_packet(packet, 64, PacketType::ReportFormat)?;

    let index = cursor.read_u8().unwrap();
    let field_type = cursor.read_enum::<ScalarType>()?;
    let unit = cursor.read_enum::<Unit>()?;
    let scale = cursor.read_f32::<LE>().unwrap();
    let name = cursor.read_string(REPORT_FIELD_NAME_LENGTH)?;

    Ok(ReportFormat { index, field_type, unit, scale, name })
}

#[cfg(test)]
mod tests {
    use super::*;
    use matches::assert_matches;

    #[test]
    fn test_validate_packet() {
        // Successful validation
        assert!(validate_packet(
            &[PacketType::ParameterValue as u8, 0x00],
            2,
            PacketType::ParameterValue
        )
        .is_ok());

        // Bad PacketType
        assert_matches!(
            validate_packet(
                &[0xff, 0x00], 2, PacketType::ParameterValue),
            Err(Error::UnknownEnumValue {
                primitive: 0xff,
                enum_name
            }) if enum_name.contains("PacketType")
        );

        // Wrong packet length
        assert_eq!(
            validate_packet(&[0xff, 0x00], 3, PacketType::ParameterValue),
            Err(Error::ShortData { required: 3, received: 2 })
        );

        // Wrong PacketType
        assert_eq!(
            validate_packet(&[PacketType::ReportFormat as u8, 0x00], 2, PacketType::ParameterValue),
            Err(Error::WrongPacketType {
                expected: PacketType::ParameterValue,
                received: PacketType::ReportFormat
            })
        );
    }

    #[test]
    fn test_parse_report_format() {
        fn base_packet() -> Vec<u8> {
            let mut packet = Vec::new();
            packet.push(PacketType::ReportFormat as u8);
            packet.push(1);
            packet.push(ScalarType::F32 as u8);
            packet.push(Unit::Volts as u8);
            packet.extend(0.00125f32.to_le_bytes().as_ref());
            packet.extend(b"v_bus".as_ref());
            packet.extend(&vec![0u8; REPORT_FIELD_NAME_LENGTH - "v_bus".len()]);
            packet
        }

        // Base packet parses successfully
        assert_eq!(
            parse_report_format(&base_packet()),
            Ok(ReportFormat {
                index: 1,
                field_type: ScalarType::F32,
                unit: Unit::Volts,
                scale: 0.00125,
                name: "v_bus".to_string(),
            })
        );

        // Wrong packet type
        let mut packet = base_packet();
        packet[0] = PacketType::ParameterValue as u8;
        assert_eq!(
            parse_report_format(&packet),
            Err(Error::WrongPacketType {
                expected: PacketType::ReportFormat,
                received: PacketType::ParameterValue,
            })
        );

        // Bad field type
        let mut packet = base_packet();
        packet[2] = 0xff; // Invalid ScalarType
        assert_matches!(
            parse_report_format(&packet),
            Err(Error::UnknownEnumValue {
                primitive: 0xff,
                enum_name
            }) if enum_name.contains("ScalarType")
        );

        // Bad unit
        let mut packet = base_packet();
        packet[3] = 0xff; // Invalid Unit
        assert_matches!(
            parse_report_format(&packet),
            Err(Error::UnknownEnumValue {
                primitive: 0xff,
                enum_name
            }) if enum_name.contains("Unit")
        );

        // Invalid string
        let mut packet = base_packet();
        packet[8] = 0xff; // Invalid UTF-8 character
        assert_eq!(parse_report_format(&packet), Err(Error::InvalidString));
    }

    #[test]
    fn test_parse_parameter_value() {
        fn base_packet() -> Vec<u8> {
            let mut packet = Vec::new();
            packet.push(PacketType::ParameterValue as u8);
            packet.extend(b"shunt_resistance".as_ref());
            packet.extend(vec![0u8; PARAMETER_VALUE_NAME_LENGTH - "shunt_resistance".len()]);
            packet.push(ScalarType::F32 as u8);
            packet.extend(0.01f32.to_le_bytes().as_ref());
            packet.extend([0x00; 4].as_ref()); // 4 bytes padding
            packet
        }

        // Base packet parses successfully
        assert_eq!(
            parse_parameter_value(&base_packet()),
            Ok(ParameterValue { name: "shunt_resistance".to_string(), value: Value::F32(0.01) })
        );

        // Wrong packet type
        let mut packet = base_packet();
        packet[0] = PacketType::ReportFormat as u8;
        assert_eq!(
            parse_parameter_value(&packet),
            Err(Error::WrongPacketType {
                expected: PacketType::ParameterValue,
                received: PacketType::ReportFormat,
            })
        );

        // Invalid string
        let mut packet = base_packet();
        packet[1] = 0xff; // Invalid UTF-8 character
        assert_eq!(parse_parameter_value(&packet), Err(Error::InvalidString));

        // Bad field type
        let mut packet = base_packet();
        packet[1 + PARAMETER_VALUE_NAME_LENGTH] = 0xff; // Invalid ScalarType
        assert_matches!(
            parse_parameter_value(&packet),
            Err(Error::UnknownEnumValue {
                primitive: 0xff,
                enum_name
            }) if enum_name.contains("ScalarType")
        );
    }
}

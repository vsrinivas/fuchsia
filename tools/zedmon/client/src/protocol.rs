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

pub const MAX_PACKET_SIZE: usize = 64;

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
#[derive(Copy, Clone, Debug, FromPrimitive, PartialEq)]
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
#[derive(Clone, Debug, FromPrimitive, PartialEq)]
pub enum Unit {
    Amperes = 0x0,
    Volts = 0x1,
}

/// Allowable types of a reported value.
#[derive(Clone, Copy, Debug, PartialEq)]
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
#[derive(Clone, Debug, PartialEq)]
pub struct ReportFormat {
    pub index: u8,
    pub field_type: ScalarType,
    pub unit: Unit,
    pub scale: f32,
    pub name: String,
}

pub const REPORT_FORMAT_INDEX_END: u8 = 0xff;

/// Report of measured data at a timepoint.
#[derive(Clone, Debug, PartialEq)]
pub struct Report {
    /// Timestamp, in microseconds, in Zedmon's clock domain.
    ///
    /// At time of writing the timestamp is zero when Zedmon boots, but that is not a guarantee.
    // TODO(fxbug.dev/60030): Use one of power_manager's newtypes, or similar.
    pub timestamp_micros: u64,

    /// Reported values for this sample.
    pub values: Vec<Value>,
}

// Typical Zedmons only support the relay output.
#[repr(u8)]
pub enum Output {
    Relay = 0x00,
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
    #[error("{name} has ScalarType {scalar_type:?}, which cannot be scaled")]
    InvalidScaledType { name: String, scalar_type: ScalarType },
    #[error("Invalid ReportFormats: {reason}")]
    InvalidReportFormats { reason: String },
    #[error("std::io::Error received: {io_error}")]
    IoError { io_error: String },
}

impl From<std::io::Error> for Error {
    fn from(error: std::io::Error) -> Self {
        Error::IoError { io_error: error.to_string() }
    }
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

pub fn encode_disable_reporting() -> [u8; 1] {
    [PacketType::DisableReporting as u8]
}

pub fn encode_enable_reporting() -> [u8; 1] {
    [PacketType::EnableReporting as u8]
}

pub fn encode_query_report_format(index: u8) -> [u8; 2] {
    [PacketType::QueryReportFormat as u8, index]
}

pub fn encode_query_parameter(index: u8) -> [u8; 2] {
    [PacketType::QueryParameter as u8, index]
}

pub fn encode_query_time() -> [u8; 1] {
    [PacketType::QueryTime as u8]
}

pub fn encode_set_output(index: u8, value: bool) -> [u8; 3] {
    [PacketType::SetOutput as u8, index, value as u8]
}

/// Validates that `packet` has the required length and `PacketType`. Returns a cursor to the
/// remainder of the packet's data.
// NOTE: The `?Sized` relaxation in this function and several others below is needed to support
// T=[u8].
fn validate_packet<T: AsRef<[u8]> + ?Sized>(
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
    let mut cursor = validate_packet(packet, 64, PacketType::ParameterValue)?;

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
pub fn parse_report_format<T: AsRef<[u8]> + ?Sized>(packet: &T) -> Result<ReportFormat> {
    let mut cursor = validate_packet(packet, 64, PacketType::ReportFormat)?;

    let index = cursor.read_u8().unwrap();
    let field_type = cursor.read_enum::<ScalarType>()?;
    let unit = cursor.read_enum::<Unit>()?;
    let scale = cursor.read_f32::<LE>().unwrap();
    let name = cursor.read_string(REPORT_FIELD_NAME_LENGTH)?;

    Ok(ReportFormat { index, field_type, unit, scale, name })
}

pub fn parse_timestamp_micros<T: AsRef<[u8]> + ?Sized>(packet: &T) -> Result<u64> {
    let mut cursor = validate_packet(packet, 9, PacketType::Timestamp)?;
    Ok(cursor.read_u64::<LE>()?)
}

/// Calculates the length of a serialized Report from its field types.
fn calc_report_length(field_types: &Vec<ScalarType>) -> usize {
    let mut len = 8; // u64 timestamp
    for t in field_types {
        len += match t {
            ScalarType::U8 => 1,
            ScalarType::U16 => 2,
            ScalarType::U32 => 4,
            ScalarType::U64 => 8,
            ScalarType::I8 => 1,
            ScalarType::I16 => 2,
            ScalarType::I32 => 4,
            ScalarType::I64 => 8,
            ScalarType::Bool => 1,
            ScalarType::F32 => 4,
        };
    }
    len
}

/// ReportParser's internal way of representing the type of a reported value. A field with
/// ScalarType of U8, U16, I8, or I16 may be coupled with a nonzero scale, in which case
/// ReportParser will parse it into an F32.
///
/// For now, only the integer types that may be losslessly converted to f32 are considered scalable.
/// Should the need arise, it would be straightforward to support scalable u32 and i32 values as
/// well, with parsing into f64.
#[derive(Debug)]
enum ReportedValueType {
    Unscaled(ScalarType),
    ScaledU8(f32),
    ScaledU16(f32),
    ScaledI8(f32),
    ScaledI16(f32),
}

/// Helper struct for parsing reports.
///
/// This is used instead of a bare function to avoid repeatedly re-calculating report length based
/// on the fixed value types.
#[derive(Debug)]
pub struct ReportParser {
    report_length: usize,
    value_types: Vec<ReportedValueType>,
}

impl ReportParser {
    pub fn new(format: &Vec<ReportFormat>) -> Result<ReportParser> {
        // Sort the provided field formats by index, and make sure that the resulting indices
        // are 0..format.len().
        let format = {
            let mut sorted = format.clone();
            let comparator = |a: &ReportFormat, b: &ReportFormat| {
                if a.index < b.index {
                    std::cmp::Ordering::Less
                } else if a.index == b.index {
                    std::cmp::Ordering::Equal
                } else {
                    std::cmp::Ordering::Greater
                }
            };
            sorted.sort_by(comparator);

            let indices: Vec<u8> = sorted.iter().map(|f| f.index).collect();
            let expected: Vec<u8> = (0..sorted.len()).map(|x| x as u8).collect();
            if indices != expected {
                return Err(Error::InvalidReportFormats {
                    reason: format!(
                        "ReportFormat indices {:?} must sort to {:?}",
                        indices, expected
                    ),
                });
            }

            sorted
        };

        // Determine the ScalarType and ReportedValueType of each field.
        let mut scalar_types = Vec::new();
        let mut reported_types = Vec::new();
        for field_format in format {
            scalar_types.push(field_format.field_type);

            let scale = field_format.scale;
            let reported_type = if scale == 0.0 {
                ReportedValueType::Unscaled(field_format.field_type)
            } else {
                match field_format.field_type {
                    ScalarType::U8 => ReportedValueType::ScaledU8(scale),
                    ScalarType::U16 => ReportedValueType::ScaledU16(scale),
                    ScalarType::I8 => ReportedValueType::ScaledI8(scale),
                    ScalarType::I16 => ReportedValueType::ScaledI16(scale),
                    _ => {
                        return Err(Error::InvalidScaledType {
                            name: field_format.name.clone(),
                            scalar_type: field_format.field_type,
                        })
                    }
                }
            };
            reported_types.push(reported_type);
        }

        Ok(ReportParser {
            report_length: calc_report_length(&scalar_types),
            value_types: reported_types,
        })
    }

    /// Parses a Report packet to a Vec<Report>.
    pub fn parse_reports<T: AsRef<[u8]> + ?Sized>(&self, packet: &T) -> Result<Vec<Report>> {
        let num_bytes = packet.as_ref().len();

        // Determine how many reports follow the leading PacketType byte.
        let num_reports = (num_bytes - 1) / self.report_length;

        let mut cursor = validate_packet(packet, num_bytes, PacketType::Report)?;

        let mut reports = Vec::with_capacity(num_reports);

        for _ in 0..num_reports {
            let timestamp = cursor.read_u64::<LE>()?;
            let mut values = Vec::new();
            for t in &self.value_types {
                let value = match *t {
                    ReportedValueType::Unscaled(st) => match st {
                        ScalarType::U8 => Value::U8(cursor.read_u8()?),
                        ScalarType::U16 => Value::U16(cursor.read_u16::<LE>()?),
                        ScalarType::U32 => Value::U32(cursor.read_u32::<LE>()?),
                        ScalarType::U64 => Value::U64(cursor.read_u64::<LE>()?),
                        ScalarType::I8 => Value::I8(cursor.read_i8()?),
                        ScalarType::I16 => Value::I16(cursor.read_i16::<LE>()?),
                        ScalarType::I32 => Value::I32(cursor.read_i32::<LE>()?),
                        ScalarType::I64 => Value::I64(cursor.read_i64::<LE>()?),
                        ScalarType::Bool => Value::Bool(cursor.read_u8()? != 0),
                        ScalarType::F32 => Value::F32(cursor.read_f32::<LE>()?),
                    },
                    ReportedValueType::ScaledU8(scale) => {
                        Value::F32(cursor.read_u8()? as f32 * scale)
                    }
                    ReportedValueType::ScaledU16(scale) => {
                        Value::F32(cursor.read_u16::<LE>()? as f32 * scale)
                    }
                    ReportedValueType::ScaledI8(scale) => {
                        Value::F32(cursor.read_i8()? as f32 * scale)
                    }
                    ReportedValueType::ScaledI16(scale) => {
                        Value::F32(cursor.read_i16::<LE>()? as f32 * scale)
                    }
                };
                values.push(value);
            }
            reports.push(Report { timestamp_micros: timestamp, values });
        }
        Ok(reports)
    }
}

/// This module is public to expose serialization functions to tests in other modules.
#[cfg(test)]
pub mod tests {
    use {super::*, byteorder::WriteBytesExt, matches::assert_matches, std::io::Write};

    fn get_scalar_type(value: &Value) -> ScalarType {
        match *value {
            Value::U8(_) => ScalarType::U8,
            Value::U16(_) => ScalarType::U16,
            Value::U32(_) => ScalarType::U32,
            Value::U64(_) => ScalarType::U64,
            Value::I8(_) => ScalarType::I8,
            Value::I16(_) => ScalarType::I16,
            Value::I32(_) => ScalarType::I32,
            Value::I64(_) => ScalarType::I64,
            Value::Bool(_) => ScalarType::Bool,
            Value::F32(_) => ScalarType::F32,
        }
    }

    /// Serializes a ReportFormat into a buffer, returning the number of bytes written.
    pub fn serialize_report_format(format: ReportFormat, buffer: &mut [u8]) -> usize {
        let mut buffer = &mut buffer[..MAX_PACKET_SIZE];

        buffer
            .write(&[
                PacketType::ReportFormat as u8,
                format.index,
                format.field_type as u8,
                format.unit as u8,
            ])
            .unwrap();
        buffer.write_f32::<LE>(format.scale).unwrap();

        // Make sure the name leaves room for a null terminator, write it, then zero the rest of the
        // buffer.
        let name = &format.name;
        assert!(name.len() < buffer.len());
        buffer.write(name.as_bytes()).unwrap();
        for i in 0..buffer.len() {
            buffer[i] = 0;
        }

        MAX_PACKET_SIZE
    }

    /// Serializes a ParameterValue into a buffer, returning the number of bytes written.
    pub fn serialize_parameter_value(parameter: ParameterValue, buffer: &mut [u8]) -> usize {
        let buffer = &mut buffer[..MAX_PACKET_SIZE];

        buffer[0] = PacketType::ParameterValue as u8;

        // Write the parameter name, making sure there's room for a null terminator.
        let name_length = parameter.name.len();
        assert!(name_length < PARAMETER_VALUE_NAME_LENGTH);
        buffer[1..name_length + 1].copy_from_slice(parameter.name.as_bytes());

        // Zero out the remainder of the name region, and the value region as well, so we don't have
        // to worry about the number of bytes to zero for each ScalarType.
        for i in name_length + 1..MAX_PACKET_SIZE {
            buffer[i] = 0;
        }

        // Write the value.
        let mut value_buffer = &mut buffer[1 + PARAMETER_VALUE_NAME_LENGTH..];
        value_buffer.write_u8(get_scalar_type(&parameter.value) as u8).unwrap();
        match parameter.value {
            Value::U8(v) => value_buffer.write_u8(v).unwrap(),
            Value::U16(v) => value_buffer.write_u16::<LE>(v).unwrap(),
            Value::U32(v) => value_buffer.write_u32::<LE>(v).unwrap(),
            Value::U64(v) => value_buffer.write_u64::<LE>(v).unwrap(),
            Value::I8(v) => value_buffer.write_i8(v).unwrap(),
            Value::I16(v) => value_buffer.write_i16::<LE>(v).unwrap(),
            Value::I32(v) => value_buffer.write_i32::<LE>(v).unwrap(),
            Value::I64(v) => value_buffer.write_i64::<LE>(v).unwrap(),
            Value::Bool(v) => value_buffer.write_u8(v as u8).unwrap(),
            Value::F32(v) => value_buffer.write_f32::<LE>(v).unwrap(),
        }
        MAX_PACKET_SIZE
    }

    /// Serializes a Vec<Report> into a buffer, returning the number of bytes written.
    ///
    /// If the Values in a given Report are to be scaled, they should be sent to this function in
    /// their *unscaled* form. Note that serialize_reports is not the inverse of ReportParser.parse
    /// for scaled Values.
    pub fn serialize_reports(reports: &Vec<Report>, mut buffer: &mut [u8]) -> usize {
        let field_types = reports[0].values.iter().map(|v| get_scalar_type(v)).collect();

        // Make sure the buffer has enough space for the reports. If this fails, the test probably isn't
        // configured correctly.
        let num_bytes = calc_report_length(&field_types) * reports.len() + 1;
        assert!(num_bytes <= buffer.len());

        buffer.write_u8(PacketType::Report as u8).unwrap();
        for report in reports {
            buffer.write_u64::<LE>(report.timestamp_micros).unwrap();
            for (value, field_type) in report.values.iter().zip(field_types.iter()) {
                assert_eq!(
                    get_scalar_type(value),
                    *field_type,
                    "Value {:?} has wrong type (need {:?})",
                    *value,
                    *field_type
                );
                match *value {
                    Value::U8(v) => buffer.write_u8(v).unwrap(),
                    Value::U16(v) => buffer.write_u16::<LE>(v).unwrap(),
                    Value::U32(v) => buffer.write_u32::<LE>(v).unwrap(),
                    Value::U64(v) => buffer.write_u64::<LE>(v).unwrap(),
                    Value::I8(v) => buffer.write_i8(v).unwrap(),
                    Value::I16(v) => buffer.write_i16::<LE>(v).unwrap(),
                    Value::I32(v) => buffer.write_i32::<LE>(v).unwrap(),
                    Value::I64(v) => buffer.write_i64::<LE>(v).unwrap(),
                    Value::Bool(v) => buffer.write_u8(v as u8).unwrap(),
                    Value::F32(v) => buffer.write_f32::<LE>(v).unwrap(),
                }
            }
        }

        num_bytes
    }

    /// Serializes a timestamp into a buffer, returning the number of bytes written.
    pub fn serialize_timestamp_micros(timestamp: u64, mut buffer: &mut [u8]) -> usize {
        buffer.write_u8(PacketType::Timestamp as u8).unwrap();
        buffer.write_u64::<LE>(timestamp).unwrap();
        9
    }

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
        let mut base_packet = [0; MAX_PACKET_SIZE];
        serialize_report_format(
            ReportFormat {
                index: 1,
                field_type: ScalarType::F32,
                unit: Unit::Volts,
                scale: 0.00125,
                name: "v_bus".to_string(),
            },
            &mut base_packet,
        );

        // Base packet parses successfully
        assert_eq!(
            parse_report_format(&base_packet),
            Ok(ReportFormat {
                index: 1,
                field_type: ScalarType::F32,
                unit: Unit::Volts,
                scale: 0.00125,
                name: "v_bus".to_string(),
            })
        );

        // Wrong packet type
        let mut packet = base_packet.clone();
        packet[0] = PacketType::ParameterValue as u8;
        assert_eq!(
            parse_report_format(&packet),
            Err(Error::WrongPacketType {
                expected: PacketType::ReportFormat,
                received: PacketType::ParameterValue,
            })
        );

        // Bad field type
        let mut packet = base_packet.clone();
        packet[2] = 0xff; // Invalid ScalarType
        assert_matches!(
            parse_report_format(&packet),
            Err(Error::UnknownEnumValue {
                primitive: 0xff,
                enum_name
            }) if enum_name.contains("ScalarType")
        );

        // Bad unit
        let mut packet = base_packet.clone();
        packet[3] = 0xff; // Invalid Unit
        assert_matches!(
            parse_report_format(&packet),
            Err(Error::UnknownEnumValue {
                primitive: 0xff,
                enum_name
            }) if enum_name.contains("Unit")
        );

        // Invalid string
        let mut packet = base_packet.clone();
        packet[8] = 0xff; // Invalid UTF-8 character
        assert_eq!(parse_report_format(&packet), Err(Error::InvalidString));
    }

    #[test]
    fn test_parse_parameter_value() {
        let mut base_packet = [0; MAX_PACKET_SIZE];
        serialize_parameter_value(
            ParameterValue { name: "shunt_resistance".to_string(), value: Value::F32(0.01) },
            &mut base_packet,
        );

        // Base packet parses successfully
        assert_eq!(
            parse_parameter_value(&base_packet),
            Ok(ParameterValue { name: "shunt_resistance".to_string(), value: Value::F32(0.01) })
        );

        // Wrong packet type
        let mut packet = base_packet.clone();
        packet[0] = PacketType::ReportFormat as u8;
        assert_eq!(
            parse_parameter_value(&packet),
            Err(Error::WrongPacketType {
                expected: PacketType::ParameterValue,
                received: PacketType::ReportFormat,
            })
        );

        // Invalid string
        let mut packet = base_packet.clone();
        packet[1] = 0xff; // Invalid UTF-8 character
        assert_eq!(parse_parameter_value(&packet), Err(Error::InvalidString));

        // Bad field type
        let mut packet = base_packet.clone();
        packet[1 + PARAMETER_VALUE_NAME_LENGTH] = 0xff; // Invalid ScalarType
        assert_matches!(
            parse_parameter_value(&packet),
            Err(Error::UnknownEnumValue {
                primitive: 0xff,
                enum_name
            }) if enum_name.contains("ScalarType")
        );
    }

    #[test]
    fn test_parse_timestamp_micros() {
        let mut base_packet = [0; 9];
        serialize_timestamp_micros(1234567, &mut base_packet);

        // Base packet parses successfully
        assert_eq!(parse_timestamp_micros(&base_packet), Ok(1234567));

        let mut packet = base_packet.clone();
        packet[0] = PacketType::ParameterValue as u8;
        assert_eq!(
            parse_timestamp_micros(&packet),
            Err(Error::WrongPacketType {
                expected: PacketType::Timestamp,
                received: PacketType::ParameterValue,
            })
        );
    }

    #[test]
    fn test_report_parser() -> Result<()> {
        /// Makes a Vec<ReportFormat> from an array or Vec of tuples.
        fn make_format<V>(field_formats: V) -> Vec<ReportFormat>
        where
            for<'a> &'a V: IntoIterator<Item = &'a (u8, ScalarType, f32)>,
        {
            field_formats
                .into_iter()
                .map(|v| ReportFormat {
                    index: v.0,
                    field_type: v.1,
                    unit: Unit::Volts,
                    scale: v.2,
                    name: "foo".to_string(),
                })
                .collect()
        }

        fn serialize_and_parse(
            reports_in: &Vec<Report>,
            parser: &ReportParser,
        ) -> Result<Vec<Report>> {
            let mut packet = [0; MAX_PACKET_SIZE];
            let bytes_used = serialize_reports(&reports_in, &mut packet[..]);
            parser.parse_reports(&packet[..bytes_used])
        }

        // Test all unscaled types.
        let format = make_format([
            (0, ScalarType::U8, 0.0),
            (1, ScalarType::U16, 0.0),
            (2, ScalarType::U32, 0.0),
            (3, ScalarType::U64, 0.0),
            (4, ScalarType::I8, 0.0),
            (5, ScalarType::I16, 0.0),
            (6, ScalarType::I32, 0.0),
            (7, ScalarType::I64, 0.0),
            (8, ScalarType::Bool, 0.0),
            (9, ScalarType::F32, 0.0),
        ]);
        let parser = ReportParser::new(&format)?;
        let reports_in = vec![Report {
            timestamp_micros: 1000,
            values: vec![
                Value::U8(u8::MAX),
                Value::U16(u16::MAX),
                Value::U32(u32::MAX),
                Value::U64(u64::MAX),
                Value::I8(i8::MIN),
                Value::I16(i16::MIN),
                Value::I32(i32::MIN),
                Value::I64(i64::MIN),
                Value::Bool(true),
                Value::F32(f32::MAX),
            ],
        }];
        let reports_out = serialize_and_parse(&reports_in, &parser)?;
        assert_eq!(reports_in, reports_out);

        // Test all scaled types.
        let format = make_format([
            (0, ScalarType::U8, 0.1),
            (1, ScalarType::U16, 0.2),
            (2, ScalarType::I8, 0.3),
            (3, ScalarType::I16, 0.4),
        ]);
        let parser = ReportParser::new(&format)?;
        let reports_in = vec![Report {
            timestamp_micros: 1000,
            values: vec![
                Value::U8(u8::MAX),
                Value::U16(u16::MAX),
                Value::I8(i8::MIN),
                Value::I16(i16::MIN),
            ],
        }];
        let reports_out = serialize_and_parse(&reports_in, &parser)?;
        let expected = vec![Report {
            timestamp_micros: 1000,
            values: vec![
                Value::F32(u8::MAX as f32 * 0.1),
                Value::F32(u16::MAX as f32 * 0.2),
                Value::F32(i8::MIN as f32 * 0.3),
                Value::F32(i16::MIN as f32 * 0.4),
            ],
        }];
        assert_eq!(reports_out, expected);

        // Test multiple reports.
        let format = make_format([(0, ScalarType::U8, 0.0), (1, ScalarType::I16, 0.0)]);
        let parser = ReportParser::new(&format)?;
        let reports_in = vec![
            Report { timestamp_micros: 1000, values: vec![Value::U8(1), Value::I16(-1)] },
            Report { timestamp_micros: 2000, values: vec![Value::U8(2), Value::I16(-2)] },
            Report { timestamp_micros: 3000, values: vec![Value::U8(3), Value::I16(-3)] },
            Report { timestamp_micros: 4000, values: vec![Value::U8(4), Value::I16(-4)] },
        ];
        let reports_out = serialize_and_parse(&reports_in, &parser)?;
        assert_eq!(reports_in, reports_out);

        // Test sorting of field formats by index
        let format = make_format([
            (2, ScalarType::F32, 0.0),
            (0, ScalarType::U8, 0.0),
            (1, ScalarType::I16, 0.0),
        ]);
        let parser = ReportParser::new(&format)?;
        let reports_in = vec![Report {
            timestamp_micros: 1000,
            values: vec![Value::U8(u8::MAX), Value::I16(i16::MIN), Value::F32(f32::MIN)],
        }];
        let reports_out = serialize_and_parse(&reports_in, &parser)?;
        assert_eq!(reports_in, reports_out);

        // Test error on invalid ReportFormat indices
        let format = make_format([
            (0, ScalarType::U8, 0.0),
            (1, ScalarType::I16, 0.0),
            (3, ScalarType::F32, 0.0),
        ]);
        let result = ReportParser::new(&format);
        assert_matches!(result, Err(Error::InvalidReportFormats { reason: _ }));

        // Test error on invalid scaled type
        let format = make_format([(0, ScalarType::Bool, 0.1)]);
        let result = ReportParser::new(&format);
        assert_matches!(result, Err(Error::InvalidScaledType { name: _, scalar_type: _ }));

        Ok(())
    }
}

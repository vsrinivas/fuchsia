// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

//! This crate provides an implementation of Fuchsia Diagnostic Streams, often referred to as
//! "logs."

#![warn(clippy::all, missing_docs)]

use bitfield::bitfield;
use std::convert::TryFrom;

pub use fidl_fuchsia_diagnostics::Severity;
pub use fidl_fuchsia_diagnostics_stream::{Argument, Record, Value};

pub mod encode;
pub mod parse;

/// The tracing format supports many types of records, we're sneaking in as a log message.
const TRACING_FORMAT_LOG_RECORD_TYPE: u8 = 9;

bitfield! {
    /// A header in the tracing format. Expected to precede every Record and Argument.
    ///
    /// The tracing format specifies [Record headers] and [Argument headers] as distinct types, but
    /// their layouts are the same in practice, so we represent both bitfields using the same
    /// struct.
    ///
    /// [Record headers]: https://fuchsia.dev/fuchsia-src/development/tracing/trace-format#record_header
    /// [Argument headers]: https://fuchsia.dev/fuchsia-src/development/tracing/trace-format#argument_header
    pub struct Header(u64);
    impl Debug;

    /// Record type.
    u8, raw_type, set_type: 3, 0;

    /// Record size as a multiple of 8 bytes.
    u16, size_words, set_size_words: 15, 4;

    /// String ref for the associated name, if any.
    u16, name_ref, set_name_ref: 31, 16;

    /// Reserved for record-type-specific data.
    u16, value_ref, set_value_ref: 47, 32;

    /// Severity of the record, if any.
    u8, severity, set_severity: 63, 56;
}

impl Header {
    /// Sets the length of the item the header refers to. Panics if not 8-byte aligned.
    fn set_len(&mut self, new_len: usize) {
        assert_eq!(new_len % 8, 0, "encoded message must be 8-byte aligned");
        self.set_size_words((new_len / 8) as u16 + if new_len % 8 > 0 { 1 } else { 0 });
    }
}

/// These literal values are specified by the tracing format:
///
/// https://fuchsia.dev/fuchsia-src/development/tracing/trace-format#argument_header
#[repr(u8)]
enum ArgType {
    Null = 0,
    I32 = 1,
    U32 = 2,
    I64 = 3,
    U64 = 4,
    F64 = 5,
    String = 6,
    Pointer = 7,
    Koid = 8,
}

impl TryFrom<u8> for ArgType {
    type Error = parse::ParseError;
    fn try_from(b: u8) -> Result<Self, Self::Error> {
        Ok(match b {
            0 => ArgType::Null,
            1 => ArgType::I32,
            2 => ArgType::U32,
            3 => ArgType::I64,
            4 => ArgType::U64,
            5 => ArgType::F64,
            6 => ArgType::String,
            7 => ArgType::Pointer,
            8 => ArgType::Koid,
            _ => return Err(parse::ParseError::ValueOutOfValidRange),
        })
    }
}

#[derive(Clone)]
enum StringRef<'a> {
    Empty,
    Inline(&'a str),
}

impl<'a> StringRef<'a> {
    fn mask(&self) -> u16 {
        match self {
            StringRef::Empty => 0,
            StringRef::Inline(s) => (s.len() as u16) | (1 << 15),
        }
    }

    fn for_str(string: &'a str) -> Self {
        match string.len() {
            0 => StringRef::Empty,
            _ => StringRef::Inline(string),
        }
    }
}

impl<'a> Into<String> for StringRef<'a> {
    fn into(self) -> String {
        match self {
            StringRef::Empty => String::new(),
            StringRef::Inline(s) => s.to_owned(),
        }
    }
}

impl<'a> ToString for StringRef<'a> {
    fn to_string(&self) -> String {
        self.clone().into()
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{
            encode::{BufMutShared, Encoder, EncodingError},
            parse::{parse_argument, try_parse_record, ParseResult},
        },
        fidl_fuchsia_diagnostics::Severity,
        fidl_fuchsia_diagnostics_stream::{Argument, Record, Value},
        fuchsia_zircon as zx,
        std::{fmt::Debug, io::Cursor},
    };

    const BUF_LEN: usize = 1024;

    pub(crate) fn assert_roundtrips<T>(
        val: T,
        encoder_method: impl Fn(&mut Encoder<Cursor<Vec<u8>>>, &T) -> Result<(), EncodingError>,
        parser: impl Fn(&[u8]) -> ParseResult<'_, T>,
        canonical: Option<&[u8]>,
    ) where
        T: Debug + PartialEq,
    {
        let mut encoder = Encoder::new(Cursor::new(vec![0; BUF_LEN]));
        encoder_method(&mut encoder, &val).unwrap();

        // next we'll parse the record out of a buf with padding after the record
        let (_, decoded_from_full) =
            nom::dbg_dmp(&parser, "roundtrip")(encoder.buf.get_ref()).unwrap();
        assert_eq!(val, decoded_from_full, "decoded version with trailing padding must match");

        if let Some(canonical) = canonical {
            let recorded = encoder.buf.get_ref().split_at(canonical.len()).0;
            assert_eq!(canonical, recorded, "encoded repr must match the canonical value provided");

            let (zero_buf, decoded) = nom::dbg_dmp(&parser, "roundtrip")(&recorded).unwrap();
            assert_eq!(val, decoded, "decoded version must match what we tried to encode");
            assert_eq!(zero_buf.len(), 0, "must parse record exactly out of provided buffer");
        }
    }

    /// Bit pattern for the log record type, severity info, and a record of two words: one header,
    /// one timestamp.
    const MINIMAL_LOG_HEADER: u64 = 0x3000000000000029;

    #[test]
    fn minimal_header() {
        let mut poked = Header(0);
        poked.set_type(TRACING_FORMAT_LOG_RECORD_TYPE);
        poked.set_size_words(2);
        poked.set_severity(Severity::Info.into_primitive());

        assert_eq!(
            poked.0, MINIMAL_LOG_HEADER,
            "minimal log header should only describe type, size, and severity"
        );
    }

    #[test]
    fn no_args_roundtrip() {
        let mut expected_record = MINIMAL_LOG_HEADER.to_le_bytes().to_vec();
        let timestamp = 5_000_000i64;
        expected_record.extend(&timestamp.to_le_bytes());

        assert_roundtrips(
            Record { timestamp, severity: Severity::Info, arguments: vec![] },
            Encoder::write_record,
            try_parse_record,
            Some(&expected_record),
        );
    }

    #[test]
    fn signed_arg_roundtrip() {
        assert_roundtrips(
            Argument { name: String::from("signed"), value: Value::SignedInt(-1999) },
            Encoder::write_argument,
            parse_argument,
            None,
        );
    }

    #[test]
    fn unsigned_arg_roundtrip() {
        assert_roundtrips(
            Argument { name: String::from("unsigned"), value: Value::UnsignedInt(42) },
            Encoder::write_argument,
            parse_argument,
            None,
        );
    }

    #[test]
    fn text_arg_roundtrip() {
        assert_roundtrips(
            Argument { name: String::from("stringarg"), value: Value::Text(String::from("owo")) },
            Encoder::write_argument,
            parse_argument,
            None,
        );
    }

    #[test]
    fn float_arg_roundtrip() {
        assert_roundtrips(
            Argument { name: String::from("float"), value: Value::Floating(3.14159) },
            Encoder::write_argument,
            parse_argument,
            None,
        );
    }

    #[test]
    fn arg_of_each_type_roundtrips() {
        assert_roundtrips(
            Record {
                timestamp: zx::Time::get(zx::ClockId::Monotonic).into_nanos(),
                severity: Severity::Warn,
                arguments: vec![
                    Argument { name: String::from("signed"), value: Value::SignedInt(-10) },
                    Argument { name: String::from("unsigned"), value: Value::SignedInt(7) },
                    Argument { name: String::from("float"), value: Value::Floating(3.14159) },
                    Argument {
                        name: String::from("msg"),
                        value: Value::Text(String::from("test message one")),
                    },
                ],
            },
            Encoder::write_record,
            try_parse_record,
            None,
        );
    }

    #[test]
    fn multiple_string_args() {
        assert_roundtrips(
            Record {
                timestamp: zx::Time::get(zx::ClockId::Monotonic).into_nanos(),
                severity: Severity::Trace,
                arguments: vec![
                    Argument {
                        name: String::from("msg"),
                        value: Value::Text(String::from("test message one")),
                    },
                    Argument {
                        name: String::from("msg2"),
                        value: Value::Text(String::from("test message two")),
                    },
                    Argument {
                        name: String::from("msg3"),
                        value: Value::Text(String::from("test message three")),
                    },
                ],
            },
            Encoder::write_record,
            try_parse_record,
            None,
        );
    }

    #[test]
    fn invalid_records() {
        // invalid word size
        let mut encoder = Encoder::new(Cursor::new(vec![0; BUF_LEN]));
        let mut header = Header(0);
        header.set_type(TRACING_FORMAT_LOG_RECORD_TYPE);
        header.set_size_words(0); // invalid, should be at least 2 as header and time are included
        encoder.buf.put_u64_le(header.0).unwrap();
        encoder.buf.put_i64_le(zx::Time::get(zx::ClockId::Monotonic).into_nanos()).unwrap();
        encoder
            .write_argument(&Argument {
                name: String::from("msg"),
                value: Value::Text(String::from("test message one")),
            })
            .unwrap();

        assert!(try_parse_record(encoder.buf.get_ref()).is_err());
    }
}

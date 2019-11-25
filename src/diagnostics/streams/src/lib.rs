// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

//! This crate provides an implementation of Fuchsia Diagnostic Streams, often referred to as
//! "logs."

#![warn(clippy::all, missing_docs)]

use {
    bitfield::bitfield,
    std::{array::TryFromSliceError, convert::TryFrom},
};

pub use fidl_fuchsia_diagnostics_streaming::{Argument, Record, Value};

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
}

impl Header {
    /// Dynamically sized portion of the record, in bytes.
    fn variable_length(&self) -> usize {
        (self.size_words() - 2) as usize * 8
    }

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
    type Error = StreamError;
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
            _ => return Err(StreamError::ValueOutOfValidRange),
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

/// Errors which occur when interacting with streams of diagnostic records.
#[derive(Debug)]
pub enum StreamError {
    /// The provided buffer is incorrectly sized, usually due to being too small.
    BufferSize,

    /// We attempted to parse bytes as a type for which the bytes are not a valid pattern.
    ValueOutOfValidRange,

    /// We attempted to parse or encode values which are not yet supported by this implementation of
    /// the Fuchsia Tracing format.
    Unsupported,

    /// We encountered a generic `nom` error while parsing.
    Nom(nom::error::ErrorKind),
}

impl From<TryFromSliceError> for StreamError {
    fn from(_: TryFromSliceError) -> Self {
        StreamError::BufferSize
    }
}

impl From<std::str::Utf8Error> for StreamError {
    fn from(_: std::str::Utf8Error) -> Self {
        StreamError::ValueOutOfValidRange
    }
}

impl nom::error::ParseError<&[u8]> for StreamError {
    fn from_error_kind(_input: &[u8], kind: nom::error::ErrorKind) -> Self {
        StreamError::Nom(kind)
    }

    fn append(_input: &[u8], kind: nom::error::ErrorKind, _prev: Self) -> Self {
        // TODO support chaining these
        StreamError::Nom(kind)
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{
            encode::Encoder,
            parse::{parse_argument, parse_record, ParseResult},
        },
        fidl_fuchsia_diagnostics_streaming::{Argument, Record, Value},
        fuchsia_zircon as zx,
        std::{fmt::Debug, io::Cursor},
    };

    pub(crate) fn assert_roundtrips<T>(
        val: T,
        encoder_method: impl Fn(&mut Encoder<Cursor<Vec<u8>>>, &T) -> Result<(), StreamError>,
        parser: impl Fn(&[u8]) -> ParseResult<'_, T>,
        canonical: Option<&[u8]>,
    ) where
        T: Debug + PartialEq,
    {
        const BUF_LEN: usize = 1024;
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

    /// Bit pattern for the log record type and a record of two words: one header, one timestamp.
    const MINIMAL_LOG_HEADER: u64 = 0x29;

    #[test]
    fn minimal_header() {
        let mut poked = Header(0);
        poked.set_type(TRACING_FORMAT_LOG_RECORD_TYPE);
        poked.set_size_words(2);

        assert_eq!(
            poked.0, MINIMAL_LOG_HEADER,
            "minimal log header should only describe type and size"
        );
    }

    #[test]
    fn no_args_roundtrip() {
        let mut expected_record = MINIMAL_LOG_HEADER.to_le_bytes().to_vec();
        let timestamp = 5_000_000i64;
        expected_record.extend(&timestamp.to_le_bytes());

        assert_roundtrips(
            Record { timestamp, arguments: vec![] },
            Encoder::write_record,
            parse_record,
            Some(&expected_record),
        );
    }

    #[test]
    fn signed_arg_roundtrip() {
        assert_roundtrips(
            Argument { name: String::from("signed"), value: Value::Signed(-1999) },
            Encoder::write_argument,
            parse_argument,
            None,
        );
    }

    #[test]
    fn unsigned_arg_roundtrip() {
        assert_roundtrips(
            Argument { name: String::from("unsigned"), value: Value::Unsigned(42) },
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
                arguments: vec![
                    Argument { name: String::from("signed"), value: Value::Signed(-10) },
                    Argument { name: String::from("unsigned"), value: Value::Signed(7) },
                    Argument { name: String::from("float"), value: Value::Floating(3.14159) },
                    Argument {
                        name: String::from("msg"),
                        value: Value::Text(String::from("test message one")),
                    },
                ],
            },
            Encoder::write_record,
            parse_record,
            None,
        );
    }

    #[test]
    fn multiple_string_args() {
        assert_roundtrips(
            Record {
                timestamp: zx::Time::get(zx::ClockId::Monotonic).into_nanos(),
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
            parse_record,
            None,
        );
    }
}

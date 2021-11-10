// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Parse diagnostic records from streams, returning FIDL-generated structs that match expected
//! diagnostic service APIs.

use crate::{ArgType, Header, StringRef};
use fidl_fuchsia_diagnostics::Severity;
use fidl_fuchsia_diagnostics_stream::{Argument, Record, Value};
use nom::{
    bytes::complete::take,
    multi::many0,
    number::complete::{le_f64, le_i64, le_u64},
    Err, IResult,
};
use std::{borrow::Cow, cell::RefCell, convert::TryFrom, rc::Rc};
use thiserror::Error;

pub(crate) type ParseResult<'a, T> = IResult<&'a [u8], T, ParseError>;

/// Attempt to parse a diagnostic record from the head of this buffer, returning the record and any
/// unused portion of the buffer if successful.
pub fn parse_record(buf: &[u8]) -> Result<(Record, &[u8]), ParseError> {
    match try_parse_record(buf) {
        Ok((remainder, record)) => Ok((record, remainder)),
        Err(Err::Incomplete(n)) => Err(ParseError::Incomplete(n)),
        Err(Err::Error(e)) | Err(Err::Failure(e)) => Err(e),
    }
}

/// Internal parser state.
/// Used to support handling of invalid utf-8 in msg fields.
enum ParseState {
    /// Initial parsing state
    Initial,
    /// We're in a message
    InMessage,
    /// We're in arguments (no special Unicode treatment)
    InArguments,
}

pub(crate) fn try_parse_record(buf: &[u8]) -> ParseResult<'_, Record> {
    let (after_header, header) = parse_header(buf)?;

    if header.raw_type() != crate::TRACING_FORMAT_LOG_RECORD_TYPE {
        return Err(nom::Err::Failure(ParseError::ValueOutOfValidRange));
    }

    let (var_len, timestamp) = le_i64(after_header)?;

    // Remove two word lengths for header and timestamp.
    let remaining_record_len = if header.size_words() >= 2 {
        (header.size_words() - 2) as usize * 8
    } else {
        return Err(nom::Err::Failure(ParseError::ValueOutOfValidRange));
    };
    let severity = Severity::from_primitive(header.severity())
        .ok_or(nom::Err::Failure(ParseError::Unsupported))?;

    let (after_record, args_buf) = take(remaining_record_len)(var_len)?;
    let state = Rc::new(RefCell::new(ParseState::Initial));
    let (_, arguments) =
        many0(|input| parse_argument_internal(input, &mut state.borrow_mut()))(args_buf)?;

    Ok((after_record, Record { timestamp, severity, arguments }))
}

fn parse_header(buf: &[u8]) -> ParseResult<'_, Header> {
    let (after, header) = le_u64(buf)?;
    let header = Header(header);

    Ok((after, header))
}

/// Parses an argument
pub fn parse_argument(buf: &[u8]) -> ParseResult<'_, Argument> {
    let mut state = ParseState::Initial;
    parse_argument_internal(buf, &mut state)
}

fn parse_argument_internal<'a, 'b>(
    buf: &'a [u8],
    state: &'b mut ParseState,
) -> ParseResult<'a, Argument> {
    let (after_header, header) = parse_header(buf)?;
    let arg_ty = ArgType::try_from(header.raw_type()).map_err(nom::Err::Failure)?;

    let (after_name, name) = string_ref(header.name_ref(), after_header, false)?;
    match (&state, &name) {
        (ParseState::Initial, StringRef::Inline(Cow::Borrowed("message"))) => {
            *state = ParseState::InMessage;
        }
        _ => {}
    }
    let (value, after_value) = match arg_ty {
        ArgType::Null => (Value::UnsignedInt(1), after_name),
        ArgType::I64 => {
            let (rem, n) = le_i64(after_name)?;
            (Value::SignedInt(n), rem)
        }
        ArgType::U64 => {
            let (rem, n) = le_u64(after_name)?;
            (Value::UnsignedInt(n), rem)
        }
        ArgType::F64 => {
            let (rem, n) = le_f64(after_name)?;
            (Value::Floating(n), rem)
        }
        ArgType::String => {
            let (rem, s) =
                string_ref(header.value_ref(), after_name, matches!(state, ParseState::InMessage))?;
            (Value::Text(s.to_string()), rem)
        }
        ArgType::Pointer | ArgType::Koid | ArgType::I32 | ArgType::U32 => {
            return Err(Err::Failure(ParseError::Unsupported))
        }
    };
    if matches!(state, ParseState::InMessage) {
        *state = ParseState::InArguments;
    }

    Ok((after_value, Argument { name: name.to_string(), value }))
}

fn string_ref(
    ref_mask: u16,
    buf: &[u8],
    support_invalid_utf8: bool,
) -> ParseResult<'_, StringRef<'_>> {
    Ok(if ref_mask == 0 {
        (buf, StringRef::Empty)
    } else if (ref_mask & 1 << 15) == 0 {
        return Err(Err::Failure(ParseError::Unsupported));
    } else {
        // zero out the top bit
        let name_len = (ref_mask & !(1 << 15)) as usize;
        let (after_name, name) = take(name_len)(buf)?;
        let parsed;
        if support_invalid_utf8 {
            parsed = match std::str::from_utf8(name) {
                Ok(valid) => Cow::Borrowed(valid),
                Err(_) => Cow::Owned(format!(
                    "INVALID UTF-8 SEE https://fxbug.dev/88259, message may be corrupted: {}",
                    String::from_utf8_lossy(name)
                )),
            };
        } else {
            parsed = Cow::Borrowed(
                std::str::from_utf8(name).map_err(|e| nom::Err::Error(ParseError::from(e)))?,
            );
        }
        let (_padding, after_padding) = after_name.split_at(after_name.len() % 8);

        (after_padding, StringRef::Inline(parsed))
    })
}

/// Errors which occur when interacting with streams of diagnostic records.
#[derive(Debug, Error)]
pub enum ParseError {
    /// We attempted to parse bytes as a type for which the bytes are not a valid pattern.
    #[error("value out of range")]
    ValueOutOfValidRange,

    /// We attempted to parse or encode values which are not yet supported by this implementation of
    /// the Fuchsia Tracing format.
    #[error("unsupported value type")]
    Unsupported,

    /// We encountered a generic `nom` error while parsing.
    #[error("nom parsing error: {0:?}")]
    Nom(nom::error::ErrorKind),

    /// We failed to parse a complete item.
    #[error("parsing terminated early, needed {0:?}")]
    Incomplete(nom::Needed),
}

impl From<std::str::Utf8Error> for ParseError {
    fn from(_: std::str::Utf8Error) -> Self {
        ParseError::ValueOutOfValidRange
    }
}

impl nom::error::ParseError<&[u8]> for ParseError {
    fn from_error_kind(_input: &[u8], kind: nom::error::ErrorKind) -> Self {
        ParseError::Nom(kind)
    }

    fn append(_input: &[u8], kind: nom::error::ErrorKind, _prev: Self) -> Self {
        // TODO(fxbug.dev/55841) support chaining these
        ParseError::Nom(kind)
    }
}

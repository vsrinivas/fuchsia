//! Parse diagnostic records from streams, returning FIDL-generated structs that match expected
//! diagnostic service APIs.

use {
    crate::{ArgType, Header, StreamError, StringRef},
    fidl_fuchsia_diagnostics_streaming::{Argument, Record, Value},
    nom::{
        bytes::complete::take,
        multi::many0,
        number::complete::{le_f64, le_i64, le_u64},
        Err, IResult,
    },
    std::convert::TryFrom,
};

pub(crate) type ParseResult<'a, T> = IResult<&'a [u8], T, StreamError>;

/// Attempt to parse a diagnostic record from the head of this buffer.
pub fn parse_record(buf: &[u8]) -> ParseResult<'_, Record> {
    let (after_header, header) = parse_header(buf)?;

    if header.raw_type() != crate::TRACING_FORMAT_LOG_RECORD_TYPE {
        return Err(nom::Err::Failure(StreamError::ValueOutOfValidRange));
    }

    let (var_len, timestamp) = le_i64(after_header)?;

    let remaining_record_len = header.variable_length();

    let (after_record, args_buf) = take(remaining_record_len)(var_len)?;
    let (_, arguments) = many0(parse_argument)(args_buf)?;

    Ok((after_record, Record { timestamp, arguments }))
}

fn parse_header(buf: &[u8]) -> ParseResult<'_, Header> {
    let (after, header) = le_u64(buf)?;
    let header = Header(header);

    Ok((after, header))
}

pub(super) fn parse_argument(buf: &[u8]) -> ParseResult<'_, Argument> {
    let (after_header, header) = parse_header(buf)?;
    let arg_ty = ArgType::try_from(header.raw_type()).map_err(nom::Err::Failure)?;

    let (after_name, name) = string_ref(header.name_ref(), after_header)?;

    let (value, after_value) = match arg_ty {
        ArgType::Null => (Value::Unsigned(1), after_name),
        ArgType::I64 => {
            let (rem, n) = le_i64(after_name)?;
            (Value::Signed(n), rem)
        }
        ArgType::U64 => {
            let (rem, n) = le_u64(after_name)?;
            (Value::Unsigned(n), rem)
        }
        ArgType::F64 => {
            let (rem, n) = le_f64(after_name)?;
            (Value::Floating(n), rem)
        }
        ArgType::String => {
            let (rem, s) = string_ref(header.value_ref(), after_name)?;
            (Value::Text(s.to_string()), rem)
        }
        ArgType::Pointer | ArgType::Koid | ArgType::I32 | ArgType::U32 => {
            return Err(Err::Failure(StreamError::Unsupported))
        }
    };

    Ok((after_value, Argument { name: name.to_string(), value }))
}

fn string_ref(ref_mask: u16, buf: &[u8]) -> ParseResult<'_, StringRef<'_>> {
    Ok(if ref_mask == 0 {
        (buf, StringRef::Empty)
    } else if (ref_mask & 1 << 15) == 0 {
        return Err(Err::Failure(StreamError::Unsupported));
    } else {
        // zero out the top bit
        let name_len = (ref_mask & !(1 << 15)) as usize;
        let (after_name, name) = take(name_len)(buf)?;
        let name = std::str::from_utf8(name).map_err(|e| nom::Err::Error(StreamError::from(e)))?;

        let (_padding, after_padding) = after_name.split_at(after_name.len() % 8);

        (after_padding, StringRef::Inline(name))
    })
}

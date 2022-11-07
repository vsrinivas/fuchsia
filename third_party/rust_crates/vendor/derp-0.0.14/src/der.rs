// Copyright 2015 Brian Smith.
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHORS DISCLAIM ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY
// SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
// OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
// CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

//! Building blocks for parsing DER-encoded ASN.1 structures.
//!
//! This module contains the foundational parts of an ASN.1 DER parser.

#[cfg(feature = "cli")]
use std::fmt::{self, Display, Formatter};
use untrusted::{Input, Reader};

use crate::{Error, Result};

const CONSTRUCTED: u8 = 1 << 5;
const CONTEXT_SPECIFIC: u8 = 2 << 6;

/// ASN.1 Tags
#[derive(Debug, Clone, Copy, PartialEq)]
#[repr(u8)]
pub enum Tag {
    Eoc = 0x00,
    Boolean = 0x01,
    Integer = 0x02,
    BitString = 0x03,
    OctetString = 0x04,
    Null = 0x05,
    Oid = 0x06,
    Sequence = CONSTRUCTED | 0x10, // 0x30
    UtcTime = 0x17,
    GeneralizedTime = 0x18,
    ContextSpecificConstructed0 = CONTEXT_SPECIFIC | CONSTRUCTED | 0,
    ContextSpecificConstructed1 = CONTEXT_SPECIFIC | CONSTRUCTED | 1,
    ContextSpecificConstructed2 = CONTEXT_SPECIFIC | CONSTRUCTED | 2,
    ContextSpecificConstructed3 = CONTEXT_SPECIFIC | CONSTRUCTED | 3,
}

impl From<Tag> for usize {
    fn from(tag: Tag) -> Self {
        tag as Self
    }
}

impl From<Tag> for u8 {
    fn from(tag: Tag) -> Self {
        tag as Self
    } // XXX: narrowing conversion.
}

#[cfg(feature = "cli")]
impl Display for Tag {
    fn fmt(&self, f: &mut Formatter) -> fmt::Result {
        let s = match *self {
            Tag::Eoc => "EOC",
            Tag::Boolean => "BOOLEAN",
            Tag::Integer => "INTEGER",
            Tag::BitString => "BIT STRING",
            Tag::OctetString => "OCTET STRING",
            Tag::Null => "NULL",
            Tag::Oid => "OBJECT IDENTIFIER",
            Tag::Sequence => "SEQUENCE",
            Tag::UtcTime => "UTC TIME",
            Tag::GeneralizedTime => "GENERALIZED TIME",
            Tag::ContextSpecificConstructed0 => "CONTEXT SPECIFIC CONSTRUCTED 0",
            Tag::ContextSpecificConstructed1 => "CONTEXT SPECIFIC CONSTRUCTED 1",
            Tag::ContextSpecificConstructed2 => "CONTEXT SPECIFIC CONSTRUCTED 2",
            Tag::ContextSpecificConstructed3 => "CONTEXT SPECIFIC CONSTRUCTED 3",
        };
        f.write_str(s)
    }
}

#[cfg(feature = "cli")]
impl Tag {
    pub fn from_byte(byte: u8) -> Result<Self> {
        match byte {
            x if x == Tag::Eoc as u8 => Ok(Tag::Eoc),
            x if x == Tag::Boolean as u8 => Ok(Tag::Boolean),
            x if x == Tag::Integer as u8 => Ok(Tag::Integer),
            x if x == Tag::BitString as u8 => Ok(Tag::BitString),
            x if x == Tag::OctetString as u8 => Ok(Tag::OctetString),
            x if x == Tag::Null as u8 => Ok(Tag::Null),
            x if x == Tag::Oid as u8 => Ok(Tag::Oid),
            x if x == Tag::Sequence as u8 => Ok(Tag::Sequence),
            x if x == Tag::UtcTime as u8 => Ok(Tag::UtcTime),
            x if x == Tag::GeneralizedTime as u8 => Ok(Tag::GeneralizedTime),
            x if x == Tag::ContextSpecificConstructed0 as u8 => {
                Ok(Tag::ContextSpecificConstructed0)
            }
            x if x == Tag::ContextSpecificConstructed1 as u8 => {
                Ok(Tag::ContextSpecificConstructed1)
            }
            x if x == Tag::ContextSpecificConstructed2 as u8 => {
                Ok(Tag::ContextSpecificConstructed2)
            }
            x if x == Tag::ContextSpecificConstructed3 as u8 => {
                Ok(Tag::ContextSpecificConstructed3)
            }
            _ => Err(Error::UnknownTag),
        }
    }
}

/// Read a tag and return it's value. Errors when the expect and actual tag do not match.
pub fn expect_tag_and_get_value<'a>(input: &mut Reader<'a>, tag: Tag) -> Result<Input<'a>> {
    let (actual_tag, inner) = read_tag_and_get_value(input)?;
    if usize::from(tag) != usize::from(actual_tag) {
        return Err(Error::WrongTag);
    }
    Ok(inner)
}

/// Read the next tag, and return it and its value.
pub fn read_tag_and_get_value<'a>(input: &mut Reader<'a>) -> Result<(u8, Input<'a>)> {
    let tag = input.read_byte()?;
    if (tag & 0x1F) == 0x1F {
        return Err(Error::HighTagNumberForm);
    }

    // If the high order bit of the first byte is set to zero then the length
    // is encoded in the seven remaining bits of that byte. Otherwise, those
    // seven bits represent the number of bytes used to encode the length.
    let length = match input.read_byte()? {
        n if (n & 0x80) == 0 => usize::from(n),
        0x81 => {
            let second_byte = input.read_byte()?;
            if second_byte < 128 {
                return Err(Error::NonCanonical);
            }
            usize::from(second_byte)
        }
        0x82 => {
            let second_byte = usize::from(input.read_byte()?);
            let third_byte = usize::from(input.read_byte()?);
            let combined = (second_byte << 8) | third_byte;
            if combined < 256 {
                return Err(Error::NonCanonical);
            }
            combined
        }
        _ => return Err(Error::LongLengthNotSupported),
    };

    let inner = input.read_bytes(length)?;
    Ok((tag, inner))
}

pub fn read_null<'a>(input: &mut Reader<'a>) -> Result<()> {
    expect_tag_and_get_value(input, Tag::Null).map(|_| ())
}

/// Read a `BIT STRING` with leading byte `0x00` signifying no unused bits.
///
/// ```
/// extern crate derp;
/// extern crate untrusted;
///
/// use untrusted::Input;
///
/// const BIT_STRING: &'static [u8] = &[0x03, 0x04, 0x00, 0x01, 0x02, 0x03];
///
/// fn main() {
///     let input = Input::from(BIT_STRING);
///     let bits = input.read_all(derp::Error::Read, |input| {
///         derp::bit_string_with_no_unused_bits(input)
///     }).unwrap();
///     assert_eq!(bits, Input::from(&[0x01, 0x02, 0x03]));
/// }
/// ```
pub fn bit_string_with_no_unused_bits<'a>(input: &mut Reader<'a>) -> Result<Input<'a>> {
    nested(input, Tag::BitString, |value| {
        let unused_bits_at_end = value.read_byte()?;
        if unused_bits_at_end != 0 {
            return Err(Error::NonZeroUnusedBits);
        }
        Ok(value.read_bytes_to_end())
    })
}

/// Return the value of the given tag and apply a decoding function to it.
///
/// ```
/// extern crate derp;
/// extern crate untrusted;
///
/// use derp::Tag;
/// use untrusted::Input;
///
/// const NESTED: &'static [u8] = &[
///     0x30, 0x14,                                 // seq
///         0x30, 0x0c,                             // seq
///             0x02, 0x04, 0x0a, 0x0b, 0x0c, 0x0d, // x
///             0x02, 0x04, 0x1a, 0x1b, 0x1c, 0x1d, // y
///         0x02, 0x04, 0x01, 0x02, 0x03, 0x04,     // z
/// ];
/// fn main () {
///     let input = Input::from(NESTED);
///     let (x, y, z) = input.read_all(derp::Error::Read, |input| {
///         derp::nested(input, Tag::Sequence, |input| {
///             let (x, y) = derp::nested(input, Tag::Sequence, |input| {
///                 let x = derp::positive_integer(input)?;
///                 let y = derp::positive_integer(input)?;
///                 Ok((x, y))
///             })?;
///             let z = derp::positive_integer(input)?;
///             Ok((x, y, z))
///         })
///     }).unwrap();
///
///     assert_eq!(x, Input::from(&[0x0a, 0x0b, 0x0c, 0x0d]));
///     assert_eq!(y, Input::from(&[0x1a, 0x1b, 0x1c, 0x1d]));
///     assert_eq!(z, Input::from(&[0x01, 0x02, 0x03, 0x04]));
/// }
/// ```
// TODO: investigate taking decoder as a reference to reduce generated code
// size.
pub fn nested<'a, F, R>(input: &mut Reader<'a>, tag: Tag, decoder: F) -> Result<R>
where
    F: FnOnce(&mut untrusted::Reader<'a>) -> Result<R>,
{
    let inner = expect_tag_and_get_value(input, tag)?;
    inner.read_all(Error::Read, decoder)
}

/// Return a non-negative integer.
pub fn nonnegative_integer<'a>(input: &mut Reader<'a>, min_value: u8) -> Result<Input<'a>> {
    // Verify that |input|, which has had any leading zero stripped off, is the
    // encoding of a value of at least |min_value|.
    fn check_minimum(input: untrusted::Input, min_value: u8) -> Result<()> {
        input.read_all(Error::Read, |input| {
            let first_byte = input.read_byte()?;
            if input.at_end() && first_byte < min_value {
                return Err(Error::LessThanMinimum);
            }
            let _ = input.skip_to_end();
            Ok(())
        })
    }

    let value = expect_tag_and_get_value(input, Tag::Integer)?;

    value.read_all(Error::Read, |input| {
        // Empty encodings are not allowed.
        let first_byte = input.read_byte()?;

        if first_byte == 0 {
            if input.at_end() {
                // |value| is the legal encoding of zero.
                if min_value > 0 {
                    return Err(Error::LessThanMinimum);
                }
                return Ok(value);
            }

            let r = input.read_bytes_to_end();
            r.read_all(Error::Read, |input| {
                let second_byte = input.read_byte()?;
                if (second_byte & 0x80) == 0 {
                    // A leading zero is only allowed when the value's high bit
                    // is set.
                    return Err(Error::LeadingZero);
                }
                let _ = input.skip_to_end();
                Ok(())
            })?;
            check_minimum(r, min_value)?;
            return Ok(r);
        }

        // Negative values are not allowed.
        if (first_byte & 0x80) != 0 {
            return Err(Error::NegativeValue);
        }

        let _ = input.read_bytes_to_end();
        check_minimum(value, min_value)?;
        Ok(value)
    })
}

/// Parse as integer with a value in the in the range [0, 255], returning its
/// numeric value. This is typically used for parsing version numbers.
#[inline]
pub fn small_nonnegative_integer(input: &mut untrusted::Reader) -> Result<u8> {
    let value = nonnegative_integer(input, 0)?;
    value.read_all(Error::Read, |input| {
        let r = input.read_byte()?;
        Ok(r)
    })
}

/// Parses a positive DER integer, returning the big-endian-encoded value, sans
/// any leading zero byte.
#[inline]
pub fn positive_integer<'a>(input: &mut Reader<'a>) -> Result<Input<'a>> {
    nonnegative_integer(input, 1)
}

/// Parse a boolean value.
#[inline]
pub fn boolean<'a>(input: &mut Reader<'a>) -> Result<bool> {
    let value = expect_tag_and_get_value(input, Tag::Boolean)?;
    match value.as_slice_less_safe() {
        x if x == &[0x00] => Ok(false),
        x if x == &[0x01] => Ok(true),
        _ => Err(Error::BadBooleanValue),
    }
}

/// For a given length, how many bytes are required to represent it in DER form.
pub fn length_of_length(len: usize) -> u8 {
    let mut i = len;
    let mut num_bytes = 1;

    while i > 255 {
        num_bytes += 1;
        i >>= 8;
    }

    num_bytes
}

#[cfg(test)]
mod tests {
    use super::*;

    fn with_good_i<F, R>(value: &[u8], f: F)
    where
        F: FnOnce(&mut untrusted::Reader) -> Result<R>,
    {
        let r = untrusted::Input::from(value).read_all(Error::Read, f);
        assert!(r.is_ok());
    }

    fn with_bad_i<F, R>(value: &[u8], f: F)
    where
        F: FnOnce(&mut untrusted::Reader) -> Result<R>,
    {
        let r = untrusted::Input::from(value).read_all(Error::Read, f);
        assert!(r.is_err());
    }

    static ZERO_INTEGER: &[u8] = &[0x02, 0x01, 0x00];

    static GOOD_POSITIVE_INTEGERS: &[(&[u8], u8)] = &[
        (&[0x02, 0x01, 0x01], 0x01),
        (&[0x02, 0x01, 0x02], 0x02),
        (&[0x02, 0x01, 0x7e], 0x7e),
        (&[0x02, 0x01, 0x7f], 0x7f),
        // Values that need to have an 0x00 prefix to disambiguate them from
        // them from negative values.
        (&[0x02, 0x02, 0x00, 0x80], 0x80),
        (&[0x02, 0x02, 0x00, 0x81], 0x81),
        (&[0x02, 0x02, 0x00, 0xfe], 0xfe),
        (&[0x02, 0x02, 0x00, 0xff], 0xff),
    ];

    static BAD_NONNEGATIVE_INTEGERS: &[&[u8]] = &[
        &[],           // At end of input
        &[0x02],       // Tag only
        &[0x02, 0x00], // Empty value
        // Length mismatch
        &[0x02, 0x00, 0x01],
        &[0x02, 0x01],
        &[0x02, 0x01, 0x00, 0x01],
        &[0x02, 0x01, 0x01, 0x00], // Would be valid if last byte is ignored.
        &[0x02, 0x02, 0x01],
        // Negative values
        &[0x02, 0x01, 0x80],
        &[0x02, 0x01, 0xfe],
        &[0x02, 0x01, 0xff],
        // Values that have an unnecessary leading 0x00
        &[0x02, 0x02, 0x00, 0x00],
        &[0x02, 0x02, 0x00, 0x01],
        &[0x02, 0x02, 0x00, 0x02],
        &[0x02, 0x02, 0x00, 0x7e],
        &[0x02, 0x02, 0x00, 0x7f],
    ];

    #[test]
    fn test_small_nonnegative_integer() {
        with_good_i(ZERO_INTEGER, |input| {
            assert_eq!(small_nonnegative_integer(input)?, 0x00);
            Ok(())
        });
        for &(test_in, test_out) in GOOD_POSITIVE_INTEGERS.iter() {
            with_good_i(test_in, |input| {
                assert_eq!(small_nonnegative_integer(input)?, test_out);
                Ok(())
            });
        }
        for &test_in in BAD_NONNEGATIVE_INTEGERS.iter() {
            with_bad_i(test_in, |input| {
                let _ = small_nonnegative_integer(input)?;
                Ok(())
            });
        }
    }

    #[test]
    fn test_positive_integer() {
        with_bad_i(ZERO_INTEGER, |input| {
            let _ = positive_integer(input)?;
            Ok(())
        });
        for &(test_in, test_out) in GOOD_POSITIVE_INTEGERS.iter() {
            with_good_i(test_in, |input| {
                let test_out = [test_out];
                assert_eq!(positive_integer(input)?, Input::from(&test_out[..]));
                Ok(())
            });
        }
        for &test_in in BAD_NONNEGATIVE_INTEGERS.iter() {
            with_bad_i(test_in, |input| {
                let _ = positive_integer(input)?;
                Ok(())
            });
        }
    }
}

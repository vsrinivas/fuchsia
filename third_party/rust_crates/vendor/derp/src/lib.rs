//! DER Parser (and Writer)
//!
//! ```
//! extern crate derp;
//! extern crate untrusted;
//!
//! use derp::{Tag, Der};
//! use untrusted::Input;
//!
//! const MY_DATA: &'static [u8] = &[
//!     0x30, 0x18,                                             // sequence
//!         0x05, 0x00,                                         // null
//!         0x30, 0x0e,                                         // sequence
//!             0x02, 0x06, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, // x
//!             0x02, 0x04, 0x0a, 0x0b, 0x0c, 0x0d,             // y
//!         0x03, 0x04, 0x00, 0xff, 0xff, 0xff,                 // bits
//! ];
//!
//! fn main() {
//!     let input = Input::from(MY_DATA);
//!     let (x, y, bits) = input.read_all(derp::Error::Read, |input| {
//!         derp::nested(input, Tag::Sequence, |input| {
//!             derp::read_null(input)?;
//!             let (x, y) = derp::nested(input, Tag::Sequence, |input| {
//!                 let x = derp::positive_integer(input)?;
//!                 let y = derp::positive_integer(input)?;
//!                 Ok((x.as_slice_less_safe(), y.as_slice_less_safe()))
//!             })?;
//!             let bits = derp::bit_string_with_no_unused_bits(input)?;
//!             Ok((x, y, bits.as_slice_less_safe()))
//!         })
//!     }).unwrap();
//!
//!     assert_eq!(x, &[0x01, 0x02, 0x03, 0x04, 0x05, 0x06]);
//!     assert_eq!(y, &[0x0a, 0x0b, 0x0c, 0x0d]);
//!     assert_eq!(bits, &[0xff, 0xff, 0xff]);
//!
//!     let mut buf = Vec::new();
//!     {
//!         let mut der = Der::new(&mut buf);
//!         der.sequence(|der| {
//!             der.null()?;
//!             der.sequence(|der| {
//!                 der.integer(x)?;
//!                 der.integer(y)
//!             })?;
//!             der.bit_string(0, bits)
//!         }).unwrap();
//!     }
//!
//!     assert_eq!(buf.as_slice(), MY_DATA);
//! }
//! ```

use std::fmt::{self, Display};

mod der;
mod writer;

pub use der::*;
pub use writer::*;

#[derive(Debug, PartialEq, Clone, Copy)]
pub enum Error {
    BadBooleanValue,
    LeadingZero,
    LessThanMinimum,
    LongLengthNotSupported,
    HighTagNumberForm,
    Io,
    NegativeValue,
    NonCanonical,
    NonZeroUnusedBits,
    Read,
    UnexpectedEnd,
    UnknownTag,
    WrongTag,
    WrongValue,
}

impl Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        let s = match *self {
            Error::BadBooleanValue => "bad boolean value",
            Error::LeadingZero => "leading zero",
            Error::LessThanMinimum => "less than minimum",
            Error::LongLengthNotSupported => "long length not supported",
            Error::HighTagNumberForm => "high tag number form",
            Error::Io => "I/O",
            Error::NegativeValue => "negative value",
            Error::NonCanonical => "non-canonical",
            Error::NonZeroUnusedBits => "non-zero unused bits",
            Error::Read => "read",
            Error::UnexpectedEnd => "unexpected end",
            Error::UnknownTag => "unknown tag",
            Error::WrongTag => "wrong tag",
            Error::WrongValue => "wrong value",
        };
        s.fmt(f)
    }
}
impl ::std::error::Error for Error {
    fn description(&self) -> &str {
        match *self {
            Error::BadBooleanValue => "bad boolean value",
            Error::LeadingZero => "leading zero",
            Error::LessThanMinimum => "less than minimum",
            Error::LongLengthNotSupported => "long length not supported",
            Error::HighTagNumberForm => "high tag number form",
            Error::Io => "I/O",
            Error::NegativeValue => "negative value",
            Error::NonCanonical => "non-canonical",
            Error::NonZeroUnusedBits => "non-zero unused bits",
            Error::Read => "read",
            Error::UnexpectedEnd => "unexpected end",
            Error::UnknownTag => "unknown tag",
            Error::WrongTag => "wrong tag",
            Error::WrongValue => "wrong value",
        }
    }
}

impl From<untrusted::EndOfInput> for Error {
    fn from(_: untrusted::EndOfInput) -> Error {
        Error::UnexpectedEnd
    }
}

impl From<::std::io::Error> for Error {
    fn from(_: ::std::io::Error) -> Error {
        Error::Io
    }
}

/// Alias for `Result<T, Error>`
pub type Result<T> = ::std::result::Result<T, Error>;

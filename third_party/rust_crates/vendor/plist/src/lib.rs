//! # Plist
//!
//! A rusty plist parser.
//!
//! ## Usage
//!
//! Put this in your `Cargo.toml`:
//!
//! ```toml
//! [dependencies]
//! plist = "0.2"
//! ```
//!
//! And put this in your crate root:
//!
//! ```rust
//! extern crate plist;
//! ```
//!
//! ## Examples
//!
//! ```rust
//! use plist::Plist;
//! use std::fs::File;
//!
//! let file = File::open("tests/data/xml.plist").unwrap();
//! let plist = Plist::read(file).unwrap();
//!
//! match plist {
//!     Plist::Array(_array) => (),
//!     _ => ()
//! }
//! ```
//!
//! ```rust,ignore
//! #[macro_use]
//! extern crate serde_derive;
//!
//! use plist::serde::deserialize;
//! use std::fs::File;
//!
//! #[derive(Deserialize)]
//! struct Info {
//!     name: String,
//!     x: i32
//! }
//!
//! let file = File::open("tests/data/xml.plist").unwrap();
//! let info: Info = deserialize(file).unwrap();
//! ```

extern crate base64;
extern crate byteorder;
extern crate chrono;
extern crate xml as xml_rs;

pub mod binary;
pub mod xml;

mod builder;
mod date;
mod plist;

pub use date::Date;
pub use plist::Plist;

// Optional serde module
#[cfg(feature = "serde")]
#[macro_use]
extern crate serde as serde_base;
#[cfg(feature = "serde")]
pub mod serde;

use std::fmt;
use std::io::{Read, Seek, SeekFrom};
use std::io::Error as IoError;

/// An encoding of a plist as a flat structure.
///
/// Output by the event readers.
///
/// Dictionary keys and values are represented as pairs of values e.g.:
///
/// ```ignore rust
/// StartDictionary
/// StringValue("Height") // Key
/// RealValue(181.2)      // Value
/// StringValue("Age")    // Key
/// IntegerValue(28)      // Value
/// EndDictionary
/// ```
#[derive(Clone, Debug, PartialEq)]
pub enum PlistEvent {
    // While the length of an array or dict cannot be feasably greater than max(usize) this better
    // conveys the concept of an effectively unbounded event stream.
    StartArray(Option<u64>),
    EndArray,

    StartDictionary(Option<u64>),
    EndDictionary,

    BooleanValue(bool),
    DataValue(Vec<u8>),
    DateValue(Date),
    IntegerValue(i64),
    RealValue(f64),
    StringValue(String),
}

pub type Result<T> = ::std::result::Result<T, Error>;

#[derive(Debug)]
pub enum Error {
    InvalidData,
    UnexpectedEof,
    Io(IoError),
    Serde(String),
}

impl ::std::error::Error for Error {
    fn description(&self) -> &str {
        match *self {
            Error::InvalidData => "invalid data",
            Error::UnexpectedEof => "unexpected eof",
            Error::Io(ref err) => err.description(),
            Error::Serde(ref err) => &err,
        }
    }

    fn cause(&self) -> Option<&::std::error::Error> {
        match *self {
            Error::Io(ref err) => Some(err),
            _ => None,
        }
    }
}

impl fmt::Display for Error {
    fn fmt(&self, fmt: &mut fmt::Formatter) -> fmt::Result {
        match *self {
            Error::Io(ref err) => err.fmt(fmt),
            _ => <Self as ::std::error::Error>::description(self).fmt(fmt),
        }
    }
}

impl From<IoError> for Error {
    fn from(err: IoError) -> Error {
        Error::Io(err)
    }
}

pub struct EventReader<R: Read + Seek>(EventReaderInner<R>);

enum EventReaderInner<R: Read + Seek> {
    Uninitialized(Option<R>),
    Xml(xml::EventReader<R>),
    Binary(binary::EventReader<R>),
}

impl<R: Read + Seek> EventReader<R> {
    pub fn new(reader: R) -> EventReader<R> {
        EventReader(EventReaderInner::Uninitialized(Some(reader)))
    }

    fn is_binary(reader: &mut R) -> Result<bool> {
        reader.seek(SeekFrom::Start(0))?;
        let mut magic = [0; 8];
        reader.read_exact(&mut magic)?;
        reader.seek(SeekFrom::Start(0))?;

        Ok(&magic == b"bplist00")
    }
}

impl<R: Read + Seek> Iterator for EventReader<R> {
    type Item = Result<PlistEvent>;

    fn next(&mut self) -> Option<Result<PlistEvent>> {
        let mut reader = match self.0 {
            EventReaderInner::Xml(ref mut parser) => return parser.next(),
            EventReaderInner::Binary(ref mut parser) => return parser.next(),
            EventReaderInner::Uninitialized(ref mut reader) => reader.take().unwrap(),
        };

        let event_reader = match EventReader::is_binary(&mut reader) {
            Ok(true) => EventReaderInner::Binary(binary::EventReader::new(reader)),
            Ok(false) => EventReaderInner::Xml(xml::EventReader::new(reader)),
            Err(err) => {
                ::std::mem::replace(&mut self.0, EventReaderInner::Uninitialized(Some(reader)));
                return Some(Err(err));
            }
        };

        ::std::mem::replace(&mut self.0, event_reader);

        self.next()
    }
}

pub trait EventWriter {
    fn write(&mut self, event: &PlistEvent) -> Result<()>;
}

fn u64_to_usize(len_u64: u64) -> Result<usize> {
    let len = len_u64 as usize;
    if len as u64 != len_u64 {
        return Err(Error::InvalidData); // Too long
    }
    Ok(len)
}

fn u64_option_to_usize(len: Option<u64>) -> Result<Option<usize>> {
    match len {
        Some(len) => Ok(Some(u64_to_usize(len)?)),
        None => Ok(None),
    }
}

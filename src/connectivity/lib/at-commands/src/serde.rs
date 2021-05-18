// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This module contains the top level AT command library methods.  It contains a
//! trait for serialization and deserialization, which is implemented by the high level
//! generated Command and Response types by composing together the parsing and
//! unparsing methods for the low level ASTs with the the generated methods
//! for raising and lowering from and to the low level ASTs and high level generated tyoes.

pub use crate::parser::common::ParseError;
use {
    crate::{highlevel, lowlevel},
    pest,
    std::io,
    thiserror::Error,
};

/// Errors that can occur while deserializing AT commands into high level generated AT command
/// and response types.
#[derive(Clone, Debug, Error, PartialEq)]
pub enum DeserializeErrorCause {
    // IO errors aren't Clone, so just use a String.
    #[error("IO error: {0:?}")]
    IoError(String),
    // Just store the parse errors as Strings so as to not require clients to know about the
    // pest parser types.
    #[error("Parse error: {0:?}")]
    ParseError(String),
    #[error("Bad UTF8: {0:?}")]
    Utf8Error(std::string::FromUtf8Error),
    #[error("Parsed unknown command: {0:?}")]
    UnknownCommand(lowlevel::Command),
    #[error("Parsed unknown response: {0:?}")]
    UnknownResponse(lowlevel::Response),
    #[error("Parsed arguments do not match argument definition: {0:?}")]
    UnknownArguments(lowlevel::Arguments),
}

impl From<io::Error> for DeserializeErrorCause {
    fn from(io_error: io::Error) -> DeserializeErrorCause {
        let string = format!("{:?}", io_error);
        DeserializeErrorCause::IoError(string)
    }
}

impl<RT: pest::RuleType> From<ParseError<RT>> for DeserializeErrorCause {
    fn from(parse_error: ParseError<RT>) -> DeserializeErrorCause {
        let string = format!("{:?}", parse_error);
        DeserializeErrorCause::ParseError(string)
    }
}

impl From<std::string::FromUtf8Error> for DeserializeErrorCause {
    fn from(utf8_error: std::string::FromUtf8Error) -> DeserializeErrorCause {
        DeserializeErrorCause::Utf8Error(utf8_error)
    }
}

/// Error struct containing the cause of a deserialization error and the bytes that caused the error.
#[derive(Clone, Debug, PartialEq)]
pub struct DeserializeError {
    pub cause: DeserializeErrorCause,
    pub bytes: Vec<u8>,
}

#[derive(Debug, Error)]
pub enum SerializeErrorCause {
    #[error("IO error: {0:?}")]
    IoError(io::Error),
}

impl From<io::Error> for SerializeErrorCause {
    fn from(io_error: io::Error) -> SerializeErrorCause {
        SerializeErrorCause::IoError(io_error)
    }
}

// While public traits can't depend on private traits, they can depend on public traits
// in private modules.  By wrapping traits library clients should not have access to in
// a module, clients can be prevented from using them.  The module is pub(crate) to allow
// tests access to the internal traits.
pub(crate) mod internal {
    use {
        super::{DeserializeError, DeserializeErrorCause, ParseError, SerializeErrorCause},
        crate::{
            highlevel, lowlevel,
            lowlevel::write_to::WriteTo,
            parser::{command_grammar, command_parser, response_grammar, response_parser},
            translate,
        },
        std::io,
    };

    /// Trait to specify the parse, raise and lower functions for AT commands or responses.
    /// This is used by the blanket SerDe implementation below to glue together the pieces
    /// in a generic way.
    pub trait SerDeMethods: Sized {
        type Lowlevel: WriteTo;
        type Rule: pest::RuleType;

        fn parse(string: &String) -> Result<Self::Lowlevel, ParseError<Self::Rule>>;
        fn raise(lowlevel: &Self::Lowlevel) -> Result<Self, DeserializeErrorCause>;
        fn lower(&self) -> Self::Lowlevel;

        fn write_to<W: io::Write>(sink: &mut W, lowlevel: &Self::Lowlevel) -> io::Result<()> {
            lowlevel.write_to(sink)
        }
    }

    /// Define the functions used for command serde.
    impl SerDeMethods for highlevel::Command {
        type Lowlevel = lowlevel::Command;
        type Rule = command_grammar::Rule;

        fn parse(string: &String) -> Result<lowlevel::Command, ParseError<Self::Rule>> {
            command_parser::parse(string)
        }

        fn raise(lowlevel: &Self::Lowlevel) -> Result<Self, DeserializeErrorCause> {
            translate::raise_command(lowlevel)
        }

        fn lower(&self) -> Self::Lowlevel {
            translate::lower_command(self)
        }
    }

    /// Define the functions used for response serde.
    impl SerDeMethods for highlevel::Response {
        type Lowlevel = lowlevel::Response;
        type Rule = response_grammar::Rule;

        fn parse(string: &String) -> Result<lowlevel::Response, ParseError<Self::Rule>> {
            response_parser::parse(string)
        }

        fn raise(lowlevel: &Self::Lowlevel) -> Result<Self, DeserializeErrorCause> {
            translate::raise_response(lowlevel)
        }

        fn lower(&self) -> Self::Lowlevel {
            translate::lower_response(self)
        }
    }

    /// Trait implemented for the generated high level AT command and response types
    /// to convert back and forth between individule objects of those types and byte
    /// streams.  This is used by SerDe to parse streams of commands and responses.
    pub trait SerDeOne: Sized {
        fn deserialize_one<R: io::Read>(source: &mut R) -> Result<Self, DeserializeError>;
        fn serialize_one<W: io::Write>(&self, sink: &mut W) -> Result<(), SerializeErrorCause>;
    }

    /// Blanket implementation of SerDeOne which uses SerDeMethods implemenations for commands
    /// and responses to glue the various serde functions together.
    impl<T: SerDeMethods> SerDeOne for T {
        fn deserialize_one<R: io::Read>(source: &mut R) -> Result<Self, DeserializeError> {
            //TODO(fxb/66041) Remove the intermediate String and parse directly from the Read.
            let mut bytes: Vec<u8> = Vec::new();
            let mut string = String::new();

            let read_result = source.read_to_end(&mut bytes).map_err(|err| DeserializeError {
                cause: DeserializeErrorCause::from(err),
                bytes: bytes.clone(),
            });
            let string_result = read_result.and_then(|_byte_count| {
                String::from_utf8(bytes).map_err(|err| {
                    let bytes = Vec::from(err.as_bytes());
                    DeserializeError { cause: DeserializeErrorCause::from(err), bytes }
                })
            });
            let lowlevel_result = string_result.and_then(|s| {
                // Hold on to string for error reporting in other steps.
                string = s;
                Self::parse(&string).map_err(|err| DeserializeError {
                    cause: DeserializeErrorCause::from(err),
                    bytes: string.clone().into_bytes(),
                })
            });
            let highlevel_result = lowlevel_result.and_then(|lowlevel| {
                Self::raise(&lowlevel).map_err(|err| DeserializeError {
                    cause: DeserializeErrorCause::from(err),
                    // This unwrap can't fail since we're inside the and_then, so
                    // lowlevel_result and string_result both must be Ok(_).
                    bytes: string.into_bytes(),
                })
            });

            highlevel_result
        }

        fn serialize_one<W: io::Write>(&self, sink: &mut W) -> Result<(), SerializeErrorCause> {
            let lowlevel = Self::lower(self);
            Self::write_to(sink, &lowlevel)?;

            Ok(())
        }
    }
} // mod internal

/// An error and remaining item to serialize if a serialization failure occurs while
/// serializing multiple items.
#[derive(Debug)]
pub struct SerializeError<T> {
    pub remaining: Vec<T>,
    pub failed: T,
    pub cause: SerializeErrorCause,
}

/// An abstract representaiton of bytes remaining after a deserialization failure.
#[derive(Debug, PartialEq)]
pub struct DeserializeBytes {
    // Public for testing; this allows the use of assert_eq on DeserializeBytes.
    pub(crate) bytes: Vec<u8>,
}

impl DeserializeBytes {
    /// Adds bytes to self from an io::Read source.  This should guarantee that *any* bytes read
    /// from the source are added, even in the case of an IO error.
    fn add_bytes<R: io::Read>(&mut self, source: &mut R) -> Result<(), DeserializeError> {
        let mut more_bytes = Vec::new();
        let byte_count_result = source.read_to_end(&mut more_bytes);
        self.bytes.append(&mut more_bytes);

        match byte_count_result {
            Ok(_byte_count) => Ok(()),
            Err(err) => Err(DeserializeError { cause: err.into(), bytes: more_bytes }),
        }
    }

    fn from(bytes: &[u8]) -> Self {
        DeserializeBytes { bytes: bytes.into() }
    }

    /// Creates an empty `SerializeBytes`.  This is the only method clients should use--
    /// the only other way to get an instance is to get one returned from `deserialize_multiple`.
    pub fn new() -> Self {
        DeserializeBytes { bytes: Vec::new() }
    }
}

impl Default for DeserializeBytes {
    fn default() -> Self {
        Self::new()
    }
}

/// Result from attempt to deserialize multiple items, including the successfully serialized
/// items, an error if one occurred, and the remaining bytes that were not seriialized.
#[derive(Debug, PartialEq)]
pub struct DeserializeResult<T> {
    pub values: Vec<T>,
    pub error: Option<DeserializeError>,
    pub remaining_bytes: DeserializeBytes,
}

/// A trait for serializing or deserializing multiple items at once and defragmenting partially
/// serialized items when new bytes become available.
pub trait SerDe: Sized {
    fn serialize<W: io::Write>(sink: &mut W, values: &[Self]) -> Result<(), SerializeError<Self>>;
    fn deserialize<R: io::Read>(
        source: &mut R,
        existing_bytes: DeserializeBytes,
    ) -> DeserializeResult<Self>;
}

/// Blanket implementation for types that implement SerDe and which break bytestreams on newlines
/// when deserializing.  This is just used for AT commands and responses.
// Clone is needed to return copies of items which failed to serialize.
impl<T: internal::SerDeOne + Clone> SerDe for T {
    fn serialize<W: io::Write>(sink: &mut W, values: &[Self]) -> Result<(), SerializeError<Self>> {
        let mut iter = values.iter();
        for value in &mut iter {
            match value.serialize_one(sink) {
                Ok(()) => (),
                Err(cause) => {
                    return Err(SerializeError {
                        remaining: iter.cloned().collect(),
                        failed: value.clone(),
                        cause,
                    })
                }
            }
        }
        Ok(())
    }

    fn deserialize<R: io::Read>(
        source: &mut R,
        mut existing_bytes: DeserializeBytes,
    ) -> DeserializeResult<T> {
        let mut values = Vec::new();
        if let Err(error) = existing_bytes.add_bytes(source) {
            return DeserializeResult {
                values,
                error: Some(error),
                remaining_bytes: existing_bytes,
            };
        }
        let mut beginning = 0;
        let mut end = 0;
        let bytes = &existing_bytes.bytes;
        let len = bytes.len();
        let should_split = |b| b == &b'\n' || b == &b'\r';
        let is_not_whitespace = |b| !should_split(b);
        loop {
            end += 1;
            if end >= len {
                break;
            };
            if !(should_split(&bytes[end])) {
                continue;
            }
            let mut slice = &bytes[beginning..end];
            // If it's empty or all whitespace due to mulitple consecutive \n or \rs.
            if slice.is_empty() || slice.iter().position(is_not_whitespace).is_none() {
                beginning = end;
                continue;
            }

            let value_result = T::deserialize_one(&mut slice);
            match value_result {
                Ok(value) => values.push(value),
                Err(error) => {
                    // If the received bytes are unparseable, don't put them in the remaining bytes.
                    // Clients can retrieve these from the error struct itself if need be.
                    return DeserializeResult {
                        values,
                        error: Some(error),
                        remaining_bytes: DeserializeBytes::from(&bytes[end..]),
                    };
                }
            };

            beginning = end;
        }
        DeserializeResult {
            values,
            error: None,
            remaining_bytes: DeserializeBytes::from(&bytes[beginning..]),
        }
    }
}

/// Wrap a Success case in a Response.
pub fn success(success: highlevel::Success) -> highlevel::Response {
    highlevel::Response::Success(success)
}

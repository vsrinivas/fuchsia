// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Custom error types for the netstack.

use failure::Fail;

/// Results returned from many functions in the netstack.
pub(crate) type Result<T> = std::result::Result<T, NetstackError>;

/// Results returned from parsing functions in the netstack.
pub(crate) type ParseResult<T> = std::result::Result<T, ParseError>;

/// Top-level error type the netstack.
#[derive(Fail, Debug)]
pub(crate) enum NetstackError {
    #[fail(display = "{}", _0)]
    /// Errors related to packet parsing.
    Parse(#[cause] ParseError),
    // Add error types here as we add more to the stack
}

/// Error type for packet parsing.
#[derive(Fail, Debug, PartialEq)]
#[allow(missing_docs)]
pub(crate) enum ParseError {
    #[fail(display = "Operation is not supported")]
    NotSupported,
    #[fail(display = "Operation was not expected in this context")]
    NotExpected,
    #[fail(display = "Invalid checksum")]
    Checksum,
    #[fail(display = "Packet is not formatted properly")]
    Format,
}

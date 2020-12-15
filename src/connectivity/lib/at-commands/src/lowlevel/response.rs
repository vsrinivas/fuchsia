// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This module contains an AST for AT command responses.
//!
//! The format of these is not specifed in any one place in the spec, but they are
//! described thoughout HFP 1.8.

use {
    crate::lowlevel::{arguments, write_to::WriteTo},
    std::io,
};

/// Responses and indications.
#[derive(Debug, Clone, PartialEq)]
pub enum Response {
    /// The success indication.  Its format is described in HFP v1.8 Section 4.34.1, and it is
    /// used throughout the spec.
    Ok,
    /// The error indication.  Its format is described in HFP v1.8 Section 4.34.1, and it is
    /// used throughout the spec.
    Error,
    /// A set of hardcoded specific error indications.  They are described in HFP v1.8 Section 4.34.2.
    HardcodedError(HardcodedError),
    /// Error codes used with the +CME ERROR indication.  Described in HFP v1.8 4.34.2
    CmeError(i64),
    /// All other non-error responses.  These are described throughout the HFP v1.8 spec.
    Success { name: String, is_extension: bool, arguments: arguments::Arguments },
}

impl WriteTo for Response {
    fn write_to<W: io::Write>(&self, sink: &mut W) -> io::Result<()> {
        // Responses are delimited on both sides by CRLF.
        sink.write_all(b"\r\n")?;
        match self {
            Response::Ok => sink.write_all(b"OK")?,
            Response::Error => sink.write_all(b"ERROR")?,
            Response::HardcodedError(error) => error.write_to(sink)?,
            Response::CmeError(error_code) => {
                sink.write_all(b"+CME ERROR: ")?;
                sink.write_all(error_code.to_string().as_bytes())?;
            }
            Response::Success { name, is_extension, arguments } => {
                if *is_extension {
                    sink.write_all(b"+")?
                }
                sink.write_all(name.as_bytes())?;
                sink.write_all(b": ")?;
                arguments.write_to(sink)?;
            }
        };
        // Responses are delimited on both sides by CRLF.
        sink.write_all(b"\r\n")
    }
}

// Hardedcoded error indications described in HFP v1.8 Section 4.34.2
#[derive(Debug, Clone, PartialEq)]
pub enum HardcodedError {
    NoCarrier,
    Busy,
    NoAnswer,
    Delayed,
    Blacklist,
}

impl WriteTo for HardcodedError {
    fn write_to<W: io::Write>(&self, sink: &mut W) -> io::Result<()> {
        match self {
            HardcodedError::NoCarrier => sink.write_all(b"NO CARRIER"),
            HardcodedError::Busy => sink.write_all(b"BUSY"),
            HardcodedError::NoAnswer => sink.write_all(b"NO ANSWER"),
            HardcodedError::Delayed => sink.write_all(b"DELAYED"),
            HardcodedError::Blacklist => sink.write_all(b"BLACKLIST"),
        }
    }
}

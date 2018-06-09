// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Custom error types for the netstack

/// Top-level Error type the netstack
#[derive(Fail, Debug)]
pub enum NetworkError {
    #[fail(display = "{}", _0)]
    /// Errors related to Packet Parsing
    Parse(#[cause] ParseError),
    // Add Error types here as we add more to the stack
}

/// Error type for packet parsing
#[derive(Fail, Debug, PartialEq)]
#[allow(missing_docs)]
pub enum ParseError {
    #[fail(display = "Operation is not supported")]
    NotSupported,
    #[fail(display = "Invalid checksum")]
    Checksum,
    #[fail(display = "Packet is not formatted properly")]
    Format,
}

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::error;
use std::fmt;
use std::io;
use std::str::Utf8Error;

// Directly include schema in the binary. This is used to parse component manifests.
pub const CMX_SCHEMA: &str = include_str!("../cmx_schema.json");

/// Enum type that can represent any error encountered by a cmx operation.
#[derive(Debug)]
pub enum Error {
    InvalidArgs(String),
    Io(io::Error),
    Parse(String),
    Internal(String),
    Utf8(Utf8Error),
}

impl error::Error for Error {}

impl Error {
    pub fn invalid_args(err: impl Into<String>) -> Self {
        Error::InvalidArgs(err.into())
    }

    pub fn parse(err: impl Into<String>) -> Self {
        Error::Parse(err.into())
    }

    pub fn internal(err: impl Into<String>) -> Self {
        Error::Internal(err.into())
    }
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match &self {
            Error::InvalidArgs(err) => write!(f, "Invalid args: {}", err),
            Error::Io(err) => write!(f, "IO error: {}", err),
            Error::Parse(err) => write!(f, "Parse error: {}", err),
            Error::Internal(err) => write!(f, "Internal error: {}", err),
            Error::Utf8(err) => write!(f, "UTF8 error: {}", err),
        }
    }
}

impl From<io::Error> for Error {
    fn from(err: io::Error) -> Self {
        Error::Io(err)
    }
}

impl From<Utf8Error> for Error {
    fn from(err: Utf8Error) -> Self {
        Error::Utf8(err)
    }
}


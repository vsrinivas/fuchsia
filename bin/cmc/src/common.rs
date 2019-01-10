// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use json5;
use serde_json;
use serde_json::Value;
use std::error;
use std::fmt;
use std::io;
use std::str::Utf8Error;

// Directly include schemas in the binary. These are used to parse component manifests.
pub const CM_SCHEMA: &str = include_str!("../cm_schema.json");
pub const CML_SCHEMA: &str = include_str!("../cml_schema.json");
pub const CMX_SCHEMA: &str = include_str!("../cmx_schema.json");

/// Keyword definitions and syntax helpers for CM and CML.
pub mod keywords {
    use lazy_static::lazy_static;
    use regex::Regex;

    pub const DIRECTORY: &str = "directory";
    pub const SERVICE: &str = "service";

    lazy_static! {
        pub static ref CHILD_RE: Regex = Regex::new(r"^#([A-Za-z0-9\-_]+)$").unwrap();
        pub static ref FROM_RE: Regex = Regex::new(r"^(realm|self|#[A-Za-z0-9\-_]+)$").unwrap();
        pub static ref NAME_RE: Regex = Regex::new(r"^[A-Za-z0-9\-_]+$").unwrap();
    }
}

/// Represents a JSON schema.
pub type JsonSchemaStr<'a> = &'a str;

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

pub fn from_json_str(json: &str) -> Result<Value, Error> {
    let v = serde_json::from_str(json)
        .map_err(|e| Error::parse(format!("Couldn't read input as JSON: {}", e)))?;
    Ok(v)
}

pub fn from_json5_str(json5: &str) -> Result<Value, Error> {
    let v: Value = json5::from_str(json5)
        .map_err(|e| Error::parse(format!("Couldn't read input as JSON5: {}", e)))?;
    Ok(v)
}

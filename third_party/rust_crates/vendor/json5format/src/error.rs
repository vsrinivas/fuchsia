// Copyright (c) 2020 Google LLC All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#![deny(missing_docs)]

/// A location within a document buffer or document file. This module uses `Location` to identify
/// to refer to locations of JSON5 syntax errors, while parsing) and also to locations in this Rust
/// source file, to improve unit testing output.
pub struct Location {
    /// The name of the JSON5 document file being parsed and formatted (if provided).
    pub file: Option<String>,

    /// A line number within the JSON5 document. (The first line at the top of the document/file is
    /// line 1.)
    pub line: usize,

    /// A character column number within the specified line. (The left-most character of the line is
    /// column 1).
    pub col: usize,
}

impl Location {
    /// Create a new `Location` for the given source document location.
    pub fn new(file: Option<String>, line: usize, col: usize) -> Self {
        Location { file, line, col }
    }
}

impl std::fmt::Display for Location {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        if let Some(file) = &self.file {
            write!(formatter, "{}:{}:{}", file, self.line, self.col)
        } else {
            write!(formatter, "{}:{}", self.line, self.col)
        }
    }
}

impl std::fmt::Debug for Location {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(formatter, "{}", &self)
    }
}

/// Errors produced by the json5format library.
#[derive(Debug)]
pub enum Error {
    /// A formatter configuration option was invalid.
    Configuration(String),

    /// A syntax error was encountered while parsing a JSON5 document.
    Parse(Option<Location>, String),

    /// The parser or formatter entered an unexpected state. An `Error::Internal` likely indicates
    /// there is a software bug in the json5format library.
    Internal(Option<Location>, String),

    /// This error is only produced by internal test functions to indicate a test result did not
    /// match expectations.
    TestFailure(Option<Location>, String),
}

impl std::error::Error for Error {}

impl Error {
    /// Return a configuration error.
    /// # Arguments
    ///   * err - The error message.
    pub fn configuration(err: impl std::fmt::Display) -> Self {
        Error::Configuration(err.to_string())
    }

    /// Return a parsing error.
    /// # Arguments
    ///   * location - Optional location in the JSON5 document where the error was detected.
    ///   * err - The error message.
    pub fn parse(location: Option<Location>, err: impl std::fmt::Display) -> Self {
        Error::Parse(location, err.to_string())
    }

    /// Return an internal error (indicating an error in the software implementation itself).
    /// # Arguments
    ///   * location - Optional location in the JSON5 document where the error was detected,
    ///     which might be available if the error occurred while parsing the document.
    ///   * err - The error message.
    pub fn internal(location: Option<Location>, err: impl Into<String>) -> Self {
        Error::Internal(location, err.into())
    }

    /// Return a TestFailure error.
    /// # Arguments
    ///   * location - Optional Rust source code location where the test failed.
    ///   * err - The error message.
    pub fn test_failure(location: Option<Location>, err: impl Into<String>) -> Self {
        Error::TestFailure(location, err.into())
    }
}

impl std::fmt::Display for Error {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let (prefix, loc, err) = match &self {
            Error::Configuration(err) => ("Configuration error", &None, err.to_string()),
            Error::Parse(loc, err) => ("Parse error", loc, err.to_string()),
            Error::Internal(loc, err) => ("Internal error", loc, err.to_string()),
            Error::TestFailure(loc, err) => ("Test failure", loc, err.to_string()),
        };
        match loc {
            Some(loc) => write!(f, "{}: {}: {}", prefix, loc, err),
            None => write!(f, "{}: {}", prefix, err),
        }
    }
}

/// Create a `TestFailure` error including the source file location of the macro call.
///
/// # Example:
///
/// ```no_run
/// # use json5format::Error;
/// # use json5format::Location;
/// # use json5format::test_error;
/// # fn test() -> std::result::Result<(),Error> {
/// return Err(test_error!("error message"));
/// # }
/// # test();
/// ```
#[macro_export]
macro_rules! test_error {
    ($err:expr) => {
        Error::test_failure(
            Some(Location::new(Some(file!().to_string()), line!() as usize, column!() as usize)),
            $err,
        )
    };
}

// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

use diagnostics_stream::parse::ParseError;
use thiserror::Error;

use super::message::{fx_log_severity_t, MAX_TAGS, MAX_TAG_LEN, MIN_PACKET_SIZE};

#[derive(Debug, Error)]
pub enum StreamError {
    #[error("buffer too small ({len}) to contain a valid log message (min {})", MIN_PACKET_SIZE)]
    ShortRead { len: usize },

    #[error("message incorrectly terminated, found {terminator}, expected 0")]
    NotNullTerminated { terminator: u8 },

    #[error("provided {} or more tags", MAX_TAGS + 1)]
    TooManyTags,

    #[error("provided tag at position {index} with length {len} (max {})", MAX_TAG_LEN)]
    TagTooLong { index: usize, len: usize },

    #[error("incorrect or corrupt metadata indicates to read past end of buffer")]
    OutOfBounds,

    #[error("invalid or corrupt severity received: {provided}")]
    InvalidSeverity { provided: fx_log_severity_t },

    #[error("unrecognized value type encountered")]
    UnrecognizedValue,

    #[error("wrong value type encountered, expected integer, found {found} {value}")]
    ExpectedInteger { value: String, found: &'static str },

    #[error("couldn't parse message: {parse_error:?}")]
    ParseError {
        #[from]
        parse_error: ParseError,
    },

    #[error("string with invalid UTF-8 encoding: {source:?}")]
    InvalidString {
        #[from]
        source: std::str::Utf8Error,
    },

    #[error("couldn't read from socket: {source:?}")]
    Io {
        #[from]
        source: std::io::Error,
    },

    #[error("socket was closed and no messages remain")]
    Closed,
}

#[cfg(test)]
impl PartialEq for StreamError {
    fn eq(&self, other: &Self) -> bool {
        use StreamError::*;
        match (self, other) {
            (ShortRead { len }, ShortRead { len: len2 }) => len == len2,
            (NotNullTerminated { terminator }, NotNullTerminated { terminator: t2 }) => {
                terminator == t2
            }
            (TooManyTags, TooManyTags) => true,
            (TagTooLong { index, len }, TagTooLong { index: i2, len: l2 }) => {
                index == i2 && len == l2
            }
            (OutOfBounds, OutOfBounds) => true,
            (InvalidSeverity { provided }, InvalidSeverity { provided: p2 }) => provided == p2,
            (InvalidString { source }, InvalidString { source: s2 }) => source == s2,
            (Io { source }, Io { source: s2 }) => source.kind() == s2.kind(),
            _ => false,
        }
    }
}

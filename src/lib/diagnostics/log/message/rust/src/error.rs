// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

use crate::message::{fx_log_severity_t, MAX_TAGS, MAX_TAG_LEN, MIN_PACKET_SIZE};
use diagnostics_log_encoding::parse::ParseError;
use thiserror::Error;

#[derive(Debug, Error)]
pub enum MessageError {
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
    #[error("incorrect or corrupt metadata indicates to read past end of buffer")]
    OutOfBounds,
    #[error("provided tag at position {index} with length {len} (max {})", MAX_TAG_LEN)]
    TagTooLong { index: usize, len: usize },
    #[error("provided {} or more tags", MAX_TAGS + 1)]
    TooManyTags,
    #[error("message incorrectly terminated, found {terminator}, expected 0")]
    NotNullTerminated { terminator: u8 },
    #[error("buffer too small ({len}) to contain a valid log message (min {})", MIN_PACKET_SIZE)]
    ShortRead { len: usize },
}

impl PartialEq for MessageError {
    fn eq(&self, other: &Self) -> bool {
        use MessageError::*;
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
            _ => false,
        }
    }
}

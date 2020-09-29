// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This module defines errors used in the crate. Some of these error are
//! converted C/C++ error types.

use thiserror::Error;

/// Defines errors used in the crate.
/// Enum defines types of errors and their human readable messages.
#[repr(C)]
#[no_mangle]
#[derive(Debug, Copy, Clone, Error, PartialEq)]
pub enum Error {
    /// Given extent cannot override already added extent. This may happen
    /// because a part of extent having higher priority already exists.
    #[error("block range exists with stricter properties")]
    CannotOverride,

    /// Given extent already exists with same set of properties.
    #[error("block range exists with same properties")]
    Exists,

    /// Current options do not allow extraction of this type of block.
    #[error("current options do not allow extraction of this type of block")]
    NotAllowed,

    /// Failed to seek input stream.
    #[error("failed to seek")]
    SeekFailed,

    /// Failed to read the input stream.
    #[error("failed to read")]
    ReadFailed,

    /// Failed to write the extracted image out out stream.
    #[error("failed to write")]
    WriteFailed,

    /// The extent has invalid range.
    #[error("invalid range")]
    InvalidRange,

    /// The data lenght and range lenght do not match.
    #[error("data length does not match range")]
    InvalidDataLength,

    /// The offset found is invalid.
    #[error("offset found is invalid")]
    InvalidOffset,

    /// The invalid argument.
    #[error("invalid argument")]
    InvalidArgument,

    /// The failed to parse.
    #[error("failed to parse")]
    ParseFailed,
}

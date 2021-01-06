// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use thiserror::Error;

/// Errors associated with parsing an RFCOMM Frame.
#[derive(Error, Debug)]
pub enum FrameParseError {
    #[error("Provided buffer is too small")]
    BufferTooSmall,
    #[error("Invalid buffer size provided. Expected: {}, Actual: {}", .0, .1)]
    InvalidBufferLength(usize, usize),
    #[error("FCS check for the Frame failed")]
    FCSCheckFailed,
    #[error("DLCI ({:?}) is invalid", .0)]
    InvalidDLCI(u8),
    #[error("Frame is invalid")]
    InvalidFrame,
    #[error("Frame type is unsupported")]
    UnsupportedFrameType,
    #[error("Mux Command type {} is unsupported", .0)]
    UnsupportedMuxCommandType(u8),
    #[error("Value is out of range")]
    OutOfRange,
    #[error(transparent)]
    Other(#[from] anyhow::Error),
}

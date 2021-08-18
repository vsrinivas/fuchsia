// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::num::TryFromIntError;

use thiserror::Error;

#[derive(Error, Debug)]
pub enum NvdataError {
    #[error("Invalid nvram signature")]
    InvalidSignature,
    #[error("CRC mismatch")]
    CrcMismatch,
    #[error("Unknown nvdata key")]
    UnknownKey,
    #[error("Need at least {0} bytes of nvdata")]
    NotEnoughSpace(usize),
    #[error("Setting is not writable")]
    NotWritable,
    #[error("Integer value too large")]
    InvalidValue(TryFromIntError),
    #[error("Expected 1 or 0")]
    ExpectedBool,
    #[error("Field can only be cleared.")]
    ClearOnly,
    #[error(transparent)]
    Other(#[from] anyhow::Error),
}

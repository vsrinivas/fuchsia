// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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
    #[error(transparent)]
    Other(#[from] anyhow::Error),
}

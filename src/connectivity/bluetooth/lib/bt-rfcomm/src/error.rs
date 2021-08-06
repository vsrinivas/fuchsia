// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use thiserror::Error;

use crate::frame::FrameParseError;
use crate::{Role, DLCI};

/// Errors that occur during the usage of the RFCOMM library.
#[derive(Error, Debug)]
pub enum Error {
    #[error("Error parsing frame: {:?}", .0)]
    Frame(FrameParseError),
    #[error("Invalid DLCI: {:?}", .0)]
    InvalidDLCI(DLCI),
    #[error("Invalid role: {:?}", .0)]
    InvalidRole(Role),
    #[error(transparent)]
    Other(#[from] anyhow::Error),
}

impl From<FrameParseError> for Error {
    fn from(src: FrameParseError) -> Self {
        Self::Frame(src)
    }
}

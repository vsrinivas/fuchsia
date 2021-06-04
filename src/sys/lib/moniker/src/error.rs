// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {log::*, thiserror::Error};

/// Errors produced by `MonikerEnvironment`.
#[derive(Debug, Error, Clone, PartialEq, Eq)]
pub enum MonikerError {
    #[error("invalid moniker: {}", rep)]
    InvalidMoniker { rep: String },
    #[error("invalid moniker part: {0}")]
    InvalidMonikerPart(String),
}

impl MonikerError {
    pub fn invalid_moniker(rep: impl Into<String>) -> MonikerError {
        MonikerError::InvalidMoniker { rep: rep.into() }
    }

    pub fn invalid_moniker_part(part: impl Into<String>) -> MonikerError {
        MonikerError::InvalidMonikerPart(part.into())
    }
}

// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::errors::FxfsError,
    fuchsia_zircon::Status,
    std::convert::{TryFrom, TryInto},
};

impl TryFrom<anyhow::Error> for FxfsError {
    type Error = anyhow::Error;
    fn try_from(err: anyhow::Error) -> Result<Self, Self::Error> {
        if let Some(fxfs_error) = err.root_cause().downcast_ref::<FxfsError>() {
            Ok(fxfs_error.clone())
        } else {
            Err(err)
        }
    }
}

/// Narrowing conversion from anyhow::Error to FxfsError. Any Errors that have a non-FxfsError root
/// cause will be converted to Fxfs::Internal.
pub(super) fn fxfs_error(err: anyhow::Error) -> FxfsError {
    err.try_into().unwrap_or(FxfsError::Internal)
}

impl From<FxfsError> for Status {
    fn from(err: FxfsError) -> Status {
        match err {
            FxfsError::AlreadyExists => Status::ALREADY_EXISTS,
            FxfsError::Inconsistent => Status::IO_DATA_INTEGRITY,
            FxfsError::Internal => Status::INTERNAL,
            FxfsError::NotDir => Status::NOT_DIR,
            FxfsError::NotFound => Status::NOT_FOUND,
        }
    }
}

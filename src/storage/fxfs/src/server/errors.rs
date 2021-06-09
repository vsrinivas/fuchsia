// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {crate::errors::FxfsError, fuchsia_zircon::Status};

impl From<FxfsError> for Status {
    fn from(err: FxfsError) -> Status {
        match err {
            FxfsError::AlreadyExists => Status::ALREADY_EXISTS,
            FxfsError::Inconsistent => Status::IO_DATA_INTEGRITY,
            FxfsError::Internal => Status::INTERNAL,
            FxfsError::NotDir => Status::NOT_DIR,
            FxfsError::NotFile => Status::NOT_FILE,
            FxfsError::NotFound => Status::NOT_FOUND,
            FxfsError::NotEmpty => Status::NOT_EMPTY,
            FxfsError::ReadOnlyFilesystem => Status::ACCESS_DENIED,
            FxfsError::NoSpace => Status::NO_SPACE,
            FxfsError::Deleted => Status::ACCESS_DENIED,
            FxfsError::InvalidArgs => Status::INVALID_ARGS,
            FxfsError::TooBig => Status::FILE_BIG,
            FxfsError::InvalidVersion => Status::NOT_SUPPORTED,
        }
    }
}

pub(super) fn map_to_status(err: anyhow::Error) -> Status {
    if let Some(status) = err.root_cause().downcast_ref::<Status>() {
        status.clone()
    } else if let Some(fxfs_error) = err.root_cause().downcast_ref::<FxfsError>() {
        fxfs_error.clone().into()
    } else {
        Status::INTERNAL
    }
}

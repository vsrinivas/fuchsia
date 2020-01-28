// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_merkle::ParseHashError, fuchsia_pkg::ParsePackagePathError, std::io, thiserror::Error,
};

#[derive(Debug, Error)]
pub enum PathHashMappingError {
    #[error("entry has no '=': '{entry:?}'")]
    EntryHasNoEqualsSign { entry: String },

    #[error("io error: '{0}'")]
    IoError(#[from] io::Error),

    #[error("invalid hash: '{0}'")]
    ParseHash(#[from] ParseHashError),

    #[error("invalid package path: '{0}'")]
    ParsePackagePath(#[from] ParsePackagePathError),
}

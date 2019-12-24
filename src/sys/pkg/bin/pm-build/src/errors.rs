// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_pkg::{BuildError, CreationManifestError, MetaPackageError};
use std::io;
use thiserror::Error;

#[derive(Debug, Error)]
pub enum PmBuildError {
    #[error("io error: {}", _0)]
    IoError(io::Error),

    #[error("creation manifest error: {}", _0)]
    CreationManifest(CreationManifestError),

    #[error("meta package error: {}", _0)]
    MetaPackage(MetaPackageError),

    #[error("build error: {}", _0)]
    Build(BuildError),

    #[error("signing key file should be 64 bytes but was: {}", actual_size)]
    WrongSizeSigningKey { actual_size: u64 },
}

impl From<io::Error> for PmBuildError {
    fn from(err: io::Error) -> Self {
        PmBuildError::IoError(err)
    }
}

impl From<CreationManifestError> for PmBuildError {
    fn from(err: CreationManifestError) -> Self {
        PmBuildError::CreationManifest(err)
    }
}

impl From<MetaPackageError> for PmBuildError {
    fn from(err: MetaPackageError) -> Self {
        PmBuildError::MetaPackage(err)
    }
}

impl From<BuildError> for PmBuildError {
    fn from(err: BuildError) -> Self {
        PmBuildError::Build(err)
    }
}

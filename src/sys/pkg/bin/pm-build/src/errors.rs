// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::Fail;
use fuchsia_pkg::{BuildError, CreationManifestError, MetaPackageError};
use std::io;

#[derive(Debug, Fail)]
pub enum PmBuildError {
    #[fail(display = "io error: {}", _0)]
    IoError(#[cause] io::Error),

    #[fail(display = "creation manifest error: {}", _0)]
    CreationManifest(#[cause] CreationManifestError),

    #[fail(display = "meta package error: {}", _0)]
    MetaPackage(#[cause] MetaPackageError),

    #[fail(display = "build error: {}", _0)]
    Build(#[cause] BuildError),

    #[fail(display = "signing key file should be 64 bytes but was: {}", actual_size)]
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

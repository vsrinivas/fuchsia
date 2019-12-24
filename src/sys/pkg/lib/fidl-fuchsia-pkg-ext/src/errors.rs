// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use hex;

use thiserror::Error;

#[derive(Error, Debug, PartialEq)]
pub enum BlobIdParseError {
    #[error("cannot contain uppercase hex characters")]
    CannotContainUppercase,

    #[error("invalid length, expected 32 hex bytes, got {}", _0)]
    InvalidLength(usize),

    #[error("{}", _0)]
    FromHexError(hex::FromHexError),
}

impl From<hex::FromHexError> for BlobIdParseError {
    fn from(err: hex::FromHexError) -> Self {
        BlobIdParseError::FromHexError(err)
    }
}

#[derive(Error, Debug, PartialEq)]
pub enum RepositoryParseError {
    #[error("unsupported key type")]
    UnsupportedKeyType,

    #[error("missing required field repo_url")]
    RepoUrlMissing,

    #[error("missing required field mirror_url")]
    MirrorUrlMissing,

    #[error("missing required field subscribe")]
    SubscribeMissing,

    #[error("invalid repository url: {}", _0)]
    InvalidRepoUrl(fuchsia_url::pkg_url::ParseError),

    #[error("invalid update package url: {}", _0)]
    InvalidUpdatePackageUrl(fuchsia_url::pkg_url::ParseError),

    #[error("invalid root version: {}", _0)]
    InvalidRootVersion(u32),

    #[error("invalid root threshold: {}", _0)]
    InvalidRootThreshold(u32),
}

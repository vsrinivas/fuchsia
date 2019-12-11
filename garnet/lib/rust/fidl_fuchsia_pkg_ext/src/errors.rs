// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use hex;

use failure::Fail;

#[derive(Fail, Debug, PartialEq)]
pub enum BlobIdParseError {
    #[fail(display = "cannot contain uppercase hex characters")]
    CannotContainUppercase,

    #[fail(display = "invalid length, expected 32 hex bytes, got {}", _0)]
    InvalidLength(usize),

    #[fail(display = "{}", _0)]
    FromHexError(#[cause] hex::FromHexError),
}

impl From<hex::FromHexError> for BlobIdParseError {
    fn from(err: hex::FromHexError) -> Self {
        BlobIdParseError::FromHexError(err)
    }
}

#[derive(Fail, Debug, PartialEq)]
pub enum RepositoryParseError {
    #[fail(display = "unsupported key type")]
    UnsupportedKeyType,

    #[fail(display = "missing required field repo_url")]
    RepoUrlMissing,

    #[fail(display = "missing required field mirror_url")]
    MirrorUrlMissing,

    #[fail(display = "missing required field subscribe")]
    SubscribeMissing,

    #[fail(display = "invalid repository url: {}", _0)]
    InvalidRepoUrl(#[cause] fuchsia_url::pkg_url::ParseError),

    #[fail(display = "invalid update package url: {}", _0)]
    InvalidUpdatePackageUrl(#[cause] fuchsia_url::pkg_url::ParseError),

    #[fail(display = "invalid root version: {}", _0)]
    InvalidRootVersion(u32),
}

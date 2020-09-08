// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use thiserror::Error;

#[derive(Error, Debug, PartialEq)]
pub enum BlobIdParseError {
    #[error("cannot contain uppercase hex characters")]
    CannotContainUppercase,

    #[error("invalid length, expected 32 hex bytes, got {0}")]
    InvalidLength(usize),

    #[error("invalid hex: {0}")]
    FromHexError(#[from] hex::FromHexError),
}

#[derive(Error, Debug)]
pub enum MirrorConfigError {
    #[error("Mirror URLs must have schemes")]
    MirrorUrlMissingScheme,

    #[error("Blob mirror URLs must have schemes")]
    BlobMirrorUrlMissingScheme,
}

#[derive(Error, Debug)]
pub enum RepositoryParseError {
    #[error("unsupported key type")]
    UnsupportedKeyType,

    #[error("missing required field repo_url")]
    RepoUrlMissing,

    #[error("missing required field mirror_url")]
    MirrorUrlMissing,

    #[error("missing required field subscribe")]
    SubscribeMissing,

    #[error("invalid repository url: {0}")]
    InvalidRepoUrl(fuchsia_url::pkg_url::ParseError),

    #[error("invalid update package url: {0}")]
    InvalidUpdatePackageUrl(fuchsia_url::pkg_url::ParseError),

    #[error("invalid root version: {0}")]
    InvalidRootVersion(u32),

    #[error("invalid root threshold: {0}")]
    InvalidRootThreshold(u32),

    #[error("invalid uri: {0}")]
    InvalidUri(#[from] http::uri::InvalidUri),

    #[error("invalid config: {0}")]
    MirrorConfig(#[from] MirrorConfigError),
}

#[derive(Error, Debug)]
pub enum RepositoryUrlParseError {
    #[error("invalid repository url: {0}")]
    InvalidRepoUrl(fuchsia_url::pkg_url::ParseError),
}

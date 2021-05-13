// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use thiserror::Error;

#[derive(Debug, Error)]
pub enum ResolveError {
    #[error("transport error")]
    Fidl(#[from] fidl::Error),

    #[error("the resolver encountered an otherwise unspecified error while handling the request")]
    Internal,

    #[error("the resolver does not have permission to fetch a package blob")]
    AccessDenied,

    #[error("some unspecified error during I/O")]
    Io,

    #[error("the blob does not exist")]
    BlobNotFound,

    #[error("the package does not exist")]
    PackageNotFound,

    #[error("the resolver does not know about the repo")]
    RepoNotFound,

    #[error("there is no space available to store the package or metadata")]
    NoSpace,

    #[error("the resolver is currently unable to fetch a package blob")]
    UnavailableBlob,

    #[error("the resolver is currently unable to fetch a repository's metadata")]
    UnavailableRepoMetadata,

    #[error("the `package_url` provided to resolver is invalid")]
    InvalidUrl,
}

impl From<fidl_fuchsia_pkg::ResolveError> for ResolveError {
    fn from(e: fidl_fuchsia_pkg::ResolveError) -> Self {
        match e {
            fidl_fuchsia_pkg::ResolveError::Internal => ResolveError::Internal,
            fidl_fuchsia_pkg::ResolveError::AccessDenied => ResolveError::AccessDenied,
            fidl_fuchsia_pkg::ResolveError::Io => ResolveError::Io,
            fidl_fuchsia_pkg::ResolveError::BlobNotFound => ResolveError::BlobNotFound,
            fidl_fuchsia_pkg::ResolveError::PackageNotFound => ResolveError::PackageNotFound,
            fidl_fuchsia_pkg::ResolveError::RepoNotFound => ResolveError::RepoNotFound,
            fidl_fuchsia_pkg::ResolveError::NoSpace => ResolveError::NoSpace,
            fidl_fuchsia_pkg::ResolveError::UnavailableBlob => ResolveError::UnavailableBlob,
            fidl_fuchsia_pkg::ResolveError::UnavailableRepoMetadata => {
                ResolveError::UnavailableRepoMetadata
            }
            fidl_fuchsia_pkg::ResolveError::InvalidUrl => ResolveError::InvalidUrl,
        }
    }
}

impl From<ResolveError> for fidl_fuchsia_pkg::ResolveError {
    fn from(e: ResolveError) -> Self {
        match e {
            ResolveError::Fidl(_) => fidl_fuchsia_pkg::ResolveError::Io,
            ResolveError::Internal => fidl_fuchsia_pkg::ResolveError::Internal,
            ResolveError::AccessDenied => fidl_fuchsia_pkg::ResolveError::AccessDenied,
            ResolveError::Io => fidl_fuchsia_pkg::ResolveError::Io,
            ResolveError::BlobNotFound => fidl_fuchsia_pkg::ResolveError::BlobNotFound,
            ResolveError::PackageNotFound => fidl_fuchsia_pkg::ResolveError::PackageNotFound,
            ResolveError::RepoNotFound => fidl_fuchsia_pkg::ResolveError::RepoNotFound,
            ResolveError::NoSpace => fidl_fuchsia_pkg::ResolveError::NoSpace,
            ResolveError::UnavailableBlob => fidl_fuchsia_pkg::ResolveError::UnavailableBlob,
            ResolveError::UnavailableRepoMetadata => {
                fidl_fuchsia_pkg::ResolveError::UnavailableRepoMetadata
            }
            ResolveError::InvalidUrl => fidl_fuchsia_pkg::ResolveError::InvalidUrl,
        }
    }
}

#[derive(Error, Debug, PartialEq)]
pub enum BlobIdParseError {
    #[error("cannot contain uppercase hex characters")]
    CannotContainUppercase,

    #[error("invalid length, expected 32 hex bytes, got {0}")]
    InvalidLength(usize),

    #[error("invalid hex")]
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

    #[error("invalid repository url")]
    InvalidRepoUrl(#[source] fuchsia_url::pkg_url::ParseError),

    #[error("invalid update package url")]
    InvalidUpdatePackageUrl(#[source] fuchsia_url::pkg_url::ParseError),

    #[error("invalid root version: {0}")]
    InvalidRootVersion(u32),

    #[error("invalid root threshold: {0}")]
    InvalidRootThreshold(u32),

    #[error("invalid uri")]
    InvalidUri(#[from] http::uri::InvalidUri),

    #[error("invalid config")]
    MirrorConfig(#[from] MirrorConfigError),
}

#[derive(Error, Debug)]
pub enum RepositoryUrlParseError {
    #[error("invalid repository url")]
    InvalidRepoUrl(#[source] fuchsia_url::pkg_url::ParseError),
}

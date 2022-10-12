// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use thiserror::Error;

#[derive(Debug, Error, Clone)]
pub enum ResolveError {
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

    #[error("the `context` provided to resolver is invalid")]
    InvalidContext,
}

impl From<fidl_fuchsia_pkg::ResolveError> for ResolveError {
    fn from(e: fidl_fuchsia_pkg::ResolveError) -> Self {
        use {fidl_fuchsia_pkg::ResolveError as ferror, ResolveError::*};
        match e {
            ferror::Internal => Internal,
            ferror::AccessDenied => AccessDenied,
            ferror::Io => Io,
            ferror::BlobNotFound => BlobNotFound,
            ferror::PackageNotFound => PackageNotFound,
            ferror::RepoNotFound => RepoNotFound,
            ferror::NoSpace => NoSpace,
            ferror::UnavailableBlob => UnavailableBlob,
            ferror::UnavailableRepoMetadata => UnavailableRepoMetadata,
            ferror::InvalidUrl => InvalidUrl,
            ferror::InvalidContext => InvalidContext,
        }
    }
}

impl From<ResolveError> for fidl_fuchsia_pkg::ResolveError {
    fn from(e: ResolveError) -> Self {
        use {fidl_fuchsia_pkg::ResolveError as ferror, ResolveError::*};
        match e {
            Internal => ferror::Internal,
            AccessDenied => ferror::AccessDenied,
            Io => ferror::Io,
            BlobNotFound => ferror::BlobNotFound,
            PackageNotFound => ferror::PackageNotFound,
            RepoNotFound => ferror::RepoNotFound,
            NoSpace => ferror::NoSpace,
            UnavailableBlob => ferror::UnavailableBlob,
            UnavailableRepoMetadata => ferror::UnavailableRepoMetadata,
            InvalidUrl => ferror::InvalidUrl,
            InvalidContext => ferror::InvalidContext,
        }
    }
}

#[derive(Error, Debug, PartialEq)]
pub enum BlobIdParseError {
    #[error("cannot contain uppercase hex characters")]
    CannotContainUppercase,

    #[error("invalid length, expected {} hex bytes, got {0}", crate::types::BLOB_ID_SIZE)]
    InvalidLength(usize),

    #[error("invalid hex")]
    FromHexError(#[from] hex::FromHexError),
}

#[derive(Error, Debug)]
pub enum BlobIdFromSliceError {
    #[error("invalid length, expected {} bytes, got {0}", crate::types::BLOB_ID_SIZE)]
    InvalidLength(usize),
}

#[derive(Error, Debug)]
pub enum ResolutionContextError {
    #[error("bytes was neither empty nor a valid BlobId")]
    InvalidBlobId(#[source] BlobIdFromSliceError),
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
    InvalidRepoUrl(#[source] fuchsia_url::ParseError),

    #[error("invalid update package url")]
    InvalidUpdatePackageUrl(#[source] fuchsia_url::ParseError),

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
    InvalidRepoUrl(#[source] fuchsia_url::ParseError),
}

#[derive(Debug, thiserror::Error)]
pub enum CupMissingField {
    #[error("CupData response field")]
    Response,
    #[error("CupData request field")]
    Request,
    #[error("CupData key_id field")]
    KeyId,
    #[error("CupData nonce field")]
    Nonce,
    #[error("CupData signature field")]
    Signature,
}

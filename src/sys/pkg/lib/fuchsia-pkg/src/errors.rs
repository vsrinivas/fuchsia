// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_merkle::Hash,
    fuchsia_url::errors::{PackagePathSegmentError, ResourcePathError},
    std::io,
    thiserror::Error,
};

#[derive(Debug, Error)]
pub enum CreationManifestError {
    #[error("manifest contains an invalid resource path '{}'.", path)]
    ResourcePath {
        #[source]
        cause: ResourcePathError,
        path: String,
    },

    #[error("attempted to deserialize creation manifest from malformed json")]
    Json(#[from] serde_json::Error),

    #[error("package external content cannot be in 'meta/' directory: {}", path)]
    ExternalContentInMetaDirectory { path: String },

    #[error("package far content must be in 'meta/' directory: {}", path)]
    FarContentNotInMetaDirectory { path: String },

    #[error("entry has no '=': '{}'", entry)]
    EntryHasNoEqualsSign { entry: String },

    #[error("duplicate resource path: '{}'", path)]
    DuplicateResourcePath { path: String },

    #[error("io error")]
    IoError(#[from] io::Error),

    #[error("file directory collision at: {:?}", path)]
    FileDirectoryCollision { path: String },
}

#[derive(Debug, Error, Eq, PartialEq)]
pub enum PackageManifestError {
    #[error("package contains an invalid blob source path '{source_path:?}'. {merkle}")]
    InvalidBlobPath { merkle: Hash, source_path: std::ffi::OsString },
}

#[derive(Debug, Error)]
pub enum MetaContentsError {
    #[error("invalid resource path: '{:?}'", path)]
    InvalidResourcePath {
        #[source]
        cause: ResourcePathError,
        path: String,
    },

    #[error("package external content cannot be in 'meta/' directory: '{:?}'", path)]
    ExternalContentInMetaDirectory { path: String },

    #[error("entry has no '=': '{:?}'", entry)]
    EntryHasNoEqualsSign { entry: String },

    #[error("duplicate resource path: '{:?}'", path)]
    DuplicateResourcePath { path: String },

    #[error("io error")]
    IoError(#[from] io::Error),

    #[error("invalid hash")]
    ParseHash(#[from] fuchsia_hash::ParseHashError),

    #[error("collision between a file and a directory at path: '{:?}'", path)]
    FileDirectoryCollision { path: String },
}

#[derive(Debug, Error)]
pub enum MetaPackageError {
    #[error("invalid package name")]
    PackageName(#[source] PackagePathSegmentError),

    #[error("invalid package variant")]
    PackageVariant(#[source] PackagePathSegmentError),

    #[error("attempted to deserialize meta/package from malformed json: {}", _0)]
    Json(#[from] serde_json::Error),

    #[error("meta/package file not found")]
    MetaPackageMissing,
}

#[derive(Debug, Error)]
pub enum BuildError {
    #[error("io: {}", _0)]
    IoError(#[from] io::Error),

    #[error("{}: '{}'", cause, path)]
    IoErrorWithPath { cause: io::Error, path: String },

    #[error("meta contents")]
    MetaContents(#[from] MetaContentsError),

    #[error("meta package")]
    MetaPackage(#[from] MetaPackageError),

    #[error("package name")]
    PackageName(#[source] PackagePathSegmentError),

    #[error("package manifest")]
    PackageManifest(#[from] PackageManifestError),

    #[error(
        "the creation manifest contained a resource path that conflicts with a generated resource path: '{}'",
        conflicting_resource_path
    )]
    ConflictingResource { conflicting_resource_path: String },

    #[error("archive write")]
    ArchiveWrite(#[from] fuchsia_archive::Error),
}

impl From<(io::Error, String)> for BuildError {
    fn from(pair: (io::Error, String)) -> Self {
        Self::IoErrorWithPath { cause: pair.0, path: pair.1 }
    }
}

#[derive(Debug, Error, Eq, PartialEq)]
pub enum ParsePackagePathError {
    #[error("package path has more than two segments")]
    TooManySegments,

    #[error("package path has fewer than two segments")]
    TooFewSegments,

    #[error("invalid package name")]
    PackageName(#[source] PackagePathSegmentError),

    #[error("invalid package variant")]
    PackageVariant(#[source] PackagePathSegmentError),
}

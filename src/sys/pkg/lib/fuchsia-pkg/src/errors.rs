// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::io;
use thiserror::Error;

#[derive(Clone, Debug, PartialEq, Eq, Error)]
pub enum ResourcePathError {
    #[error("object names must be at least 1 byte")]
    NameEmpty,

    #[error("object names must be at most 255 bytes")]
    NameTooLong,

    #[error("object names cannot contain the NULL byte")]
    NameContainsNull,

    #[error("object names cannot be '.'")]
    NameIsDot,

    #[error("object names cannot be '..'")]
    NameIsDotDot,

    #[error("object paths cannot start with '/'")]
    PathStartsWithSlash,

    #[error("object paths cannot end with '/'")]
    PathEndsWithSlash,

    #[error("object paths must be at least 1 byte")]
    PathIsEmpty,

    // TODO(fxbug.dev/22531) allow newline once meta/contents supports it in blob paths
    #[error(r"object names cannot contain the newline character '\n'")]
    NameContainsNewline,
}

#[derive(Debug, Eq, Error, PartialEq)]
pub enum PackageNameError {
    #[error("package names cannot be empty")]
    Empty,

    #[error("package names must be at most 100 latin-1 characters, '{}'", invalid_name)]
    TooLong { invalid_name: String },

    #[error(
        "package names must consist of only digits (0 to 9), lower-case letters (a to z), hyphen (-), and period (.), '{}'",
        invalid_name
    )]
    InvalidCharacter { invalid_name: String },
}

#[derive(Debug, Eq, Error, PartialEq)]
pub enum PackageVariantError {
    #[error("package variants cannot be empty")]
    Empty,

    #[error("package variants must be at most 100 latin-1 characters, '{}'", invalid_variant)]
    TooLong { invalid_variant: String },

    #[error(
        "package variants must consist of only digits (0 to 9), lower-case letters (a to z), hyphen (-), and period (.), '{}'",
        invalid_variant
    )]
    InvalidCharacter { invalid_variant: String },
}

#[derive(Debug, Error)]
pub enum CreationManifestError {
    #[error("manifest contains an invalid resource path '{}'. {}", path, cause)]
    ResourcePath {
        #[source]
        cause: ResourcePathError,
        path: String,
    },

    #[error("attempted to deserialize creation manifest from malformed json: {}", _0)]
    Json(#[from] serde_json::Error),

    #[error("package external content cannot be in 'meta/' directory: {}", path)]
    ExternalContentInMetaDirectory { path: String },

    #[error("package far content must be in 'meta/' directory: {}", path)]
    FarContentNotInMetaDirectory { path: String },
}

#[derive(Debug, Error)]
pub enum MetaContentsError {
    #[error("invalid resource path '{}'", path)]
    ResourcePath {
        #[source]
        cause: ResourcePathError,
        path: String,
    },

    #[error("package external content cannot be in 'meta/' directory: '{}'", path)]
    ExternalContentInMetaDirectory { path: String },

    #[error("entry has no '=': '{}'", entry)]
    EntryHasNoEqualsSign { entry: String },

    #[error("duplicate resource path: '{}'", path)]
    DuplicateResourcePath { path: String },

    #[error("io error: '{}'", _0)]
    IoError(#[from] io::Error),

    #[error("invalid hash: '{}'", _0)]
    ParseHash(#[from] fuchsia_hash::ParseHashError),
}

#[derive(Debug, Error)]
pub enum MetaPackageError {
    #[error("invalid package name '{}'", _0)]
    PackageName(#[from] PackageNameError),

    #[error("invalid package variant '{}'", _0)]
    PackageVariant(#[from] PackageVariantError),

    #[error("attempted to deserialize meta/package from malformed json: {}", _0)]
    Json(#[from] serde_json::Error),
}

#[derive(Debug, Error)]
pub enum BuildError {
    #[error("io")]
    IoError(#[from] io::Error),

    #[error("meta contents")]
    MetaContents(#[from] MetaContentsError),

    #[error("meta package")]
    MetaPackage(#[from] MetaPackageError),

    #[error(
        "the creation manifest contained a resource path that conflicts with a generated resource path: '{}'",
        conflicting_resource_path
    )]
    ConflictingResource { conflicting_resource_path: String },

    #[error("archive write")]
    ArchiveWrite(#[from] fuchsia_archive::Error),
}

#[derive(Debug, Error, Eq, PartialEq)]
pub enum ParsePackagePathError {
    #[error("package path has more than two segments")]
    TooManySegments,

    #[error("package path has fewer than two segments")]
    TooFewSegments,

    #[error("invalid package path: {0}")]
    PackagePath(#[from] PackagePathError),
}

#[derive(Debug, Error, Eq, PartialEq)]
pub enum PackagePathError {
    #[error("invalid package name: {0}")]
    PackageName(#[from] PackageNameError),

    #[error("invalid package variant: {0}")]
    PackageVariant(#[from] PackageVariantError),
}

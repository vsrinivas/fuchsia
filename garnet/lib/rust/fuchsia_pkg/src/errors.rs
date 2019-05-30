// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::{self, Fail};
use std::io;

#[derive(Clone, Debug, PartialEq, Eq, Fail)]
pub enum ResourcePathError {
    #[fail(display = "object names must be at least 1 byte")]
    NameEmpty,

    #[fail(display = "object names must be at most 255 bytes")]
    NameTooLong,

    #[fail(display = "object names cannot contain the NULL byte")]
    NameContainsNull,

    #[fail(display = "object names cannot be '.'")]
    NameIsDot,

    #[fail(display = "object names cannot be '..'")]
    NameIsDotDot,

    #[fail(display = "object paths cannot start with '/'")]
    PathStartsWithSlash,

    #[fail(display = "object paths cannot end with '/'")]
    PathEndsWithSlash,

    #[fail(display = "object paths must be at least 1 byte")]
    PathIsEmpty,

    // TODO(PKG-597) allow newline once meta/contents supports it in blob paths
    #[fail(display = r"object names cannot contain the newline character '\n'")]
    NameContainsNewline,
}

#[derive(Debug, Eq, Fail, PartialEq)]
pub enum PackageNameError {
    #[fail(display = "package names cannot be empty")]
    Empty,

    #[fail(display = "package names must be at most 100 latin-1 characters, '{}'", invalid_name)]
    TooLong { invalid_name: String },

    #[fail(
        display = "package names must consist of only digits (0 to 9), lower-case letters (a to z), hyphen (-), and period (.), '{}'",
        invalid_name
    )]
    InvalidCharacter { invalid_name: String },
}

#[derive(Debug, Eq, Fail, PartialEq)]
pub enum PackageVariantError {
    #[fail(display = "package variants cannot be empty")]
    Empty,

    #[fail(
        display = "package variants must be at most 100 latin-1 characters, '{}'",
        invalid_variant
    )]
    TooLong { invalid_variant: String },

    #[fail(
        display = "package variants must consist of only digits (0 to 9), lower-case letters (a to z), hyphen (-), and period (.), '{}'",
        invalid_variant
    )]
    InvalidCharacter { invalid_variant: String },
}

#[derive(Debug, Fail)]
pub enum CreationManifestError {
    #[fail(display = "manifest contains an invalid resource path '{}'. {}", path, cause)]
    ResourcePath {
        #[cause]
        cause: ResourcePathError,
        path: String,
    },

    #[fail(display = "attempted to deserialize creation manifest from malformed json: {}", _0)]
    Json(#[cause] serde_json::Error),

    #[fail(display = "package external content cannot be in 'meta/' directory: {}", path)]
    ExternalContentInMetaDirectory { path: String },

    #[fail(display = "package far content must be in 'meta/' directory: {}", path)]
    FarContentNotInMetaDirectory { path: String },
}

impl From<serde_json::Error> for CreationManifestError {
    fn from(err: serde_json::Error) -> Self {
        CreationManifestError::Json(err)
    }
}

#[derive(Debug, Fail)]
pub enum MetaContentsError {
    #[fail(display = "invalid resource path '{}'", path)]
    ResourcePath {
        #[cause]
        cause: ResourcePathError,
        path: String,
    },

    #[fail(display = "package external content cannot be in 'meta/' directory: '{}'", path)]
    ExternalContentInMetaDirectory { path: String },

    #[fail(display = "entry has no '=': '{}'", entry)]
    EntryHasNoEqualsSign { entry: String },

    #[fail(display = "duplicate resource path: '{}'", path)]
    DuplicateResourcePath { path: String },

    #[fail(display = "io error: '{}'", _0)]
    IoError(#[cause] io::Error),

    #[fail(display = "invalid hash: '{}'", _0)]
    ParseHash(#[cause] fuchsia_merkle::ParseHashError),
}

impl From<io::Error> for MetaContentsError {
    fn from(err: io::Error) -> Self {
        MetaContentsError::IoError(err)
    }
}

impl From<fuchsia_merkle::ParseHashError> for MetaContentsError {
    fn from(err: fuchsia_merkle::ParseHashError) -> Self {
        MetaContentsError::ParseHash(err)
    }
}

#[derive(Debug, Fail)]
pub enum MetaPackageError {
    #[fail(display = "invalid package name '{}'", _0)]
    PackageName(#[cause] PackageNameError),

    #[fail(display = "invalid package variant '{}'", _0)]
    PackageVariant(#[cause] PackageVariantError),

    #[fail(display = "attempted to deserialize meta/package from malformed json: {}", _0)]
    Json(#[cause] serde_json::Error),
}

impl From<PackageNameError> for MetaPackageError {
    fn from(err: PackageNameError) -> Self {
        MetaPackageError::PackageName(err)
    }
}

impl From<PackageVariantError> for MetaPackageError {
    fn from(err: PackageVariantError) -> Self {
        MetaPackageError::PackageVariant(err)
    }
}

impl From<serde_json::Error> for MetaPackageError {
    fn from(err: serde_json::Error) -> Self {
        MetaPackageError::Json(err)
    }
}

#[derive(Debug, Fail)]
pub enum BuildError {
    #[fail(display = "io error: '{}'", _0)]
    IoError(#[cause] io::Error),

    #[fail(display = "meta contents error: '{}'", _0)]
    MetaContents(#[cause] MetaContentsError),

    #[fail(display = "meta package error: '{}'", _0)]
    MetaPackage(#[cause] MetaPackageError),

    #[fail(
        display = "the creation manifest contained a resource path that conflicts with a generated resource path: '{}'",
        conflicting_resource_path
    )]
    ConflictingResource { conflicting_resource_path: String },

    #[fail(display = "fuchsia_archive::write error: {}", _0)]
    ArchiveWrite(#[cause] failure::Error),
}

impl From<io::Error> for BuildError {
    fn from(err: io::Error) -> Self {
        BuildError::IoError(err)
    }
}

impl From<MetaContentsError> for BuildError {
    fn from(err: MetaContentsError) -> Self {
        BuildError::MetaContents(err)
    }
}

impl From<MetaPackageError> for BuildError {
    fn from(err: MetaPackageError) -> Self {
        BuildError::MetaPackage(err)
    }
}

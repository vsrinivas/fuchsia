// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_hash::ParseHashError,
    fuchsia_pkg::{PackagePathSegmentError, ParsePackagePathError},
    std::{io, str::Utf8Error},
    thiserror::Error,
};

#[derive(Debug, Error)]
pub enum PathHashMappingError {
    #[error("entry has no '=': '{entry:?}'")]
    EntryHasNoEqualsSign { entry: String },

    #[error("io error")]
    IoError(#[from] io::Error),

    #[error("invalid hash")]
    ParseHash(#[from] ParseHashError),

    #[error("invalid package path")]
    ParsePackagePath(#[from] ParsePackagePathError),
}

#[derive(Debug, Error)]
pub enum AllowListError {
    #[error("encoding error")]
    Encoding(#[from] Utf8Error),

    #[error("invalid package name")]
    PackageName(#[from] PackagePathSegmentError),
}

#[derive(Debug, Error)]
pub enum CachePackagesInitError {
    #[error("while reading data/cache_packages.json file")]
    ReadCachePackagesJson(#[source] package_directory::ReadFileError),

    #[error("while reading data/cache_packages file")]
    ReadCachePackages(#[source] package_directory::ReadFileError),

    #[error("while processing data/cache_packages")]
    ProcessingCachePackages(#[from] PathHashMappingError),

    #[error("while parsing data/cache_packages")]
    ParseConfig(#[from] fuchsia_url::errors::ParseError),

    #[error("json parsing error while reading packages config")]
    JsonError(#[source] serde_json::error::Error),

    #[error("packages config version not supported: '{0:?}'")]
    VersionNotSupported(String),
}

#[derive(Debug, Error)]
pub enum StaticPackagesInitError {
    #[error("while reading data/static_packages file")]
    ReadStaticPackages(#[source] package_directory::ReadFileError),

    #[error("while processing data/static_packages")]
    ProcessingStaticPackages(#[source] PathHashMappingError),
}

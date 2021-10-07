// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::parse::{MAX_PACKAGE_PATH_SEGMENT_BYTES, MAX_RESOURCE_PATH_SEGMENT_BYTES},
    thiserror::Error,
};

#[derive(PartialEq, Debug, Error)]
pub enum ParseError {
    #[error("invalid scheme")]
    InvalidScheme,

    #[error("invalid host")]
    InvalidHost,

    #[error("empty host")]
    EmptyHost,

    #[error("missing host")]
    MissingHost,

    #[error("host must be empty to imply absolute path")]
    HostMustBeEmpty,

    #[error("invalid path segment")]
    InvalidPathSegment(#[source] PackagePathSegmentError),

    #[error("invalid name")]
    InvalidName(#[source] PackagePathSegmentError),

    #[error("missing name")]
    MissingName,

    #[error("invalid variant")]
    InvalidVariant(#[source] PackagePathSegmentError),

    #[error("invalid hash")]
    InvalidHash(#[source] fuchsia_hash::ParseHashError),

    #[error("uppercase hex characters in hash")]
    UpperCaseHash,

    #[error("multiple hash query parameters")]
    MultipleHashes,

    #[error("resource path failed to percent decode")]
    ResourcePathPercentDecode(#[source] std::str::Utf8Error),

    #[error("invalid resource path")]
    InvalidResourcePath(#[source] ResourcePathError),

    #[error("extra path segments")]
    ExtraPathSegments,

    #[error("extra query parameters")]
    ExtraQueryParameters,

    #[error("cannot contain port")]
    CannotContainPort,

    #[error("cannot contain username")]
    CannotContainUsername,

    #[error("cannot contain password")]
    CannotContainPassword,

    #[error("cannot contain query parameters")]
    CannotContainQueryParameters,

    #[error("invalid repository URI")]
    InvalidRepository,

    #[error("url parse error")]
    UrlParseError(#[from] url::ParseError),
}

#[derive(PartialEq, Eq, Debug, Error)]
pub enum PackagePathSegmentError {
    #[error("empty segment")]
    Empty,

    #[error(
        "segment too long. should be at most {} bytes, was {0} bytes",
        MAX_PACKAGE_PATH_SEGMENT_BYTES
    )]
    TooLong(usize),

    #[error("package path segments must consist of only digits (0 to 9), lower-case letters (a to z), hyphen (-), underscore (_), and period (.). this contained {character:?}")]
    InvalidCharacter { character: char },
}

#[derive(Clone, Debug, PartialEq, Eq, Error)]
pub enum ResourcePathError {
    #[error("object names must be at least 1 byte")]
    NameEmpty,

    #[error("object names must be at most {} bytes", MAX_RESOURCE_PATH_SEGMENT_BYTES)]
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

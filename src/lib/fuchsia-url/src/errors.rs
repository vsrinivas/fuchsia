// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use thiserror::Error;

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

    #[error("invalid path")]
    InvalidPath(#[source] ValidateNameError),

    #[error("invalid name")]
    InvalidName(#[source] ValidateNameError),

    #[error("missing name")]
    MissingName,

    #[error("invalid variant")]
    InvalidVariant(#[source] ValidateNameError),

    #[error("invalid hash")]
    InvalidHash(#[source] fuchsia_hash::ParseHashError),

    #[error("uppercase hex characters in hash")]
    UpperCaseHash,

    #[error("multiple hash query parameters")]
    MultipleHashes,

    #[error("invalid resource path")]
    InvalidResourcePath,

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

#[derive(PartialEq, Debug, Error)]
pub enum ValidateNameError {
    #[error("empty name")]
    EmptyName,

    #[error("name longer than 255 bytes")]
    NameTooLong,

    #[error("invalid character {character:?}")]
    InvalidCharacter { character: char },
}

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use thiserror::Error;

#[derive(Clone, Debug, PartialEq, Eq, Error)]
pub enum ParseError {
    #[error("invalid scheme")]
    InvalidScheme,

    #[error("invalid host")]
    InvalidHost,

    #[error("host must be empty to imply absolute path")]
    HostMustBeEmpty,

    #[error("invalid path")]
    InvalidPath,

    #[error("invalid name")]
    InvalidName,

    #[error("invalid variant")]
    InvalidVariant,

    #[error("invalid hash")]
    InvalidHash,

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

    #[error("parse error: {}", _0)]
    UrlParseError(#[from] url::ParseError),
}

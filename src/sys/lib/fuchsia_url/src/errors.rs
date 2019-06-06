// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::Fail;

#[derive(Clone, Debug, PartialEq, Eq, Fail)]
pub enum ParseError {
    #[fail(display = "invalid scheme")]
    InvalidScheme,

    #[fail(display = "invalid host")]
    InvalidHost,

    #[fail(display = "host must be empty to imply absolute path")]
    HostMustBeEmpty,

    #[fail(display = "invalid path")]
    InvalidPath,

    #[fail(display = "invalid name")]
    InvalidName,

    #[fail(display = "invalid variant")]
    InvalidVariant,

    #[fail(display = "invalid hash")]
    InvalidHash,

    #[fail(display = "invalid resource path")]
    InvalidResourcePath,

    #[fail(display = "extra path segments")]
    ExtraPathSegments,

    #[fail(display = "extra query parameters")]
    ExtraQueryParameters,

    #[fail(display = "cannot contain port")]
    CannotContainPort,

    #[fail(display = "cannot contain username")]
    CannotContainUsername,

    #[fail(display = "cannot contain password")]
    CannotContainPassword,

    #[fail(display = "cannot contain query parameters")]
    CannotContainQueryParameters,

    #[fail(display = "invalid repository URI")]
    InvalidRepository,

    #[fail(display = "parse error: {}", _0)]
    UrlParseError(#[cause] url::ParseError),
}

impl From<url::ParseError> for ParseError {
    fn from(err: url::ParseError) -> Self {
        ParseError::UrlParseError(err)
    }
}

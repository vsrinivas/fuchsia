// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::error;
use std::fmt;

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum ParseError {
    InvalidScheme,
    InvalidHost,
    HostMustBeEmpty,
    InvalidPath,
    InvalidName,
    InvalidVariant,
    InvalidHash,
    InvalidResourcePath,
    ExtraPathSegments,
    CannotContainPort,
    CannotContainUsername,
    CannotContainPassword,
    CannotContainQueryParameters,
    UrlParseError(url::ParseError),
}

impl fmt::Display for ParseError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match *self {
            ParseError::InvalidScheme => write!(f, "invalid scheme"),
            ParseError::InvalidHost => write!(f, "invalid host"),
            ParseError::HostMustBeEmpty => write!(f, "host must be empty to imply absolute path"),
            ParseError::InvalidPath => write!(f, "invalid path"),
            ParseError::InvalidName => write!(f, "invalid name"),
            ParseError::InvalidVariant => write!(f, "invalid variant"),
            ParseError::InvalidHash => write!(f, "invalid hash"),
            ParseError::InvalidResourcePath => write!(f, "invalid resource path"),
            ParseError::ExtraPathSegments => write!(f, "extra path segments"),
            ParseError::CannotContainPort => write!(f, "cannot contain port"),
            ParseError::CannotContainUsername => write!(f, "cannot contain username"),
            ParseError::CannotContainPassword => write!(f, "cannot contain password"),
            ParseError::CannotContainQueryParameters => {
                write!(f, "cannot contain query parameters")
            }
            ParseError::UrlParseError(err) => err.fmt(f),
        }
    }
}

impl error::Error for ParseError {
    fn source(&self) -> Option<&(dyn error::Error + 'static)> {
        match *self {
            ParseError::InvalidScheme => None,
            ParseError::InvalidHost => None,
            ParseError::HostMustBeEmpty => None,
            ParseError::InvalidPath => None,
            ParseError::InvalidName => None,
            ParseError::InvalidVariant => None,
            ParseError::InvalidHash => None,
            ParseError::InvalidResourcePath => None,
            ParseError::ExtraPathSegments => None,
            ParseError::CannotContainPort => None,
            ParseError::CannotContainUsername => None,
            ParseError::CannotContainPassword => None,
            ParseError::CannotContainQueryParameters => None,
            ParseError::UrlParseError(ref err) => err.source(),
        }
    }
}

impl From<url::ParseError> for ParseError {
    fn from(err: url::ParseError) -> Self {
        ParseError::UrlParseError(err)
    }
}

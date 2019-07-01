// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::Fail;
use fidl_fuchsia_pkg_ext::BlobIdParseError;
use serde_json;

#[derive(Debug, Fail)]
pub enum Error {
    #[fail(display = "{}", _0)]
    Help(&'static str),

    #[fail(display = "missing argument {}", _0)]
    MissingArgument(&'static str),

    #[fail(display = "unrecognized argument {}", _0)]
    UnrecognizedArgument(String),

    #[fail(display = "missing subcommand")]
    MissingCommand,

    #[fail(display = "{}", _0)]
    Json(#[cause] serde_json::Error),

    #[fail(display = "{}", _0)]
    BlobId(#[cause] BlobIdParseError),
}

impl From<serde_json::Error> for Error {
    fn from(err: serde_json::Error) -> Error {
        Error::Json(err)
    }
}

impl From<BlobIdParseError> for Error {
    fn from(err: BlobIdParseError) -> Error {
        Error::BlobId(err)
    }
}

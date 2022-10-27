// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_pkg_ext::BlobIdParseError;
use thiserror::Error;

#[derive(Debug, Error)]
pub enum Error {
    #[error("{}", _0)]
    Json(serde_json::Error),

    #[error("{}", _0)]
    BlobId(BlobIdParseError),

    #[error("invalid experiment id {}", _0)]
    ExperimentId(String),
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

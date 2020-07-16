// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon as zx;

#[derive(Debug, thiserror::Error)]
pub(crate) enum Error {
    #[error(transparent)]
    Zx(#[from] zx::Status),
    #[error(transparent)]
    Generic(#[from] anyhow::Error),
}

impl From<Error> for zx::Status {
    fn from(error: Error) -> zx::Status {
        match error {
            Error::Zx(status) => status,
            Error::Generic(_) => zx::Status::INTERNAL,
        }
    }
}

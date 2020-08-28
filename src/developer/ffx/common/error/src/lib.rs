// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Error type for wrapping errors known to an `ffx` command and whose occurrence should
// not a priori be considered a bug in ffx.
// TODO(57592): consider extending this to allow custom types from plugins.
#[derive(thiserror::Error, Debug)]
pub enum FfxError {
    #[error(transparent)]
    Error(#[from] anyhow::Error),
}

// Utility macro for constructing a FfxError::Error with a simple error string.
#[macro_export]
macro_rules! printable_error {
    ($error_message: expr) => {{
        use ::ffx_error::FfxError;
        FfxError::Error(anyhow::anyhow!($error_message))
    }};
}

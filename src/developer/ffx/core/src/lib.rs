// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub use core_macros::{ffx_command, ffx_plugin};

// Error type for wrapping errors known to an `ffx` command and whose occurrence should
// not a priori be considered a bug in ffx.
// TODO(fxbug.dev/57592): consider extending this to allow custom types from plugins.
#[derive(thiserror::Error, Debug)]
pub enum FfxError {
    #[error(transparent)]
    Error(#[from] anyhow::Error),
}

// Utility macro for constructing a FfxError::Error with a simple error string.
#[macro_export]
macro_rules! ffx_error {
    ($error_message: expr) => {{
        $crate::FfxError::Error(anyhow::anyhow!($error_message))
    }};
    ($fmt:expr, $($arg:tt)*) => {
        $crate::ffx_error!(format!($fmt, $($arg)*));
    };
}

#[macro_export]
macro_rules! ffx_bail {
    ($msg:literal $(,)?) => {
        anyhow::bail!($crate::ffx_error!($msg))
    };
    ($fmt:expr, $($arg:tt)*) => {
        anyhow::bail!($crate::ffx_error!($fmt, $($arg)*));
    };
}

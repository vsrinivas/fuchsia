// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use async_utils::hanging_get::error::HangingGetServerError;
use fidl_fuchsia_bluetooth_power::{Identifier, Information};
use fuchsia_zircon as zx;
use thiserror::Error;

/// Errors that occur during the operation of the component.
#[derive(Error, Debug)]
pub enum Error {
    #[error("Invalid FIDL Power Information: {:?}", .info)]
    Info { info: Information },
    #[error("Invalid Identifier: {:?}", .identifier)]
    Identifier { identifier: Identifier },
    #[error("Invalid Battery Information: {}", .message)]
    BatteryInfo { message: String },
    #[error("Error managing a hanging get request for a client: {}", .0)]
    HangingGet(#[from] HangingGetServerError),
    #[error("Internal error: {}", .message)]
    Internal { message: String },
    #[error("Fidl Error: {}", .0)]
    Fidl(#[from] fidl::Error),
}

impl Error {
    /// An internal error occurred in the component.
    ///
    /// This allocates memory which could fail if the error is an OOM.
    pub fn internal(message: impl Into<String>) -> Self {
        Self::Internal { message: message.into() }
    }

    pub fn battery(message: impl Into<String>) -> Self {
        Self::BatteryInfo { message: message.into() }
    }
}

impl From<&Information> for Error {
    fn from(src: &Information) -> Error {
        Error::Info { info: src.clone() }
    }
}

impl From<&Identifier> for Error {
    fn from(src: &Identifier) -> Error {
        Error::Identifier { identifier: src.clone() }
    }
}

impl From<&Error> for zx::Status {
    fn from(src: &Error) -> zx::Status {
        match src {
            Error::Info { .. } | Error::BatteryInfo { .. } | Error::Identifier { .. } => {
                zx::Status::INVALID_ARGS
            }
            _ => zx::Status::INTERNAL,
        }
    }
}

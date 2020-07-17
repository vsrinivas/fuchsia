// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::format_err, fidl_fuchsia_bluetooth as bt, fidl_fuchsia_bluetooth_sys as sys,
    fuchsia_bluetooth::bt_fidl_status, thiserror::Error,
};

/// Type representing Possible errors raised in the operation of BT-GAP
#[derive(Debug, Error)]
pub enum Error {
    /// Internal bt-gap Error
    #[error("Internal bt-gap Error: {0}")]
    InternalError(anyhow::Error),

    /// fuchsia.bluetooth.sys API errors. Used to encapsulate errors that are reported by bt-host
    /// and for the fuchsia.bluetooth.sys.Access API.
    #[error("fuchsia.bluetooth.sys Error: {0:?}")]
    SysError(sys::Error),
}

pub type Result<T> = std::result::Result<T, Error>;

impl Error {
    pub fn as_status(self) -> bt::Status {
        match self {
            Error::InternalError(err) => bt_fidl_status!(Failed, format!("{}", err)),
            Error::SysError(err) => {
                bt::Status { error: Some(Box::new(sys_error_to_deprecated(err))) }
            }
        }
    }

    pub fn no_host() -> Error {
        Error::SysError(sys::Error::Failed)
    }

    pub fn as_failure(self) -> anyhow::Error {
        match self {
            Error::InternalError(err) => err,
            Error::SysError(err) => format_err!("Host Error: {:?}", err),
        }
    }
}

impl From<sys::Error> for Error {
    fn from(err: sys::Error) -> Error {
        Error::SysError(err)
    }
}

impl Into<sys::Error> for Error {
    fn into(self) -> sys::Error {
        match self {
            Error::SysError(err) => err,
            Error::InternalError(_) => sys::Error::Failed,
        }
    }
}

impl From<anyhow::Error> for Error {
    fn from(err: anyhow::Error) -> Error {
        Error::InternalError(err)
    }
}

impl From<fidl::Error> for Error {
    fn from(err: fidl::Error) -> Error {
        Error::InternalError(format_err!(format!("Internal FIDL error: {}", err)))
    }
}

pub fn from_fidl_result<T>(r: fidl::Result<std::result::Result<T, sys::Error>>) -> Result<T> {
    match r {
        Ok(r) => r.map_err(Error::from),
        Err(e) => Err(Error::from(e)),
    }
}

// Maps a fuchsia.bluetooth.sys.Error value to a fuchsia.bluetooth.Error. This is maintained for
// compatibility until fuchsia.bluetooth.control and fuchsia.bluetooth.Status are removed.
fn sys_error_to_deprecated(e: sys::Error) -> bt::Error {
    bt::Error {
        error_code: match e {
            sys::Error::Failed => bt::ErrorCode::Failed,
            sys::Error::PeerNotFound => bt::ErrorCode::NotFound,
            sys::Error::TimedOut => bt::ErrorCode::TimedOut,
            sys::Error::Canceled => bt::ErrorCode::Canceled,
            sys::Error::InProgress => bt::ErrorCode::InProgress,
            sys::Error::NotSupported => bt::ErrorCode::NotSupported,
            sys::Error::InvalidArguments => bt::ErrorCode::InvalidArguments,
        },
        protocol_error_code: 0,
        description: None,
    }
}

pub fn status_response(result: Result<()>) -> bt::Status {
    match result {
        Ok(()) => bt_fidl_status!(),
        Err(err) => err.as_status(),
    }
}

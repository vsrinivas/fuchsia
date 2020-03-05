// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::format_err, fidl_fuchsia_bluetooth as bt, fidl_fuchsia_bluetooth_sys as sys,
    fuchsia_bluetooth::bt_fidl_status,
};

/// Type representing Possible errors raised in the operation of BT-GAP
#[derive(Debug)]
pub enum Error {
    /// Internal bt-gap Error
    InternalError(anyhow::Error),

    /// Host Error
    HostError(bt::Error),

    /// fuchsia.bluetooth.sys API errors. Used to encapsulate errors that are reported by bt-host
    /// and for the fuchsia.bluetooth.sys.Access API.
    SysError(sys::Error),
}

pub type Result<T> = std::result::Result<T, Error>;

impl Error {
    pub fn as_status(self) -> bt::Status {
        match self {
            Error::HostError(err) => bt::Status { error: Some(Box::new(err)) },
            Error::InternalError(err) => bt_fidl_status!(Failed, format!("{}", err)),
            Error::SysError(err) => {
                bt::Status { error: Some(Box::new(sys_error_to_deprecated(err))) }
            }
        }
    }

    pub fn no_host() -> Error {
        Error::HostError(bt::Error {
            error_code: bt::ErrorCode::BluetoothNotAvailable,
            protocol_error_code: 0,
            description: Some("No Host found".to_string()),
        })
    }

    pub fn as_failure(self) -> anyhow::Error {
        match self {
            Error::InternalError(err) => err,
            Error::HostError(err) => format_err!(
                "Host Error: {}",
                err.description.unwrap_or("Unknown Host Error".to_string())
            ),
            Error::SysError(err) => format_err!("Host Error: {:?}", err),
        }
    }
}

impl Into<anyhow::Error> for Error {
    fn into(self) -> anyhow::Error {
        self.as_failure()
    }
}

impl From<bt::Error> for Error {
    fn from(err: bt::Error) -> Error {
        Error::HostError(err)
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
            Error::HostError(err) => deprecated_error_to_sys(err),
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

pub fn from_fidl_status(r: fidl::Result<bt::Status>) -> Result<()> {
    match r {
        Ok(status) => status.as_result(),
        Err(e) => Err(Error::from(e)),
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

// Maps a fuchsia.bluetooth.Error value to a fuchsia.bluetooth.sys.Error. This is maintained for
// compatibility until fuchsia.bluetooth.control and fuchsia.bluetooth.Status are removed.
fn deprecated_error_to_sys(e: bt::Error) -> sys::Error {
    match e.error_code {
        bt::ErrorCode::Failed => sys::Error::Failed,
        bt::ErrorCode::NotFound => sys::Error::PeerNotFound,
        bt::ErrorCode::TimedOut => sys::Error::TimedOut,
        bt::ErrorCode::Canceled => sys::Error::Canceled,
        bt::ErrorCode::InProgress => sys::Error::InProgress,
        bt::ErrorCode::NotSupported => sys::Error::NotSupported,
        bt::ErrorCode::InvalidArguments => sys::Error::InvalidArguments,
        bt::ErrorCode::BluetoothNotAvailable => sys::Error::Failed,
        bt::ErrorCode::BadState => sys::Error::Failed,
        bt::ErrorCode::Unknown => sys::Error::Failed,
        bt::ErrorCode::Already => sys::Error::InProgress,
        bt::ErrorCode::ProtocolError => sys::Error::Failed,
    }
}

pub trait StatusExt {
    fn as_result(self) -> Result<()>;
}

impl StatusExt for bt::Status {
    fn as_result(self) -> Result<()> {
        match self.error {
            Some(err) => Err((*err).into()),
            None => Ok(()),
        }
    }
}

pub fn status_response(result: Result<()>) -> bt::Status {
    match result {
        Ok(()) => bt_fidl_status!(),
        Err(err) => err.as_status(),
    }
}

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{self, err_msg},
    fidl_fuchsia_bluetooth as bt,
    fuchsia_bluetooth::bt_fidl_status,
};

/// Type representing Possible errors raised in the operation of BT-GAP
#[derive(Debug)]
pub enum Error {
    // Internal bt-gap Error
    InternalError(failure::Error),
    // Host Error
    HostError(bt::Error),
}

pub type Result<T> = std::result::Result<T, Error>;

impl Error {
    pub fn as_status(self) -> bt::Status {
        match self {
            Error::HostError(err) => bt::Status { error: Some(Box::new(err)) },
            Error::InternalError(err) => bt_fidl_status!(Failed, format!("{}", err)),
        }
    }

    pub fn no_host() -> Error {
        Error::HostError(bt::Error {
            error_code: bt::ErrorCode::BluetoothNotAvailable,
            protocol_error_code: 0,
            description: Some("No Host found".to_string()),
        })
    }

    pub fn not_found(desc: &str) -> Error {
        Error::HostError(bt::Error {
            error_code: bt::ErrorCode::NotFound,
            protocol_error_code: 0,
            description: Some(desc.to_string()),
        })
    }

    pub fn as_failure(self) -> failure::Error {
        match self {
            Error::InternalError(err) => err,
            Error::HostError(err) => err_msg(format!(
                "Host Error: {}",
                err.description.unwrap_or("Unknown Host Error".to_string())
            )),
        }
    }
}

impl Into<failure::Error> for Error {
    fn into(self) -> failure::Error {
        self.as_failure()
    }
}

impl From<bt::Error> for Error {
    fn from(err: bt::Error) -> Error {
        Error::HostError(err)
    }
}

impl From<failure::Error> for Error {
    fn from(err: failure::Error) -> Error {
        Error::InternalError(err)
    }
}
impl From<fidl::Error> for Error {
    fn from(err: fidl::Error) -> Error {
        Error::InternalError(err_msg(format!("Internal FIDL error: {}", err)))
    }
}

pub fn from_fidl_status(r: fidl::Result<bt::Status>) -> Result<()> {
    match r {
        Ok(status) => status.as_result(),
        Err(e) => Err(Error::from(e))
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

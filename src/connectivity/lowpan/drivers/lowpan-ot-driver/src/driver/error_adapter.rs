// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::prelude::ot::Error;
use fidl_fuchsia_net_stack_ext::NetstackError;
use fuchsia_syslog::macros::*;
use fuchsia_zircon_status::Status as ZxStatus;
use openthread::ot;
use std::fmt::Debug;

/// Used for wrapping around error types so that they can be
/// converted to [`::fuchsia_zircon_status::Status`] values
/// that are returned by the methods of [`lowpan_driver_common::Driver`].
#[derive(thiserror::Error, Debug)]
pub(super) struct ErrorAdapter<T: Debug>(pub T);

impl<T: Debug> std::fmt::Display for ErrorAdapter<T> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        self.0.fmt(f)
    }
}

impl From<ErrorAdapter<ot::Error>> for ZxStatus {
    fn from(err: ErrorAdapter<ot::Error>) -> ZxStatus {
        Self::from(ErrorAdapter(anyhow::Error::new(err.0)))
    }
}

impl From<ErrorAdapter<anyhow::Error>> for ZxStatus {
    fn from(err: ErrorAdapter<anyhow::Error>) -> ZxStatus {
        if let Some(status) = err.0.downcast_ref::<ZxStatus>() {
            *status
        } else if let Some(err) = err.0.downcast_ref::<ot::Error>() {
            ZxStatus::from(*err)
        } else {
            fx_log_err!("Unhandled error when casting to ZxStatus: {:?}", err);
            ZxStatus::INTERNAL
        }
    }
}

impl From<ErrorAdapter<ot::WrongSize>> for ZxStatus {
    fn from(_: ErrorAdapter<ot::WrongSize>) -> ZxStatus {
        ZxStatus::INVALID_ARGS
    }
}

pub trait ErrorExt {
    fn get_zx_status(&self) -> Option<ZxStatus>;
    fn get_netstack_error(&self) -> Option<fidl_fuchsia_net_stack::Error>;
    fn get_ot_error(&self) -> Option<ot::Error>;
}

impl ErrorExt for anyhow::Error {
    /// If this error is based on a `ZxStatus`, then return it.
    fn get_zx_status(&self) -> Option<ZxStatus> {
        if let Some(status) = self.downcast_ref::<ZxStatus>() {
            Some(*status)
        } else {
            None
        }
    }

    /// If this error is based on a Netstack `Error`, then return it.
    fn get_netstack_error(&self) -> Option<fidl_fuchsia_net_stack::Error> {
        if let Some(err) = self.downcast_ref::<NetstackError>() {
            Some(err.0)
        } else {
            None
        }
    }

    fn get_ot_error(&self) -> Option<ot::Error> {
        if let Some(err) = self.downcast_ref::<ot::Error>() {
            Some(*err)
        } else {
            None
        }
    }
}

pub trait ErrorResultExt {
    type Error;
    fn ignore_already_exists(self) -> Result<(), Self::Error>;
    fn ignore_not_found(self) -> Result<(), Self::Error>;
}

impl ErrorResultExt for Result<(), anyhow::Error> {
    type Error = anyhow::Error;
    fn ignore_already_exists(self) -> Result<(), Self::Error> {
        self.or_else(|err| {
            if err.get_zx_status() == Some(ZxStatus::ALREADY_EXISTS) {
                Ok(())
            } else if err.get_netstack_error() == Some(fidl_fuchsia_net_stack::Error::AlreadyExists)
            {
                Ok(())
            } else if err.get_ot_error() == Some(ot::Error::Already) {
                Ok(())
            } else {
                Err(err)
            }
        })
    }

    fn ignore_not_found(self) -> Result<(), Self::Error> {
        self.or_else(|err| {
            if err.get_zx_status() == Some(ZxStatus::NOT_FOUND) {
                Ok(())
            } else if err.get_netstack_error() == Some(fidl_fuchsia_net_stack::Error::NotFound) {
                Ok(())
            } else if err.get_ot_error() == Some(ot::Error::NotFound) {
                Ok(())
            } else {
                Err(err)
            }
        })
    }
}

impl ErrorResultExt for Result<(), ot::Error> {
    type Error = ot::Error;
    fn ignore_already_exists(self) -> Result<(), Self::Error> {
        self.or_else(|err| match err {
            Error::Already => Ok(()),
            err => Err(err),
        })
    }

    fn ignore_not_found(self) -> Result<(), Self::Error> {
        self.or_else(|err| match err {
            Error::NotFound => Ok(()),
            err => Err(err),
        })
    }
}

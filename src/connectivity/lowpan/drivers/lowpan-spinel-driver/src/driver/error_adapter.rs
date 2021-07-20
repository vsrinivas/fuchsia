// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::spinel::Status;
use fidl_fuchsia_net_stack_ext::NetstackError;
use fuchsia_syslog::macros::*;
use fuchsia_zircon_status::Status as ZxStatus;
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

impl From<ErrorAdapter<anyhow::Error>> for ZxStatus {
    fn from(err: ErrorAdapter<anyhow::Error>) -> ZxStatus {
        if let Some(status) = err.0.downcast_ref::<ZxStatus>() {
            *status
        } else if err.0.is::<crate::spinel::Canceled>() {
            ZxStatus::CANCELED
        } else if let Some(status) = err.0.downcast_ref::<Status>() {
            match status {
                Status::Unimplemented => ZxStatus::NOT_SUPPORTED,
                Status::InvalidArgument => ZxStatus::INVALID_ARGS,
                Status::InvalidState => ZxStatus::BAD_STATE,
                _ => ZxStatus::INTERNAL,
            }
        } else {
            fx_log_err!("Unhandled error when casting to ZxStatus: {:?}", err);
            ZxStatus::INTERNAL
        }
    }
}

pub trait ErrorExt {
    fn get_zx_status(&self) -> Option<ZxStatus>;
    fn get_netstack_error(&self) -> Option<fidl_fuchsia_net_stack::Error>;
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
            } else {
                Err(err)
            }
        })
    }
}

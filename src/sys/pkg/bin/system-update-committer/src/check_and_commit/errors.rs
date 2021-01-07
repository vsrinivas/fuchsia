// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {fidl_fuchsia_paver as paver, fuchsia_zircon::Status, thiserror::Error};

/// Error condition that may be returned by a boot manager client.
#[derive(Error, Debug)]
pub enum BootManagerError {
    #[error("BootManager returned non-ok status while calling {method_name:}")]
    Status {
        method_name: &'static str,
        #[source]
        status: Status,
    },

    #[error("fidl error while calling BootManager method {method_name:}")]
    Fidl {
        method_name: &'static str,
        #[source]
        error: fidl::Error,
    },
}

/// Error condition that may be returned by `determine_slot_to_commit`.
#[derive(Error, Debug)]
pub enum DetermineSlotToCommitError {
    #[error("the current configuration ({_0:?}) is unbootable. This should never happen.")]
    CurrentConfigurationUnbootable(paver::Configuration),
}

/// Error condition that may be returned by `do_health_checks`.
#[derive(Error, Debug)]
// TODO(http://fxbug.dev/64595) use this.
#[allow(dead_code)]
pub enum HealthCheckError {
    #[error("the blobfs check failed")]
    BlobFsCheckFailed,

    #[error("an unexpected error occurred")]
    Other(#[source] anyhow::Error),
}

/// Error condition that may be returned by check_and_commit.
#[derive(Error, Debug)]
pub enum CheckAndCommitError {
    #[error("while calling do_health_checks")]
    HealthCheck(#[source] HealthCheckError),

    #[error("failed to signal EventPair peer")]
    SignalPeer(#[source] Status),

    #[error("failed to signal EventPair handle")]
    SignalHandle(#[source] Status),

    #[error("BootManager returned error")]
    BootManager(#[source] BootManagerError),

    #[error("while calling determine_slot_to_commit")]
    DetermineSlotToCommit(#[source] DetermineSlotToCommitError),
}

/// Helper to convert fidl's nested errors.
pub trait BootManagerResultExt {
    type T;

    fn into_boot_manager_result(
        self,
        method_name: &'static str,
    ) -> Result<Self::T, BootManagerError>;
}

impl BootManagerResultExt for Result<i32, fidl::Error> {
    type T = ();

    fn into_boot_manager_result(
        self: Result<i32, fidl::Error>,
        method_name: &'static str,
    ) -> Result<(), BootManagerError> {
        match self.map(Status::ok) {
            Ok(Ok(())) => Ok(()),
            Ok(Err(status)) => Err(BootManagerError::Status { status, method_name }),
            Err(error) => Err(BootManagerError::Fidl { error, method_name }),
        }
    }
}

impl<T> BootManagerResultExt for Result<Result<T, i32>, fidl::Error> {
    type T = T;

    fn into_boot_manager_result(
        self: Result<Result<Self::T, i32>, fidl::Error>,
        method_name: &'static str,
    ) -> Result<Self::T, BootManagerError> {
        match self {
            Ok(Ok(value)) => Ok(value),
            Ok(Err(raw)) => {
                Err(BootManagerError::Status { status: Status::from_raw(raw), method_name })
            }
            Err(error) => Err(BootManagerError::Fidl { error, method_name }),
        }
    }
}

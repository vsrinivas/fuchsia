// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_paver as paver, fidl_fuchsia_update_verify as verify, fuchsia_zircon::Status,
    thiserror::Error,
};

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

/// Error condition that may be returned by the PolicyEngine.
#[derive(Error, Debug)]
pub enum PolicyError {
    #[error("the policy engine failed to build")]
    Build(#[source] BootManagerError),

    #[error("the current configuration ({_0:?}) is unbootable. This should never happen.")]
    CurrentConfigurationUnbootable(paver::Configuration),
}

#[derive(Debug, PartialEq, Eq, Copy, Clone)]
pub enum VerifySource {
    Blobfs,
}

/// Error condition that may be returned when doing health verification.
#[derive(Error, Debug)]
pub enum VerifyErrors {
    #[error("one or more verifications failed: {_0:?}")]
    VerifyErrors(Vec<VerifyError>),
}

/// Error condition that may be returned when doing health verification.
#[derive(Error, Debug)]
pub enum VerifyError {
    #[error("the {_0:?} verification failed")]
    VerifyError(VerifySource, #[source] VerifyFailureReason),
}

#[derive(Error, Debug)]
pub enum VerifyFailureReason {
    #[error("the fidl call failed")]
    Fidl(#[source] fidl::Error),

    #[error("the verify request timed out")]
    Timeout,

    #[error("the verification failed: {0:?}")]
    Verify(verify::VerifyError),
}

/// Error condition that may be returned by `put_metadata_in_happy_state`.
#[derive(Error, Debug)]
pub enum MetadataError {
    #[error("while doing health verification")]
    Verify(#[source] VerifyErrors),

    #[error("while signalling EventPair peer")]
    SignalPeer(#[source] Status),

    #[error("while sending the unblock")]
    Unblock,

    #[error("while doing commit")]
    Commit(#[source] BootManagerError),

    #[error("while interfacing with policy")]
    Policy(#[source] PolicyError),
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

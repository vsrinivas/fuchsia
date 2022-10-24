// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::hash_tree::HashTreeError;
use crate::lookup_table::LookupTableError;
use crate::pinweaver::PinWeaverProtocolError;
use crate::provision::ProvisionError;
use fidl_fuchsia_identity_credential::CredentialError;
use std::fmt;
use thiserror::Error;

/// Wrapper around FIDL generated CredentialError (NewType Idiom).
#[derive(Error, Debug)]
pub struct CredentialErrorWrapper(pub CredentialError);

/// Convert a `CredentialError` into a `CredentialErrorWrapper`.
impl From<CredentialError> for CredentialErrorWrapper {
    fn from(error: CredentialError) -> Self {
        CredentialErrorWrapper(error)
    }
}

/// Display the wrapped `CredentialError` type for the `CredentialErrorWrapper`
impl fmt::Display for CredentialErrorWrapper {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{:?}", self.0)
    }
}

/// `ServiceError` is the core error type that contains all errors
/// that each subsystem can generate. It makes error propagation simple
/// while retaining rich typing.
///
/// The reason this is preferable to anyhow is because errors in some cases can
/// impact the actions of their caller. In addition to this errors that bubble
/// up to a FIDL API caller have to be converted into FIDL generated
/// `CredentialError` types. So having one location for that transformation
/// decouples that responsibility from subsystems.
#[derive(Error, Debug)]
pub enum ServiceError {
    #[error(transparent)]
    Credential(#[from] CredentialErrorWrapper),
    #[error(transparent)]
    PinWeaver(#[from] PinWeaverProtocolError),
    #[error(transparent)]
    Provision(#[from] ProvisionError),
    #[error(transparent)]
    HashTree(#[from] HashTreeError),
    #[error(transparent)]
    LookupTable(#[from] LookupTableError),
}

/// Convert a `CredentialError` to a `ServiceError`.
///
/// Since CredentialError can not implement `StdError` the NewType idiom has
/// to be used to generate a wrapper type that can implement these traits. This
/// trait makes the conversion between the `CredentialError` and its wrapped
/// NewType implicit.
impl From<CredentialError> for ServiceError {
    fn from(error: CredentialError) -> Self {
        ServiceError::Credential(CredentialErrorWrapper(error))
    }
}

/// Converts a `ServiceError` into a `CredentialError`.
///
/// `ServiceError` provides rich error information and is designed to
/// be output to logs to improve debugability. However the FIDL caller is only
/// concerned with actionable information as to why the called failed. So on
/// FIDL return the rich type is truncated to just expose cases a callee would
/// care about.
impl From<ServiceError> for CredentialError {
    fn from(error: ServiceError) -> Self {
        match error {
            ServiceError::Credential(CredentialErrorWrapper(err)) => err,
            ServiceError::HashTree(HashTreeError::NoLeafNodes) => CredentialError::NoFreeLabel,
            ServiceError::HashTree(HashTreeError::NonLeafLabel) => CredentialError::InvalidLabel,
            ServiceError::HashTree(HashTreeError::UnknownLeafLabel) => {
                CredentialError::InvalidLabel
            }
            ServiceError::PinWeaver(_)
            | ServiceError::Provision(_)
            | ServiceError::HashTree(_)
            | ServiceError::LookupTable(_) => CredentialError::InternalError,
        }
    }
}

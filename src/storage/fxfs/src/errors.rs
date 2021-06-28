// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use thiserror::Error;

#[derive(Eq, Error, Clone, Debug, PartialEq)]
pub enum FxfsError {
    #[error("Already exists")]
    AlreadyExists,
    #[error("Filesystem inconsistency")]
    Inconsistent,
    #[error("Internal error")]
    Internal,
    #[error("Expected directory")]
    NotDir,
    #[error("Expected file")]
    NotFile,
    #[error("Not found")]
    NotFound,
    #[error("Not empty")]
    NotEmpty,
    #[error("Read only filesystem")]
    ReadOnlyFilesystem,
    #[error("No space")]
    NoSpace,
    #[error("Deleted")]
    Deleted,
    #[error("Invalid arguments")]
    InvalidArgs,
    #[error("Too big")]
    TooBig,
    #[error("Invalid version")]
    InvalidVersion,
    #[error("Journal flush error")]
    JournalFlushError,
}

impl FxfsError {
    /// A helper to match against this FxfsError against the root cause of an anyhow::Error.
    ///
    /// The main application of this helper is to allow us to match an anyhow::Error against a
    /// specific case of FxfsError in a boolean expression, such as:
    ///
    /// let result: Result<(), anyhow:Error> = foo();
    /// match result {
    ///   Ok(foo) => Ok(foo),
    ///   Err(e) if &FxfsError::NotFound.matches(e) => { ... }
    ///   Err(e) => Err(e)
    /// }
    pub fn matches(&self, error: &anyhow::Error) -> bool {
        if let Some(root_cause) = error.root_cause().downcast_ref::<FxfsError>() {
            self == root_cause
        } else {
            false
        }
    }
}

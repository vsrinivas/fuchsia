// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Code common to all code generation modules.

use {super::super::definition::Arguments, std::io, thiserror};

/// Errors that occur during code generation, mainly from ill-formed definitions.
#[derive(Debug, thiserror::Error)]
pub enum Error {
    #[error("Unable to write to sink: {0:}")]
    WriteFailure(io::Error),
    #[error("An optional argument must not followed by a required argument: {0:?}")]
    RequiredMustNotFollowOptional(Arguments),
    #[error("A list argument must be the last in an argument sequence: {0:?}")]
    ListMustBeLast(Arguments),
    #[error("A map argument must be the last in an argument sequence: {0:?}")]
    MapMustBeLast(Arguments),
    #[error("Argument names must be unique per command or response: {0:?}")]
    DuplicateArgumentName(Arguments),
}

impl From<io::Error> for Error {
    fn from(io_error: io::Error) -> Self {
        Error::WriteFailure(io_error)
    }
}

/// Result type for code generation functions.
pub type Result = std::result::Result<(), Error>;

// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_input_text::{self as ftext},
    thiserror::Error,
};

#[derive(Debug, Clone, PartialEq, Eq, Error)]
pub enum TextFieldError {
    #[error("Internal error")]
    InternalError,
    #[error("Attempted an operation that is not allowed in the current state")]
    BadState,

    #[error("{}", match expected {
        Some(expected) => format!("Stale or invalid revision ID. Expected: {expected:?}. Found: {found:?}"),
        None => format!("Missing or invalid revision ID: {found:?}"),
    })]
    BadRevisionId { expected: Option<ftext::RevisionId>, found: Option<ftext::RevisionId> },

    #[error("{}", match (expected, found) {
        (None, None) => format!("Missing transaction ID"),
        (Some(expected), _) => format!("Stale or invalid transaction ID. Expected: {expected:?}. Found: {found:?}"),
        (None, Some(found))  => format!("Unexpected revision ID: {found:?}"),
    })]
    BadTransactionId { expected: Option<ftext::TransactionId>, found: Option<ftext::TransactionId> },

    #[error("Attempted to insert content that is not allowed by the text field's settings")]
    InvalidContent,
    #[error("Attempted to select an invalid range")]
    InvalidSelection,
    #[error("Invalid argument")]
    InvalidArgument,
}

impl From<TextFieldError> for ftext::TextFieldError {
    // TODO(fxbug.dev/87882): Revise FIDL API to provide error-specific details
    fn from(e: TextFieldError) -> Self {
        match e {
            TextFieldError::InternalError => ftext::TextFieldError::InternalError,
            TextFieldError::BadState => ftext::TextFieldError::BadState,
            TextFieldError::BadRevisionId { .. } => ftext::TextFieldError::BadRevisionId,
            TextFieldError::BadTransactionId { .. } => ftext::TextFieldError::BadTransactionId,
            TextFieldError::InvalidContent => ftext::TextFieldError::InvalidContent,
            TextFieldError::InvalidSelection => ftext::TextFieldError::InvalidSelection,
            TextFieldError::InvalidArgument => ftext::TextFieldError::InvalidArgument,
        }
    }
}

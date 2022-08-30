// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {fidl_fuchsia_ui_clipboard as fclip, fuchsia_zircon as zx, thiserror::Error};

/// In-process representation of clipboard service errors. Convertible to or from
/// `fidl_fuchsia_ui_clipboard::ClipboardError`, albeit with some loss of fidelity.
#[derive(Error, Debug, Clone)]
pub(crate) enum ClipboardError {
    #[error("Internal error")]
    Internal(zx::Status),

    #[error("Clipboard is empty or item not found")]
    Empty,

    #[error("Invalid request")]
    InvalidRequest,

    #[error("Invalid ViewRef")]
    InvalidViewRef,

    #[error("ViewRef already registered for this service")]
    DuplicateViewRef,

    #[error("Unauthorized action")]
    Unauthorized,
}

impl ClipboardError {
    pub fn internal() -> Self {
        Self::Internal(zx::Status::INTERNAL)
    }

    pub fn internal_from_status(status: zx::Status) -> Self {
        Self::Internal(status)
    }
}

impl Into<fclip::ClipboardError> for ClipboardError {
    fn into(self) -> fclip::ClipboardError {
        match self {
            ClipboardError::Internal(_) => fclip::ClipboardError::Internal,
            ClipboardError::Empty => fclip::ClipboardError::Empty,
            ClipboardError::InvalidRequest => fclip::ClipboardError::InvalidRequest,
            ClipboardError::InvalidViewRef => fclip::ClipboardError::InvalidViewRef,
            ClipboardError::DuplicateViewRef => fclip::ClipboardError::InvalidViewRef,
            ClipboardError::Unauthorized => fclip::ClipboardError::Unauthorized,
        }
    }
}

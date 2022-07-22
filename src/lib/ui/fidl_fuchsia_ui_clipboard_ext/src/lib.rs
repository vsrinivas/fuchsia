// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Extensions for `fidl_fuchsia_ui_clipboard`.

use {fidl_fuchsia_ui_clipboard as fclip, thiserror::Error};

/// Newtype wrapper around `fidl_fuchsia_ui_clipboard::ClipboardError`. Implements
/// `std::error::Error` for easy conversion to `anyhow::Error`.
#[derive(Debug, Clone, Copy, Eq, PartialEq, Hash, Error)]
#[error("{:?}", .0)]
pub struct FidlClipboardError(fclip::ClipboardError);

impl Into<FidlClipboardError> for fclip::ClipboardError {
    fn into(self) -> FidlClipboardError {
        FidlClipboardError(self)
    }
}

impl Into<FidlClipboardError> for &fclip::ClipboardError {
    fn into(self) -> FidlClipboardError {
        FidlClipboardError(*self)
    }
}

impl Into<fclip::ClipboardError> for FidlClipboardError {
    fn into(self) -> fclip::ClipboardError {
        self.0
    }
}

#[cfg(test)]
mod tests {
    // Placeholder for when the code gets complicated enough to merit testing.
}

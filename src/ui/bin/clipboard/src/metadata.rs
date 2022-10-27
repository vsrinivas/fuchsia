// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {crate::errors::ClipboardError, fidl_fuchsia_ui_clipboard as fclip, fuchsia_zircon as zx};

/// In-process analog to [`fidl_fuchsia_ui_clipboard::ClipboardMetadata`].
#[derive(Debug, Eq, PartialEq, Hash, Clone)]
pub(crate) struct ClipboardMetadata {
    last_modified: zx::Time,
}

impl ClipboardMetadata {
    #[allow(dead_code)]
    pub fn with_last_modified(last_modified: zx::Time) -> Self {
        ClipboardMetadata { last_modified }
    }

    #[allow(dead_code)]
    pub fn with_last_modified_ns(last_modified_nanos: i64) -> Self {
        let last_modified = zx::Time::from_nanos(last_modified_nanos);
        Self::with_last_modified(last_modified)
    }
}

impl Into<fclip::ClipboardMetadata> for ClipboardMetadata {
    fn into(self) -> fclip::ClipboardMetadata {
        (&self).into()
    }
}

impl Into<fclip::ClipboardMetadata> for &ClipboardMetadata {
    fn into(self) -> fclip::ClipboardMetadata {
        fclip::ClipboardMetadata {
            last_modified: Some(self.last_modified.into_nanos()),
            ..fclip::ClipboardMetadata::EMPTY
        }
    }
}

impl TryInto<ClipboardMetadata> for fclip::ClipboardMetadata {
    type Error = ClipboardError;

    fn try_into(self) -> Result<ClipboardMetadata, Self::Error> {
        let output = ClipboardMetadata {
            last_modified: zx::Time::from_nanos(
                self.last_modified.ok_or(ClipboardError::InvalidRequest)?,
            ),
        };
        Ok(output)
    }
}

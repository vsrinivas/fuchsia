// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Extensions for `fidl_fuchsia_ui_view`.

use {
    fidl_fuchsia_ui_views::ViewRef,
    fuchsia_zircon::{self as zx, AsHandleRef},
};

/// Extension trait for [fidl_fuchsia_ui_view::ViewRef].
pub trait ViewRefExt {
    /// Returns the koid (kernel object ID) for this `ViewRef`. (This involves a system call.)
    fn get_koid(&self) -> Result<zx::Koid, zx::Status>;
}

impl ViewRefExt for ViewRef {
    fn get_koid(&self) -> Result<zx::Koid, zx::Status> {
        self.reference.as_handle_ref().get_koid()
    }
}

#[cfg(test)]
mod tests {
    use {super::*, fuchsia_scenic::ViewRefPair};

    #[test]
    fn smoke_test_get_koid() {
        let ViewRefPair { control_ref: _control_ref, view_ref } =
            ViewRefPair::new().expect("making ViewRefPair");

        assert!(view_ref.get_koid().is_ok());
    }
}

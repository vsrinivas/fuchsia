// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl::AsHandleRef, fidl_fuchsia_ui_views::ViewRef, fuchsia_zircon::HandleRef, std::sync::Arc,
};

/// A wrapper for `ViewRef` that lazily retrieves the `ViewRef`'s underlying, unique koid for
/// printing, making it possible to correlate cloned `ViewRef`s in log output. If the `ViewRef` is
/// invalid, the printed koid will be `0`.
///
/// (By default, debug-printing a `ViewRef` will reveal its handle ID, which is per-process and not
/// a canonical ID.)
pub struct ViewRefPrinter<'a>(HandleRef<'a>);

impl<'a> From<&'a Arc<ViewRef>> for ViewRefPrinter<'a> {
    fn from(vr: &'a Arc<ViewRef>) -> Self {
        ViewRefPrinter(vr.reference.as_handle_ref())
    }
}

impl<'a> From<&'a ViewRef> for ViewRefPrinter<'a> {
    fn from(vr: &'a ViewRef) -> Self {
        ViewRefPrinter(vr.reference.as_handle_ref())
    }
}

impl std::fmt::Debug for ViewRefPrinter<'_> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("ViewRef")
            .field("koid", &self.0.get_koid().map(|koid| koid.raw_koid()).unwrap_or_default())
            .finish()
    }
}

#[cfg(test)]
mod tests {
    use {super::*, fidl_fuchsia_ui_views_ext::ViewRefExt, fuchsia_scenic::ViewRefPair};

    #[test]
    fn smoke_test_view_ref_printer() {
        let ViewRefPair { control_ref: _control_ref, view_ref } =
            ViewRefPair::new().expect("ViewRefPair::new()");

        let expected_koid = view_ref.get_koid().expect("get_koid()").raw_koid();
        // Should look like 'ViewRef { koid: 123 }'
        let expected_str = format!("ViewRef {{ koid: {expected_koid} }}");

        let printer = ViewRefPrinter::from(&view_ref);
        let actual_str = format!("{:?}", &printer);

        assert_eq!(expected_str, actual_str);
    }
}

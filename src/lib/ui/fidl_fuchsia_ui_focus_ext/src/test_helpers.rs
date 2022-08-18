// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Helper methods for tests involving `FocusChain`.

use {
    fidl_fuchsia_ui_focus::FocusChain, fidl_fuchsia_ui_views::ViewRefControl,
    fuchsia_scenic::ViewRefPair,
};

/// Makes a focus chain of the given length, returning both the `FocusChain` instance and a vector
/// of all the corresponding `ViewRefControl`s, in the same order.
///
/// The `ViewRef`s are real, but of course do not correspond to any actual views.
pub fn make_focus_chain(length: usize) -> (FocusChain, Vec<ViewRefControl>) {
    let mut view_refs = vec![];
    let mut control_refs = vec![];
    for _ in 0..length {
        let ViewRefPair { control_ref, view_ref } = ViewRefPair::new().expect("making ViewRefPair");
        view_refs.push(view_ref);
        control_refs.push(control_ref);
    }
    let focus_chain = FocusChain { focus_chain: Some(view_refs), ..FocusChain::EMPTY };
    (focus_chain, control_refs)
}

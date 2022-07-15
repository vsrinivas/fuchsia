// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Extensions for `fidl_fuchsia_ui_focus`.

use {fidl_fuchsia_ui_focus::FocusChain, fuchsia_scenic as scenic, fuchsia_zircon::Status};

/// Extension trait for [fidl_fuchsia_ui_focus::FocusChain].
pub trait FocusChainExt
where
    Self: Sized,
{
    /// Creates a new `FocusChain`, in which all of the `ViewRef`s have been duplicated from the
    /// original.
    fn duplicate(&self) -> Result<Self, Status>;
}

impl FocusChainExt for FocusChain {
    fn duplicate(&self) -> Result<Self, Status> {
        let v = match self.focus_chain.as_ref() {
            None => None,
            Some(v) => Some(
                v.iter().map(|vr| scenic::duplicate_view_ref(vr)).collect::<Result<Vec<_>, _>>()?,
            ),
        };
        let output = FocusChain { focus_chain: v, ..FocusChain::EMPTY };
        Ok(output)
    }
}

#[cfg(test)]
mod tests {
    use {super::*, fidl_fuchsia_ui_views_ext::ViewRefExt, scenic::ViewRefPair};

    #[test]
    fn test_duplicate_focus_chain() {
        let ViewRefPair { control_ref: _vrc1, view_ref: vr1 } =
            scenic::ViewRefPair::new().expect("making ViewRefPair");
        let ViewRefPair { control_ref: _vrc2, view_ref: vr2 } =
            scenic::ViewRefPair::new().expect("making ViewRefPair");

        let mut source = FocusChain::EMPTY;
        source.focus_chain = Some(vec![vr1, vr2]);

        let target = source.duplicate().expect("error in duplicate()");

        let source_focus_chain = source.focus_chain.as_ref().unwrap();
        let target_focus_chain = target.focus_chain.as_ref().unwrap();

        assert_eq!(2, target_focus_chain.len());

        assert_eq!(
            source_focus_chain[0].get_koid().expect("getting koid"),
            target_focus_chain[0].get_koid().expect("getting koid"),
        );

        assert_eq!(
            source_focus_chain[1].get_koid().expect("getting koid"),
            target_focus_chain[1].get_koid().expect("getting koid"),
        );
    }

    #[test]
    fn test_duplicate_focus_chain_empty() {
        let source = FocusChain::EMPTY;
        let target = source.duplicate().expect("error in duplicate()");

        assert_eq!(target.focus_chain, None);
    }
}

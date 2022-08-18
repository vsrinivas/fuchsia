// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Extensions for `fidl_fuchsia_ui_focus`.

use {
    fidl_fuchsia_ui_focus::{FocusChain, FocusKoidChain},
    fidl_fuchsia_ui_views_ext::ViewRefExt,
    fuchsia_scenic as scenic,
    fuchsia_zircon::{Koid, Status},
};

/// Extension trait for [`fidl_fuchsia_ui_focus::FocusChain`] and
/// [`fidl_fuchsia_ui_focus::FocusKoidChain`].
pub trait FocusChainExt
where
    Self: Sized,
{
    /// Returns the number of views in the chain.
    fn len(&self) -> usize;

    /// Returns true if there are no views in the chain.
    fn is_empty(&self) -> bool {
        self.len() == 0
    }

    /// Creates a new chain, in which all of the `ViewRef`s (or koids) have been duplicated from the
    /// original. Returns an error if duplicating any of the `ViewRef`s fails.
    fn duplicate(&self) -> Result<Self, Status>;

    /// Returns a fallible iterator over the chain's koids. If any koid cannot be retrieved, the
    /// iterator yields an error in its place.
    fn koids(&self) -> Box<dyn ExactSizeIterator<Item = Result<Koid, Status>> + '_>;

    /// Returns true if the two chains' `ViewRef`s correspond to the same koids, in the same
    /// order. If any of the koids cannot be retrieved, returns an error.
    fn equivalent<O: FocusChainExt>(&self, other: &O) -> Result<bool, Status> {
        let self_len = self.len();
        if self_len != other.len() {
            return Ok(false);
        }
        if self_len == 0 {
            return Ok(true);
        }

        let mut zipped = std::iter::zip(self.koids(), other.koids());
        while let Some((a, b)) = zipped.next() {
            if a? != b? {
                return Ok(false);
            }
        }
        Ok(true)
    }

    /// Converts this chain into a [`FocusKoidChain`], which contains just koids instead of
    /// [`ViewRef`]s.
    fn to_focus_koid_chain(&self) -> Result<FocusKoidChain, Status>;
}

impl FocusChainExt for FocusChain {
    fn len(&self) -> usize {
        match self.focus_chain.as_ref() {
            None => 0,
            Some(v) => v.len(),
        }
    }

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

    fn koids(&self) -> Box<dyn ExactSizeIterator<Item = Result<Koid, Status>> + '_> {
        match &self.focus_chain {
            None => Box::new(std::iter::empty()),
            Some(v) => Box::new(v.iter().map(|vr| vr.get_koid())),
        }
    }

    fn to_focus_koid_chain(&self) -> Result<FocusKoidChain, Status> {
        let raw_koids = match self.focus_chain.as_ref() {
            None => None,
            Some(_) => Some(
                self.koids()
                    .map(|result| -> Result<u64, Status> { result.map(|koid| koid.raw_koid()) })
                    .collect::<Result<Vec<_>, _>>()?,
            ),
        };
        Ok(FocusKoidChain { focus_chain: raw_koids, ..FocusKoidChain::EMPTY })
    }
}

impl FocusChainExt for FocusKoidChain {
    fn len(&self) -> usize {
        match self.focus_chain.as_ref() {
            None => 0,
            Some(v) => v.len(),
        }
    }

    /// Clones this `FocusKoidChain`.
    fn duplicate(&self) -> Result<Self, Status> {
        Ok(self.clone())
    }

    fn koids(&self) -> Box<dyn ExactSizeIterator<Item = Result<Koid, Status>> + '_> {
        match &self.focus_chain {
            None => Box::new(std::iter::empty()),
            Some(v) => Box::new(v.iter().map(|raw| Ok(Koid::from_raw(*raw)))),
        }
    }

    /// Clones this `FocusKoidChain`.
    fn to_focus_koid_chain(&self) -> Result<FocusKoidChain, Status> {
        self.duplicate()
    }
}

#[cfg(test)]
mod tests {
    use {super::*, fidl_fuchsia_ui_focus_test_helpers::make_focus_chain};

    #[test]
    fn focus_chain_duplicate() {
        let (source, _control_refs) = make_focus_chain(2);
        let target = source.duplicate().expect("error in duplicate()");

        let source_koids = source.koids().collect::<Result<Vec<_>, _>>().unwrap();
        let target_koids = target.koids().collect::<Result<Vec<_>, _>>().unwrap();

        assert_eq!(source_koids, target_koids,);
    }

    #[test]
    fn focus_chain_duplicate_empty() {
        let source = FocusChain::EMPTY;
        let target = source.duplicate().expect("error in duplicate()");

        assert_eq!(target.focus_chain, None);
    }

    #[test]
    fn focus_chain_equivalent_empty() {
        let a = FocusChain::EMPTY;
        let b = FocusChain::EMPTY;
        assert!(a.equivalent(&b).unwrap());

        let (a, _) = make_focus_chain(0);
        let (b, _) = make_focus_chain(0);
        assert!(a.equivalent(&b).unwrap());
    }

    #[test]
    fn focus_chain_equivalent_same_lengths() {
        let (a, _a) = make_focus_chain(3);
        let (b, _b) = make_focus_chain(3);
        assert!(!a.equivalent(&b).unwrap());
    }

    #[test]
    fn focus_chain_equivalent_different_lengths() {
        let (a, _a) = make_focus_chain(3);
        let (b, _b) = make_focus_chain(5);
        assert!(!a.equivalent(&b).unwrap());
    }

    #[test]
    fn focus_chain_equivalent_duplicates() {
        let (a, _a) = make_focus_chain(3);
        let b = a.duplicate().expect("duplicate");
        assert!(a.equivalent(&b).unwrap());
    }

    #[test]
    fn focus_chain_to_focus_koid_chain() {
        let (focus_chain, _view_control_refs) = make_focus_chain(2);
        let raw_koids = vec![
            focus_chain.focus_chain.as_ref().unwrap()[0].get_koid().unwrap().raw_koid(),
            focus_chain.focus_chain.as_ref().unwrap()[1].get_koid().unwrap().raw_koid(),
        ];

        let expected = FocusKoidChain { focus_chain: Some(raw_koids), ..FocusKoidChain::EMPTY };

        let actual = focus_chain.to_focus_koid_chain().unwrap();

        assert_eq!(expected, actual);
    }

    #[test]
    fn focus_chain_equivalent_focus_koid_chain() {
        let (chain_a, _vcr_a) = make_focus_chain(2);
        let (chain_b, _vcr_b) = make_focus_chain(2);

        let koid_chain_a = chain_a.to_focus_koid_chain().unwrap();
        let koid_chain_b = chain_b.to_focus_koid_chain().unwrap();

        assert!(chain_a.equivalent(&koid_chain_a).unwrap());
        assert!(!chain_a.equivalent(&koid_chain_b).unwrap())
    }
}

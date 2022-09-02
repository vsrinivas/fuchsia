// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! General utilities for converting types.

/// Produces an owned value `T` from Self.
///
/// This trait is useful for implementing functions that take a value that may
/// or may not end up needing to be consumed, but where the caller doesn't
/// necessarily want or need to keep the value either.
///
/// This trait is blanket-implemented for `T` as a pass-through and for `&T`
/// where `T: Clone` by cloning `self`.
pub(crate) trait OwnedOrCloned<T> {
    fn into_owned(self) -> T;
}

impl<T> OwnedOrCloned<T> for T {
    fn into_owned(self) -> T {
        self
    }
}

impl<'s, T: Clone> OwnedOrCloned<T> for &'s T {
    fn into_owned(self) -> T {
        self.clone()
    }
}

#[cfg(test)]
mod test {
    use super::OwnedOrCloned;

    #[derive(Debug, Eq, PartialEq)]
    struct Uncloneable(u8);

    #[derive(Clone, Debug, Eq, PartialEq)]
    struct Cloneable(u16);

    #[test]
    fn owned_as_owned_or_cloned() {
        assert_eq!(Uncloneable(45).into_owned(), Uncloneable(45));
    }

    #[test]
    fn reference_as_owned_or_cloned() {
        let cloneable = Cloneable(32);
        let reference = &cloneable;
        let cloned: Cloneable = reference.into_owned();
        assert_eq!(cloned, cloneable);
    }
}

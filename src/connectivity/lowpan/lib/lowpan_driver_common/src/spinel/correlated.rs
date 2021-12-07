// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use spinel_pack::*;
use std::collections::HashSet;
use std::hash::{Hash, Hasher};
use std::io;

/// A type that wraps around something that implements the `Correlated` trait.
/// This allows the type to have different correlation behavior when in a
/// [`HashSet`].
#[derive(Clone, Debug, Eq)]
pub struct CorrelatedBox<T: Correlated>(pub T);

/// A trait that provides a way to
pub trait Correlated: Sized + Send + core::fmt::Debug {
    /// Correlated version of the hash function.
    fn correlation_hash<H: Hasher>(&self, state: &mut H);

    /// Correlated version of the equals operator.
    fn correlation_eq(&self, other: &Self) -> bool;

    /// Convenience method to put the value in a correlated box.
    fn correlated_box(self) -> CorrelatedBox<Self>
    where
        Self: Sized,
    {
        CorrelatedBox(self)
    }
}

#[derive(Clone, Debug)]
pub enum CorrelatedDiff<'a, T: Correlated> {
    /// Entry was added
    Added(&'a T),

    /// Entry was removed
    Removed(&'a T),

    /// Entry changed from the first argument to the second argument
    Changed(&'a T, &'a T),
}

impl<'a, T: Sized + Correlated + Eq> CorrelatedDiff<'a, T> {
    pub fn diff(
        old: &'a HashSet<CorrelatedBox<T>>,
        new: &'a HashSet<CorrelatedBox<T>>,
    ) -> Box<dyn Iterator<Item = Self> + 'a> {
        let removed = old.difference(new).map(|x| CorrelatedDiff::Removed(&x.0));
        let added = new.difference(old).map(|x| CorrelatedDiff::Added(&x.0));
        let changed = new.intersection(old).filter_map(move |x| {
            let y = old.get(x).unwrap();
            if x.0 == y.0 {
                None
            } else {
                Some(CorrelatedDiff::Changed(&y.0, &x.0))
            }
        });

        Box::new(removed.chain(added).chain(changed))
    }
}

impl<T: Correlated> Hash for CorrelatedBox<T> {
    fn hash<H: Hasher>(&self, state: &mut H) {
        self.0.correlation_hash(state);
    }
}

impl<T: Correlated> PartialEq for CorrelatedBox<T> {
    fn eq(&self, other: &Self) -> bool {
        self.0.correlation_eq(&other.0)
    }
}

impl<T: Correlated + TryPackAs<V>, V> TryPackAs<V> for CorrelatedBox<T> {
    fn pack_as_len(&self) -> io::Result<usize> {
        self.0.pack_as_len()
    }

    fn try_pack_as<U: std::io::Write + ?Sized>(&self, buffer: &mut U) -> io::Result<usize> {
        self.0.try_pack_as(buffer)
    }
}

impl<T: Correlated + TryPack> TryPack for CorrelatedBox<T> {
    fn pack_len(&self) -> io::Result<usize> {
        self.0.pack_len()
    }

    fn try_pack<U: std::io::Write + ?Sized>(&self, buffer: &mut U) -> io::Result<usize> {
        self.0.try_pack(buffer)
    }
}

impl<'a, T, V> TryUnpackAs<'a, V> for CorrelatedBox<T>
where
    T: Correlated + TryUnpackAs<'a, V>,
{
    fn try_unpack_as(iter: &mut std::slice::Iter<'a, u8>) -> anyhow::Result<Self> {
        T::try_unpack_as(iter).map(Correlated::correlated_box)
    }
}

impl<'a, T> TryUnpack<'a> for CorrelatedBox<T>
where
    T: Correlated + TryUnpack<'a>,
    <T as spinel_pack::TryUnpack<'a>>::Unpacked: Correlated,
{
    type Unpacked = CorrelatedBox<<T as spinel_pack::TryUnpack<'a>>::Unpacked>;

    fn try_array_unpack(iter: &mut std::slice::Iter<'a, u8>) -> anyhow::Result<Self::Unpacked> {
        T::try_array_unpack(iter).map(Correlated::correlated_box)
    }

    fn try_unpack(iter: &mut std::slice::Iter<'a, u8>) -> anyhow::Result<Self::Unpacked> {
        T::try_unpack(iter).map(Correlated::correlated_box)
    }
}

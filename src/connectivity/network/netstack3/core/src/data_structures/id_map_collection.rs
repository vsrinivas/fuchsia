// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Identifier map collection data structure.
//!
//! Defines [`IdMapCollection`], which is a generic map collection that can be
//! keyed on [`IdMapCollectionKey`], which is a two-level key structure.
//!
//! Used to provide collections keyed on [`crate::DeviceId`] that match hot path
//! performance requirements.

use alloc::vec::Vec;

use super::id_map::{self, EntryKey, IdMap};

/// A key that can index items in [`IdMapCollection`].
///
/// An `IdMapCollectionKey` is a key with two levels: `variant` and `id`. The
/// number of `variant`s must be fixed and known at compile time, and is
/// typically mapped to a number of `enum` variants (nested or not).
pub trait IdMapCollectionKey {
    /// The number of variants this key supports.
    const VARIANT_COUNT: usize;

    /// Get the variant index for this key.
    ///
    /// # Panics
    ///
    /// Callers may assume that `get_variant` returns a value in the range `[0,
    /// VARIANT_COUNT)`, and may panic if that assumption is violated.
    fn get_variant(&self) -> usize;

    /// Get the id index for this key.
    fn get_id(&self) -> usize;
}

impl<O> EntryKey for O
where
    O: IdMapCollectionKey,
{
    fn get_key_index(&self) -> usize {
        <O as IdMapCollectionKey>::get_id(self)
    }
}

/// A vacant entry from an [`IdMapCollection`].
pub struct VacantEntry<'a, K, T> {
    entry: id_map::VacantEntry<'a, K, T>,
    count: &'a mut usize,
}

impl<'a, K, T> VacantEntry<'a, K, T> {
    /// Sets the value of the entry with the VacantEntry's key, and returns a
    /// mutable reference to it.
    pub fn insert(self, value: T) -> &'a mut T
    where
        K: EntryKey,
    {
        let Self { entry, count } = self;
        *count += 1;
        entry.insert(value)
    }

    /// Gets a reference to the key that would be used when inserting a value
    /// through the `VacantEntry`.
    pub fn key(&self) -> &K {
        self.entry.key()
    }

    /// Take ownership of the key.
    pub fn into_key(self) -> K {
        self.entry.into_key()
    }

    /// Changes the key type of this `VacantEntry` to another key `X` that still
    /// maps to the same index in an `IdMap`.
    ///
    /// # Panics
    ///
    /// Panics if the resulting mapped key from `f` does not return the same
    /// value for [`EntryKey::get_key_index`] as the old key did.
    pub(crate) fn map_key<X, F>(self, f: F) -> VacantEntry<'a, X, T>
    where
        K: EntryKey,
        X: EntryKey,
        F: FnOnce(K) -> X,
    {
        let Self { entry, count } = self;
        VacantEntry { entry: entry.map_key(f), count }
    }
}

/// An occupied entry from an [`IdMapCollection`].
pub struct OccupiedEntry<'a, K, T> {
    entry: id_map::OccupiedEntry<'a, K, T>,
    count: &'a mut usize,
}

impl<'a, K: EntryKey, T> OccupiedEntry<'a, K, T> {
    /// Gets a reference to the key in the entry.
    pub fn key(&self) -> &K {
        self.entry.key()
    }

    /// Gets a reference to the value in the entry.
    pub fn get(&self) -> &T {
        self.entry.get()
    }

    /// Gets a mutable reference to the value in the entry.
    ///
    /// If you need a reference to the `OccupiedEntry` which may outlive the
    /// destruction of the entry value, see [`OccupiedEntry::into_mut`].
    pub fn get_mut(&mut self) -> &mut T {
        self.entry.get_mut()
    }

    /// Converts the `OccupiedEntry` into a mutable reference to the value in
    /// the entry with a lifetime bound to the map itself.
    ///
    /// If you need multiple references to the `OccupiedEntry`, see
    /// [`OccupiedEntry::get_mut`].
    pub fn into_mut(self) -> &'a mut T {
        self.entry.into_mut()
    }

    /// Sets the value of the entry, and returns the entry's old value.
    pub fn insert(&mut self, value: T) -> T {
        self.entry.insert(value)
    }

    /// Takes the value out of the entry, and returns it.
    pub fn remove(self) -> T {
        let Self { entry, count } = self;
        *count -= 1;
        entry.remove()
    }

    /// Changes the key type of this `OccupiedEntry` to another key `X` that
    /// still maps to the same value.
    ///
    /// # Panics
    ///
    /// Panics if the resulting mapped key from `f` does not return the same
    /// value for [`EntryKey::get_key_index`] as the old key did.
    pub(crate) fn map_key<X, F>(self, f: F) -> OccupiedEntry<'a, X, T>
    where
        K: EntryKey,
        X: EntryKey,
        F: FnOnce(K) -> X,
    {
        let Self { entry, count } = self;
        OccupiedEntry { entry: entry.map_key(f), count }
    }
}

/// A view into an in-place entry in a map that can be vacant or occupied.
pub enum Entry<'a, K, T> {
    /// A vacant entry.
    Vacant(VacantEntry<'a, K, T>),
    /// An occupied entry.
    Occupied(OccupiedEntry<'a, K, T>),
}

impl<'a, K: EntryKey, T> Entry<'a, K, T> {
    /// Returns a reference to this entry's key.
    pub fn key(&self) -> &K {
        match self {
            Entry::Occupied(e) => e.key(),
            Entry::Vacant(e) => e.key(),
        }
    }

    /// Ensures a value is in the entry by inserting `default` if empty, and
    /// returns a mutable reference to the value in the entry.
    pub fn or_insert(self, default: T) -> &'a mut T
    where
        K: EntryKey,
    {
        self.or_insert_with(|| default)
    }

    /// Ensures a value is in the entry by inserting the result of the function
    /// `f` if empty, and returns a mutable reference to the value in the entry.
    pub fn or_insert_with<F: FnOnce() -> T>(self, f: F) -> &'a mut T {
        match self {
            Entry::Occupied(e) => e.into_mut(),
            Entry::Vacant(e) => e.insert(f()),
        }
    }

    /// Ensures a value is in the entry by inserting the default value if empty,
    /// and returns a mutable reference to the value in the entry.
    pub fn or_default(self) -> &'a mut T
    where
        T: Default,
        K: EntryKey,
    {
        self.or_insert_with(<T as Default>::default)
    }

    /// Provides in-place mutable access to an occupied entry before any
    /// potential inserts into the map.
    pub fn and_modify<F: FnOnce(&mut T)>(self, f: F) -> Self {
        match self {
            Entry::Occupied(mut e) => {
                f(e.get_mut());
                Entry::Occupied(e)
            }
            Entry::Vacant(e) => Entry::Vacant(e),
        }
    }
}

/// An iterator wrapper used to implement ExactSizeIterator.
///
/// Wraps an iterator of type `I`, keeping track of the number of elements it
/// is expected to produce.
struct SizeAugmentedIterator<I> {
    wrapped: I,
    remaining: usize,
}

impl<I: Iterator> Iterator for SizeAugmentedIterator<I> {
    type Item = I::Item;

    fn next(&mut self) -> Option<Self::Item> {
        let Self { wrapped, remaining } = self;
        match wrapped.next() {
            Some(v) => {
                *remaining -= 1;
                Some(v)
            }
            None => {
                assert_eq!(remaining, &0);
                None
            }
        }
    }

    fn size_hint(&self) -> (usize, Option<usize>) {
        (self.remaining, Some(self.remaining))
    }
}

impl<I: Iterator> ExactSizeIterator for SizeAugmentedIterator<I> {}

/// A generic collection indexed by an [`IdMapCollectionKey`].
///
/// `IdMapCollection` provides the same performance guarantees as [`IdMap`], but
/// provides a two-level keying scheme that matches the pattern used in
/// [`crate::DeviceId`].
pub struct IdMapCollection<K: IdMapCollectionKey, T> {
    // TODO(brunodalbo): we define a vector container here because we can't just
    // define a fixed array length based on an associated const in
    // IdMapCollectionKey. When rust issue #43408 gets resolved we can switch
    // this to use the associated const and just have a fixed length array.
    data: Vec<IdMap<T>>,
    count: usize,
    _marker: core::marker::PhantomData<K>,
}

impl<K: IdMapCollectionKey, T> IdMapCollection<K, T> {
    /// Creates a new empty `IdMapCollection`.
    pub fn new() -> Self {
        let mut data = Vec::new();
        data.resize_with(K::VARIANT_COUNT, IdMap::default);
        Self { data, count: 0, _marker: core::marker::PhantomData }
    }

    fn get_map(&self, key: &K) -> &IdMap<T> {
        &self.data[key.get_variant()]
    }

    fn get_entry(&mut self, key: &K) -> Entry<'_, usize, T> {
        let Self { data, count, _marker } = self;
        match data[key.get_variant()].entry(key.get_id()) {
            id_map::Entry::Occupied(entry) => Entry::Occupied(OccupiedEntry { entry, count }),
            id_map::Entry::Vacant(entry) => Entry::Vacant(VacantEntry { entry, count }),
        }
    }

    /// Returns `true` if the `IdMapCollection` holds no items.
    pub fn is_empty(&self) -> bool {
        let Self { count, data: _, _marker } = self;
        *count == 0
    }

    /// Returns a reference to the item indexed by `key`, or `None` if the `key`
    /// doesn't exist.
    pub fn get(&self, key: &K) -> Option<&T> {
        self.get_map(key).get(key.get_id())
    }

    /// Returns a mutable reference to the item indexed by `key`, or `None` if
    /// the `key` doesn't exist.
    pub fn get_mut(&mut self, key: &K) -> Option<&mut T> {
        match self.get_entry(key) {
            Entry::Occupied(e) => Some(e.into_mut()),
            Entry::Vacant(_) => None,
        }
    }

    /// Removes item indexed by `key` from the container.
    ///
    /// Returns the removed item if it exists, or `None` otherwise.
    pub fn remove(&mut self, key: &K) -> Option<T> {
        match self.get_entry(key) {
            Entry::Occupied(e) => Some(e.remove()),
            Entry::Vacant(_) => None,
        }
    }

    /// Inserts `item` at `key`.
    ///
    /// If the [`IdMapCollection`] already contained an item indexed by `key`,
    /// `insert` returns it, or `None` otherwise.
    pub fn insert(&mut self, key: &K, item: T) -> Option<T> {
        match self.get_entry(key) {
            Entry::Occupied(mut e) => Some(e.insert(item)),
            Entry::Vacant(e) => {
                let _: &mut T = e.insert(item);
                None
            }
        }
    }

    /// Creates an iterator over the containing items.
    pub fn iter(&self) -> impl ExactSizeIterator<Item = &T> {
        let Self { data, count, _marker } = self;
        SizeAugmentedIterator {
            wrapped: data.iter().flat_map(|m| m.iter()).map(|(_, v)| v),
            remaining: *count,
        }
    }

    /// Creates a mutable iterator over the containing items.
    pub fn iter_mut(&mut self) -> impl ExactSizeIterator<Item = &mut T> {
        let Self { data, count, _marker } = self;
        SizeAugmentedIterator {
            wrapped: data.iter_mut().flat_map(|m| m.iter_mut()).map(|(_, v)| v),
            remaining: *count,
        }
    }

    /// Gets the given key's corresponding entry in the map for in-place
    /// manipulation.
    pub fn entry(&mut self, key: K) -> Entry<'_, K, T> {
        match self.get_entry(&key) {
            Entry::Occupied(e) => Entry::Occupied(e.map_key(|_| key)),
            Entry::Vacant(e) => Entry::Vacant(e.map_key(|_| key)),
        }
    }
}

impl<K: IdMapCollectionKey, T> Default for IdMapCollection<K, T> {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::testutil::assert_empty;

    #[derive(Copy, Clone, Eq, PartialEq, Debug)]
    enum FakeVariants {
        A,
        B,
        C,
    }

    #[derive(Copy, Clone, Eq, PartialEq, Debug)]
    struct FakeKey {
        id: usize,
        var: FakeVariants,
    }

    impl FakeKey {
        const fn new(id: usize, var: FakeVariants) -> Self {
            Self { id, var }
        }
    }

    impl IdMapCollectionKey for FakeKey {
        const VARIANT_COUNT: usize = 3;

        fn get_variant(&self) -> usize {
            match self.var {
                FakeVariants::A => 0,
                FakeVariants::B => 1,
                FakeVariants::C => 2,
            }
        }

        fn get_id(&self) -> usize {
            self.id
        }
    }

    type TestCollection = IdMapCollection<FakeKey, i32>;

    const KEY_A: FakeKey = FakeKey::new(0, FakeVariants::A);
    const KEY_B: FakeKey = FakeKey::new(2, FakeVariants::B);
    const KEY_C: FakeKey = FakeKey::new(4, FakeVariants::C);

    #[test]
    fn test_insert_and_get() {
        let mut t = TestCollection::new();
        let IdMapCollection { data, count, _marker } = &t;
        assert_empty(data[0].iter());
        assert_empty(data[1].iter());
        assert_empty(data[2].iter());
        assert_eq!(count, &0);

        assert_eq!(t.insert(&KEY_A, 1), None);
        let IdMapCollection { data, count, _marker } = &t;
        assert!(!data[0].is_empty());
        assert_eq!(count, &1);

        assert_eq!(t.insert(&KEY_B, 2), None);
        let IdMapCollection { data, count, _marker } = &t;
        assert!(!data[1].is_empty());
        assert_eq!(count, &2);

        assert_eq!(*t.get(&KEY_A).unwrap(), 1);
        assert_eq!(t.get(&KEY_C), None);

        *t.get_mut(&KEY_B).unwrap() = 3;
        assert_eq!(*t.get(&KEY_B).unwrap(), 3);
    }

    #[test]
    fn test_remove() {
        let mut t = TestCollection::new();
        assert_eq!(t.insert(&KEY_B, 15), None);
        assert_eq!(t.remove(&KEY_B).unwrap(), 15);
        let IdMapCollection { data: _, count, _marker } = &t;
        assert_eq!(count, &0);

        assert_eq!(t.remove(&KEY_B), None);
    }

    #[test]
    fn test_iter() {
        let mut t = TestCollection::new();
        assert_eq!(t.insert(&KEY_A, 15), None);
        assert_eq!(t.insert(&KEY_B, -5), None);
        assert_eq!(t.insert(&KEY_C, -10), None);
        let mut c = 0;
        let mut sum = 0;
        for i in t.iter() {
            c += 1;
            sum += *i;
        }
        assert_eq!(c, 3);
        assert_eq!(sum, 0);
    }

    #[test]
    fn test_iter_len() {
        let mut t = TestCollection::new();
        assert_eq!(t.insert(&KEY_A, 1), None);
        assert_eq!(t.insert(&KEY_B, 1), None);
        assert_eq!(t.insert(&KEY_C, 1), None);
        assert_eq!(t.iter().len(), 3);
        assert_eq!(t.remove(&KEY_A), Some(1));
        assert_eq!(t.iter().len(), 2);
    }

    #[test]
    fn test_is_empty() {
        let mut t = TestCollection::new();
        assert!(t.is_empty());
        assert_eq!(t.insert(&KEY_B, 15), None);
        assert!(!t.is_empty());
    }

    #[test]
    fn test_iter_mut() {
        let mut t = TestCollection::new();
        assert_eq!(t.insert(&KEY_A, 15), None);
        assert_eq!(t.insert(&KEY_B, -5), None);
        assert_eq!(t.insert(&KEY_C, -10), None);
        for i in t.iter_mut() {
            *i *= 2;
        }
        assert_eq!(*t.get(&KEY_A).unwrap(), 30);
        assert_eq!(*t.get(&KEY_B).unwrap(), -10);
        assert_eq!(*t.get(&KEY_C).unwrap(), -20);
        assert_eq!(t.iter_mut().len(), 3);
    }

    #[test]
    fn test_entry() {
        let mut t = TestCollection::new();
        assert_eq!(*t.entry(KEY_A).or_insert(2), 2);
        assert_eq!(
            *t.entry(KEY_A)
                .and_modify(|v| {
                    *v = 10;
                })
                .or_insert(5),
            10
        );
        assert_eq!(
            *t.entry(KEY_B)
                .and_modify(|v| {
                    *v = 10;
                })
                .or_insert(5),
            5
        );
        assert_eq!(*t.entry(KEY_C).or_insert_with(|| 7), 7);

        assert_eq!(*t.entry(KEY_C).key(), KEY_C);
        assert_eq!(*t.get(&KEY_A).unwrap(), 10);
        assert_eq!(*t.get(&KEY_B).unwrap(), 5);
        assert_eq!(*t.get(&KEY_C).unwrap(), 7);
    }
}

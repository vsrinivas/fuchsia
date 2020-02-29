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

use super::id_map::{Entry, EntryKey};
use super::IdMap;

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
    _marker: core::marker::PhantomData<K>,
}

impl<K: IdMapCollectionKey, T> IdMapCollection<K, T> {
    /// Creates a new empty `IdMapCollection`.
    pub fn new() -> Self {
        let mut data = Vec::new();
        data.resize_with(K::VARIANT_COUNT, IdMap::default);
        Self { data, _marker: core::marker::PhantomData }
    }

    fn get_map(&self, key: &K) -> &IdMap<T> {
        &self.data[key.get_variant()]
    }

    fn get_map_mut(&mut self, key: &K) -> &mut IdMap<T> {
        &mut self.data[key.get_variant()]
    }

    /// Returns `true` if the `IdMapCollection` holds no items.
    pub fn is_empty(&self) -> bool {
        self.data.iter().all(|d| d.is_empty())
    }

    /// Returns a reference to the item indexed by `key`, or `None` if the `key`
    /// doesn't exist.
    pub fn get(&self, key: &K) -> Option<&T> {
        self.get_map(key).get(key.get_id())
    }

    /// Returns a mutable reference to the item indexed by `key`, or `None` if
    /// the `key` doesn't exist.
    pub fn get_mut(&mut self, key: &K) -> Option<&mut T> {
        self.get_map_mut(key).get_mut(key.get_id())
    }

    /// Removes item indexed by `key` from the container.
    ///
    /// Returns the removed item if it exists, or `None` otherwise.
    pub fn remove(&mut self, key: &K) -> Option<T> {
        self.get_map_mut(key).remove(key.get_id())
    }

    /// Inserts `item` at `key`.
    ///
    /// If the [`IdMapCollection`] already contained an item indexed by `key`,
    /// `insert` returns it, or `None` otherwise.
    pub fn insert(&mut self, key: &K, item: T) -> Option<T> {
        self.get_map_mut(key).insert(key.get_id(), item)
    }

    /// Creates an iterator over the containing items.
    pub fn iter(&self) -> impl Iterator<Item = &T> {
        self.data.iter().flat_map(|m| m.iter()).map(|(_, v)| v)
    }

    /// Creates a mutable iterator over the containing items.
    pub fn iter_mut(&mut self) -> impl Iterator<Item = &mut T> {
        self.data.iter_mut().flat_map(|m| m.iter_mut()).map(|(_, v)| v)
    }

    /// Gets the given key's corresponding entry in the map for in-place
    /// manipulation.
    pub fn entry(&mut self, key: K) -> Entry<'_, K, T> {
        self.get_map_mut(&key).entry(key.get_id()).map_key(|_| key)
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

    #[derive(Copy, Clone, Eq, PartialEq, Debug)]
    enum MockVariants {
        A,
        B,
        C,
    }

    #[derive(Copy, Clone, Eq, PartialEq, Debug)]
    struct MockKey {
        id: usize,
        var: MockVariants,
    }

    impl MockKey {
        const fn new(id: usize, var: MockVariants) -> Self {
            Self { id, var }
        }
    }

    impl IdMapCollectionKey for MockKey {
        const VARIANT_COUNT: usize = 3;

        fn get_variant(&self) -> usize {
            match self.var {
                MockVariants::A => 0,
                MockVariants::B => 1,
                MockVariants::C => 2,
            }
        }

        fn get_id(&self) -> usize {
            self.id
        }
    }

    type TestCollection = IdMapCollection<MockKey, i32>;

    const KEY_A: MockKey = MockKey::new(0, MockVariants::A);
    const KEY_B: MockKey = MockKey::new(2, MockVariants::B);
    const KEY_C: MockKey = MockKey::new(4, MockVariants::C);

    #[test]
    fn test_insert_and_get() {
        let mut t = TestCollection::new();
        assert!(t.data[0].is_empty());
        assert!(t.data[1].is_empty());
        assert!(t.data[2].is_empty());
        assert!(t.insert(&KEY_A, 1).is_none());
        assert!(!t.data[0].is_empty());
        assert!(t.insert(&KEY_B, 2).is_none());
        assert!(!t.data[1].is_empty());

        assert_eq!(*t.get(&KEY_A).unwrap(), 1);
        assert!(t.get(&KEY_C).is_none());

        *t.get_mut(&KEY_B).unwrap() = 3;
        assert_eq!(*t.get(&KEY_B).unwrap(), 3);
    }

    #[test]
    fn test_remove() {
        let mut t = TestCollection::new();
        assert!(t.insert(&KEY_B, 15).is_none());
        assert_eq!(t.remove(&KEY_B).unwrap(), 15);
        assert!(t.remove(&KEY_B).is_none());
    }

    #[test]
    fn test_iter() {
        let mut t = TestCollection::new();
        assert!(t.insert(&KEY_A, 15).is_none());
        assert!(t.insert(&KEY_B, -5).is_none());
        assert!(t.insert(&KEY_C, -10).is_none());
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
    fn test_is_empty() {
        let mut t = TestCollection::new();
        assert!(t.is_empty());
        assert!(t.insert(&KEY_B, 15).is_none());
        assert!(!t.is_empty());
    }

    #[test]
    fn test_iter_mut() {
        let mut t = TestCollection::new();
        assert!(t.insert(&KEY_A, 15).is_none());
        assert!(t.insert(&KEY_B, -5).is_none());
        assert!(t.insert(&KEY_C, -10).is_none());
        for i in t.iter_mut() {
            *i *= 2;
        }
        assert_eq!(*t.get(&KEY_A).unwrap(), 30);
        assert_eq!(*t.get(&KEY_B).unwrap(), -10);
        assert_eq!(*t.get(&KEY_C).unwrap(), -20);
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

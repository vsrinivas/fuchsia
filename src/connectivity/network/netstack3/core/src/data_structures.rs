// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Common data structures.

pub use id_map::Entry;
pub(crate) use id_map::IdMap;
pub use id_map_collection::{IdMapCollection, IdMapCollectionKey};

/// Identifier map data structure.
///
/// Defines the [`IdMap`] data structure: A generic container mapped keyed
/// by an internally managed pool of identifiers kept densely packed.
pub(crate) mod id_map {

    type Key = usize;

    /// A generic container for `T` keyed by densily packed integers.
    ///
    /// `IdMap` is a generic container keyed by `usize` that manages its own
    /// key pool. `IdMap` reuses keys that are free to keep its key pool as
    /// dense as possible.
    ///
    /// The main guarantee provided by `IdMap` is that all `get` operations are
    /// provided in O(1) without the need to hash the keys.
    ///
    /// The only operations of `IdMap` that are used in the hot path are the `get`
    /// operations. `push` is used
    pub(crate) struct IdMap<T> {
        data: Vec<Option<T>>,
    }

    impl<T> IdMap<T> {
        /// Creates a new empty [`IdMap`].
        pub(crate) fn new() -> Self {
            Self { data: Vec::new() }
        }

        /// Returns `true` if there are no items in [`IdMap`].
        pub(crate) fn is_empty(&self) -> bool {
            !self.data.iter().any(|f| f.is_some())
        }

        /// Returns a reference to the item indexed by `key`, or `None` if
        /// the `key` doesn't exist.
        pub(crate) fn get(&self, key: Key) -> Option<&T> {
            self.data.get(key).and_then(|v| v.as_ref())
        }

        /// Returns a mutable reference to the item indexed by `key`, or `None`
        /// if the `key` doesn't exist.
        pub(crate) fn get_mut(&mut self, key: Key) -> Option<&mut T> {
            self.data.get_mut(key).and_then(|v| v.as_mut())
        }

        /// Removes item indexed by `key` from the container.
        ///
        /// Returns the removed item if it exists, or `None` otherwise.
        ///
        /// Note: the worst case complexity of `remove` is O(key) if the
        /// backing data structure of the [`IdMap`] is too sparse.
        pub(crate) fn remove(&mut self, key: Key) -> Option<T> {
            let r = self.data.get_mut(key).and_then(|v| v.take());
            if r.is_some() {
                self.compress();
            }
            r
        }

        /// Inserts `item` at `key`.
        ///
        /// If the [`IdMap`] already contained an item indexed by `key`,
        /// `insert` returns it, or `None` otherwise.
        ///
        /// Note: The worst case complexity of `insert` is O(key) if `key` is
        /// larger than the number of items currently held by the [`IdMap`].
        pub(crate) fn insert(&mut self, key: Key, item: T) -> Option<T> {
            if key < self.data.len() {
                self.data[key].replace(item)
            } else {
                self.data.resize_with(key, Option::default);
                self.data.push(Some(item));
                None
            }
        }

        /// Inserts `item` into the [`IdMap`].
        ///
        /// `push` inserts a new `item` into the [`IdMap`] and returns the
        /// key value allocated for `item`. `push` will allocate *any* key that
        /// is currently free in the internal structure, so it may return a key
        /// that was used previously but has since been removed.
        ///
        /// Note: The worst case complexity of `push` is O(n) where n is the
        /// number of items held by the [`IdMap`]. This can happen if the
        /// internal structure gets fragmented.
        // NOTE(brunodalbo) We could make push be O(1) if we kept a pool of
        //  available IDs as a separate vec inside IdMap. We expect that n will
        //  not get large enough for it to make too much of a difference here,
        //  but we may want to revisit this decision if it turns out that IdMap
        //  gets used for larger n's and fragmentation is concentrated at the
        //  end of the internal vec.
        pub(crate) fn push(&mut self, item: T) -> Key {
            match self.data.iter().enumerate().find_map(
                |(k, v)| {
                    if v.is_none() {
                        Some(k)
                    } else {
                        None
                    }
                },
            ) {
                Some(k) => {
                    self.data[k].replace(item);
                    k
                }
                None => {
                    let k = self.data.len();
                    self.data.push(Some(item));
                    k
                }
            }
        }

        /// Compresses the tail of the internal `Vec`.
        ///
        /// `compress` removes all trailing elements in `data` that are `None`,
        /// reducing the internal `Vec`.
        fn compress(&mut self) {
            if let Some(idx) = self.data.iter().enumerate().rev().find_map(|(k, v)| {
                if v.is_some() {
                    Some(k)
                } else {
                    None
                }
            }) {
                self.data.truncate(idx + 1);
            } else {
                self.data.clear();
            }
        }

        /// Creates an iterator over the containing items and their associated
        /// keys.
        pub(crate) fn iter(&self) -> impl Iterator<Item = (Key, &T)> {
            self.data.iter().enumerate().filter_map(|(k, v)| v.as_ref().map(|t| (k, t)))
        }

        /// Creates a mutable iterator over the containing items and their
        /// associated keys.
        pub(crate) fn iter_mut(&mut self) -> impl Iterator<Item = (Key, &mut T)> {
            self.data.iter_mut().enumerate().filter_map(|(k, v)| v.as_mut().map(|t| (k, t)))
        }

        /// Gets the given key's corresponding entry in the map for in-place
        /// manipulation.
        pub(crate) fn entry(&mut self, key: usize) -> Entry<'_, usize, T> {
            if key < self.data.len() {
                let slot = &mut self.data[key];
                if slot.is_some() {
                    Entry::Occupied(OccupiedEntry { key, value: slot })
                } else {
                    Entry::Vacant(VacantEntry { key, slot: LazyEntry::Allocated(slot) })
                }
            } else {
                Entry::Vacant(VacantEntry { key, slot: LazyEntry::Lazy(self) })
            }
        }
    }

    impl<T> Default for IdMap<T> {
        fn default() -> Self {
            Self::new()
        }
    }

    pub trait EntryKey {
        fn get_key_index(&self) -> usize;
    }

    impl EntryKey for usize {
        fn get_key_index(&self) -> usize {
            *self
        }
    }

    enum LazyEntry<'a, T> {
        Allocated(&'a mut Option<T>),
        Lazy(&'a mut IdMap<T>),
    }

    /// A view into a vacant entry in a map. It is part of the [`Entry`] enum.
    pub struct VacantEntry<'a, K, T> {
        key: K,
        slot: LazyEntry<'a, T>,
    }

    impl<'a, K, T> VacantEntry<'a, K, T> {
        /// Sets the value of the entry with the VacantEntry's key, and returns
        /// a mutable reference to it.
        pub fn insert(self, value: T) -> &'a mut T
        where
            K: EntryKey,
        {
            match self.slot {
                LazyEntry::Allocated(slot) => {
                    assert!(slot.replace(value).is_none());
                    slot.as_mut().unwrap()
                }
                LazyEntry::Lazy(id_map) => {
                    assert!(id_map.insert(self.key.get_key_index(), value).is_none());
                    id_map.data[self.key.get_key_index()].as_mut().unwrap()
                }
            }
        }

        /// Gets a reference to the key that would be used when inserting a
        /// value through the `VacantEntry`.
        pub fn key(&self) -> &K {
            &self.key
        }

        /// Take ownership of the key.
        pub fn into_key(self) -> K {
            self.key
        }

        /// Changes the key type of this `VacantEntry` to another key `X` that
        /// still maps to the same index in an `IdMap`.
        ///
        /// # Panics
        ///
        /// Panics if the resulting mapped key from `f` does not return the
        /// same value for [`EntryKey::get_key_index`] as the old key did.
        pub(crate) fn map_key<X, F>(self, f: F) -> VacantEntry<'a, X, T>
        where
            K: EntryKey,
            X: EntryKey,
            F: FnOnce(K) -> X,
        {
            let idx = self.key.get_key_index();
            let key = f(self.key);
            assert_eq!(idx, key.get_key_index());
            VacantEntry { key, slot: self.slot }
        }
    }

    /// A view into an occupied entry in a map. It is part of the
    /// [`Entry`] enum.
    pub struct OccupiedEntry<'a, K, T> {
        key: K,
        value: &'a mut Option<T>,
    }

    impl<'a, K, T> OccupiedEntry<'a, K, T> {
        /// Gets a reference to the key in the entry.
        pub fn key(&self) -> &K {
            &self.key
        }

        /// Gets a reference to the value in the entry.
        pub fn get(&self) -> &T {
            // we can unwrap because value is always Some for OccupiedEntry
            self.value.as_ref().unwrap()
        }

        /// Gets a mutable reference to the value in the entry.
        ///
        /// If you need a reference to the `OccupiedEntry` which may outlive the
        /// destruction of the entry value, see [`OccupiedEntry::into_mut`].
        pub fn get_mut(&mut self) -> &mut T {
            // we can unwrap because value is always Some for OccupiedEntry
            self.value.as_mut().unwrap()
        }

        /// Converts the `OccupiedEntry` into a mutable reference to the value
        /// in the entry with a lifetime bound to the map itself.
        ///
        /// If you need multiple references to the `OccupiedEntry`, see
        /// [`OccupiedEntry::get_mut`].
        pub fn into_mut(self) -> &'a mut T {
            // we can unwrap because value is always Some for OccupiedEntry
            self.value.as_mut().unwrap()
        }

        /// Sets the value of the entry, and returns the entry's old value.
        pub fn insert(&mut self, value: T) -> T {
            // we can unwrap because value is always Some for OccupiedEntry
            self.value.replace(value).unwrap()
        }

        /// Takes the value out of the entry, and returns it.
        pub fn remove(self) -> T {
            // we can unwrap because value is always Some for OccupiedEntry
            self.value.take().unwrap()
        }

        /// Changes the key type of this `OccupiedEntry` to another key `X` that
        /// still maps to the same index in an `IdMap`.
        ///
        /// # Panics
        ///
        /// Panics if the resulting mapped key from `f` does not return the
        /// same value for [`EntryKey::get_key_index`] as the old key did.
        pub(crate) fn map_key<X, F>(self, f: F) -> OccupiedEntry<'a, X, T>
        where
            K: EntryKey,
            X: EntryKey,
            F: FnOnce(K) -> X,
        {
            let idx = self.key.get_key_index();
            let key = f(self.key);
            assert_eq!(idx, key.get_key_index());
            OccupiedEntry { key, value: self.value }
        }
    }

    /// A view into an in-place entry in a map that can be vacant or occupied.
    pub enum Entry<'a, K, T> {
        Vacant(VacantEntry<'a, K, T>),
        Occupied(OccupiedEntry<'a, K, T>),
    }

    impl<'a, K, T> Entry<'a, K, T> {
        /// Returns a reference to this entry's key.
        pub fn key(&self) -> &K {
            match self {
                Entry::Vacant(e) => e.key(),
                Entry::Occupied(e) => e.key(),
            }
        }

        /// Ensures a value is in the entry by inserting `default` if empty,
        /// and returns a mutable reference to the value in the entry.
        pub fn or_insert(self, default: T) -> &'a mut T
        where
            K: EntryKey,
        {
            match self {
                Entry::Vacant(e) => e.insert(default),
                Entry::Occupied(e) => e.into_mut(),
            }
        }

        /// Ensures a value is in the entry by inserting the result of the
        /// function `f` if empty, and returns a mutable reference to the value
        /// in the entry.
        pub fn or_insert_with<F: FnOnce() -> T>(self, f: F) -> &'a mut T
        where
            K: EntryKey,
        {
            match self {
                Entry::Vacant(e) => e.insert(f()),
                Entry::Occupied(e) => e.into_mut(),
            }
        }

        /// Ensures a value is in the entry by inserting the default value if
        /// empty, and returns a mutable reference to the value in the entry.
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
                Entry::Vacant(e) => Entry::Vacant(e),
                Entry::Occupied(mut e) => {
                    f(e.get_mut());
                    Entry::Occupied(e)
                }
            }
        }

        /// Changes the key type of this `Entry` to another key `X` that still
        /// maps to the same index in an `IdMap`.
        ///
        /// # Panics
        ///
        /// Panics if the resulting mapped key from `f` does not return the
        /// same value for [`EntryKey::get_key_index`] as the old key did.
        pub(crate) fn map_key<X, F>(self, f: F) -> Entry<'a, X, T>
        where
            K: EntryKey,
            X: EntryKey,
            F: FnOnce(K) -> X,
        {
            match self {
                Entry::Vacant(e) => Entry::Vacant(e.map_key(f)),
                Entry::Occupied(e) => Entry::Occupied(e.map_key(f)),
            }
        }
    }

    #[cfg(test)]
    mod tests {
        use super::{Entry, IdMap};

        #[test]
        fn test_push() {
            let mut map = IdMap::new();
            map.data = vec![None, Some(2)];
            assert_eq!(map.push(1), 0);
            assert_eq!(map.data, vec![Some(1), Some(2)]);
            assert_eq!(map.push(3), 2);
            assert_eq!(map.data, vec![Some(1), Some(2), Some(3)]);
        }

        #[test]
        fn test_get() {
            let mut map = IdMap::new();
            map.data = vec![Some(1), None, Some(3)];
            assert_eq!(*map.get(0).unwrap(), 1);
            assert!(map.get(1).is_none());
            assert_eq!(*map.get(2).unwrap(), 3);
            assert!(map.get(3).is_none());
        }

        #[test]
        fn test_get_mut() {
            let mut map = IdMap::new();
            map.data = vec![Some(1), None, Some(3)];
            *map.get_mut(2).unwrap() = 10;
            assert_eq!(*map.get(0).unwrap(), 1);
            assert_eq!(*map.get(2).unwrap(), 10);

            assert!(map.get_mut(1).is_none());
            assert!(map.get_mut(3).is_none());
        }

        #[test]
        fn test_is_empty() {
            assert!(IdMap::<i32>::new().is_empty())
        }

        #[test]
        fn test_remove() {
            let mut map = IdMap::new();
            map.data = vec![Some(1), Some(2), Some(3)];
            assert_eq!(map.remove(1).unwrap(), 2);
            assert!(map.remove(1).is_none());
            assert_eq!(map.data, vec![Some(1), None, Some(3)]);
        }

        #[test]
        fn test_remove_compress() {
            let mut map = IdMap::new();
            map.data = vec![Some(1), None, Some(3)];
            assert_eq!(map.remove(2).unwrap(), 3);
            assert_eq!(map.data, vec![Some(1)]);
            assert_eq!(map.remove(0).unwrap(), 1);
            assert!(map.data.is_empty());
        }

        #[test]
        fn test_insert() {
            let mut map = IdMap::new();
            assert!(map.insert(1, 2).is_none());
            assert_eq!(map.data, vec![None, Some(2)]);
            assert!(map.insert(3, 4).is_none());
            assert_eq!(map.data, vec![None, Some(2), None, Some(4)]);
            assert!(map.insert(0, 1).is_none());
            assert_eq!(map.data, vec![Some(1), Some(2), None, Some(4)]);
            assert_eq!(map.insert(3, 5).unwrap(), 4);
            assert_eq!(map.data, vec![Some(1), Some(2), None, Some(5)]);
        }

        #[test]
        fn test_iter() {
            let mut map = IdMap::new();
            map.data = vec![None, Some(0), None, Some(1), None, None, Some(2)];
            let mut c = 0;
            for (i, (k, v)) in map.iter().enumerate() {
                assert_eq!(i, *v as usize);
                assert_eq!(map.get(k).unwrap(), v);
                c += 1;
            }
            assert_eq!(c, 3);
        }

        #[test]
        fn test_iter_mut() {
            let mut map = IdMap::new();
            map.data = vec![None, Some(0), None, Some(1), None, None, Some(2)];
            for (k, v) in map.iter_mut() {
                *v += k as u32;
            }
            assert_eq!(map.data, vec![None, Some(1), None, Some(4), None, None, Some(8)]);
        }

        #[test]
        fn test_entry() {
            let mut map = IdMap::new();
            assert_eq!(*map.entry(1).or_insert(2), 2);
            assert_eq!(map.data, vec![None, Some(2)]);
            assert_eq!(
                *map.entry(1)
                    .and_modify(|v| {
                        *v = 10;
                    })
                    .or_insert(5),
                10
            );
            assert_eq!(map.data, vec![None, Some(10)]);
            assert_eq!(
                *map.entry(2)
                    .and_modify(|v| {
                        *v = 10;
                    })
                    .or_insert(5),
                5
            );
            assert_eq!(map.data, vec![None, Some(10), Some(5)]);
            assert_eq!(*map.entry(4).or_default(), 0);
            assert_eq!(map.data, vec![None, Some(10), Some(5), None, Some(0)]);
            assert_eq!(*map.entry(3).or_insert_with(|| 7), 7);
            assert_eq!(map.data, vec![None, Some(10), Some(5), Some(7), Some(0)]);
            assert_eq!(*map.entry(0).or_insert(1), 1);
            assert_eq!(map.data, vec![Some(1), Some(10), Some(5), Some(7), Some(0)]);

            match map.entry(0) {
                Entry::Occupied(mut e) => {
                    assert_eq!(*e.key(), 0);
                    assert_eq!(*e.get(), 1);
                    *e.get_mut() = 2;
                    assert_eq!(*e.get(), 2);
                    assert_eq!(e.remove(), 2);
                }
                _ => panic!("Wrong entry type, should be occupied"),
            }
            assert_eq!(map.data, vec![None, Some(10), Some(5), Some(7), Some(0)]);

            match map.entry(0) {
                Entry::Vacant(mut e) => {
                    assert_eq!(*e.key(), 0);
                    assert_eq!(*e.insert(4), 4);
                }
                _ => panic!("Wrong entry type, should be vacant"),
            }
            assert_eq!(map.data, vec![Some(4), Some(10), Some(5), Some(7), Some(0)]);
        }

    }
}

/// Identifier map collection data structure.
///
/// Defines [`IdMapCollection`], which is a generic map collection that can be
/// keyed on [`IdMapCollectionKey`], which is a two-level key structure.
///
/// Used to provide collections keyed on [`crate::DeviceId`] that match hot path
/// performance requirements.
pub mod id_map_collection {
    use super::id_map::{Entry, EntryKey};
    use super::IdMap;

    /// A key that can index items in [`IdMapCollection`].
    ///
    /// An `IdMapCollectionKey` is a key with two levels: `variant` and `id`.
    /// The number of `variant`s must be fixed and known at compile time, and is
    /// typically mapped to a number of `enum` variants (nested or not).
    pub trait IdMapCollectionKey {
        /// The number of variants this key supports.
        const VARIANT_COUNT: usize;

        /// Get the variant index for this key.
        ///
        /// # Panics
        ///
        /// Callers may assume that `get_variant` returns a value in the range
        /// `[0, VARIANT_COUNT)`, and may panic if that assumption is violated.
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
    /// `IdMapCollection` provides the same performance guarantees as [`IdMap`],
    /// but provides a two-level keying scheme that matches the pattern used
    /// in [`crate::DeviceId`].
    pub struct IdMapCollection<K: IdMapCollectionKey, T> {
        // TODO(brunodalbo): we define a vector container here because we can't
        // just define a fixed array length based on an associated const in
        // IdMapCollectionKey. When rust issue #43408 gets resolved we can
        // switch this to use the associated const and just have a fixed length
        // array.
        data: Vec<IdMap<T>>,
        _marker: std::marker::PhantomData<K>,
    }

    impl<K: IdMapCollectionKey, T> IdMapCollection<K, T> {
        /// Creates a new empty `IdMapCollection`.
        pub fn new() -> Self {
            let mut data = Vec::new();
            data.resize_with(K::VARIANT_COUNT, IdMap::default);
            Self { data, _marker: std::marker::PhantomData }
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

        /// Returns a reference to the item indexed by `key`, or `None` if
        /// the `key` doesn't exist.
        pub fn get(&self, key: &K) -> Option<&T> {
            self.get_map(key).get(key.get_id())
        }

        /// Returns a mutable reference to the item indexed by `key`, or `None`
        /// if the `key` doesn't exist.
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
        /// If the [`IdMapCollection`] already contained an item indexed by
        /// `key`, `insert` returns it, or `None` otherwise.
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

}

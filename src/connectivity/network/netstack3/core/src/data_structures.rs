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
    /// [`IdMap`]s use `usize`s for keys.
    pub(crate) type Key = usize;

    /// IdMapEntry where all free blocks are linked together.
    #[derive(PartialEq, Eq, Debug)]
    #[cfg_attr(test, derive(Clone))]
    enum IdMapEntry<T> {
        /// The Entry should either be allocated and contains a value...
        Allocated(T),
        /// Or it is not currently used and should be part of a freelist.
        Free(FreeListLink),
    }

    /// The link of the doubly-linked free-list.
    #[derive(PartialEq, Eq, Debug, Clone, Copy)]
    struct FreeListLink {
        /// The index of the previous free block in the list.
        prev: Option<usize>,
        /// The index of the next free block in the list.
        next: Option<usize>,
    }

    impl Default for FreeListLink {
        /// By default, an entry is not linked into the list.
        fn default() -> Self {
            Self { prev: None, next: None }
        }
    }

    /// Stores positions of the head and tail of the free-list
    /// linked by `FreeListLink`.
    #[derive(PartialEq, Eq, Debug, Clone, Copy)]
    struct FreeList {
        /// The index of the first free block.
        head: usize,
        /// The index of the last free block.
        tail: usize,
    }

    impl FreeList {
        /// Construct a freelist with only one element.
        fn singleton(elem: usize) -> FreeList {
            FreeList { head: elem, tail: elem }
        }
    }

    // The following is to mimic the `Option<T>` API.
    impl<T> IdMapEntry<T> {
        /// If the entry is allocated, return the reference,
        /// otherwise, return `None`.
        fn as_ref(&self) -> Option<&T> {
            match self {
                IdMapEntry::Allocated(e) => Some(e),
                IdMapEntry::Free(_) => None,
            }
        }

        /// If the entry is allocated, return the mutable reference,
        /// otherwise, return `None`.
        fn as_mut(&mut self) -> Option<&mut T> {
            match self {
                IdMapEntry::Allocated(e) => Some(e),
                IdMapEntry::Free(_) => None,
            }
        }

        /// Convert the entry into an option.
        fn into_option(self) -> Option<T> {
            match self {
                IdMapEntry::Allocated(e) => Some(e),
                IdMapEntry::Free(_) => None,
            }
        }

        /// Return whether the entry is allocated.
        fn is_allocated(&self) -> bool {
            match self {
                IdMapEntry::Allocated(_) => true,
                IdMapEntry::Free(_) => false,
            }
        }

        /// Return whether the entry is free.
        fn is_free(&self) -> bool {
            !self.is_allocated()
        }

        /// Return the old entry and replace it with `new`.
        fn replace(&mut self, new: T) -> IdMapEntry<T> {
            std::mem::replace(self, IdMapEntry::Allocated(new))
        }

        /// Take the old allocated entry and replace it with a free entry.
        ///
        /// The new free entry's `prev` pointer will be `None`, so the new
        /// free entry is intended to be inserted at the front of the freelist.
        /// If the old entry is already free, this is a no-op.
        fn take_allocated(&mut self, next: Option<usize>) -> Option<T> {
            if self.is_allocated() {
                std::mem::replace(self, IdMapEntry::Free(FreeListLink { prev: None, next }))
                    .into_option()
            } else {
                // If it is currently free, we don't want to unlink
                // the entry and link it back at the head again.
                None
            }
        }

        /// Return the stored value, panic if not allocated.
        fn unwrap(self) -> T {
            self.into_option().unwrap()
        }

        /// Return a mutable reference to the freelist link, panic if allocated.
        fn freelist_link_mut(&mut self) -> &mut FreeListLink {
            match self {
                IdMapEntry::Free(link) => link,
                _ => unreachable!("An allocated block should never be linked into the free list"),
            }
        }

        /// Return a reference to the freelist link, panic if allocated.
        fn freelist_link(&self) -> &FreeListLink {
            match self {
                IdMapEntry::Free(link) => link,
                _ => unreachable!("An allocated block should never be linked into the free list"),
            }
        }
    }

    /// A generic container for `T` keyed by densily packed integers.
    ///
    /// `IdMap` is a generic container keyed by `usize` that manages its own
    /// key pool. `IdMap` reuses keys that are free to keep its key pool as
    /// dense as possible.
    ///
    /// The main guarantee provided by `IdMap` is that all `get` operations are
    /// provided in O(1) without the need to hash the keys. The only operations
    /// of `IdMap` that are used in the hot path are the `get` operations.
    ///
    /// All operations that mutate the `IdMap` are O(log(n)) average.
    ///
    /// `push` will grab the lowest free `id` and assign it to the given value,
    /// returning the assigned `id`. `insert` can be used for assigning a
    /// specific `id` to an object, and returns the previous object at that `id`
    /// if any.
    #[cfg_attr(test, derive(Clone))]
    pub(crate) struct IdMap<T> {
        freelist: Option<FreeList>,
        data: Vec<IdMapEntry<T>>,
    }

    impl<T> IdMap<T> {
        /// Creates a new empty [`IdMap`].
        pub(crate) fn new() -> Self {
            Self { freelist: None, data: Vec::new() }
        }

        /// Returns `true` if there are no items in [`IdMap`].
        pub(crate) fn is_empty(&self) -> bool {
            // Because of `compress`, our map is empty if
            // and only if the underlying vector is empty.
            // If the underlying vector is not empty but our
            // map is empty, it must be the case where the
            // underlying vector contains nothing but free entries,
            // and all these entries should be reclaimed when
            // the last allocated entry is removed.
            self.data.is_empty()
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
            let r = self.remove_inner(key);
            if r.is_some() {
                self.compress();
            }
            r
        }

        fn remove_inner(&mut self, key: Key) -> Option<T> {
            let old_head = self.freelist.map(|l| l.head);
            let r = self.data.get_mut(key).and_then(|v| v.take_allocated(old_head));
            if r.is_some() {
                // If it was allocated, we add the removed entry to the head of
                // the free-list.
                match self.freelist.as_mut() {
                    Some(FreeList { head, .. }) => {
                        self.data[*head].freelist_link_mut().prev = Some(key);
                        *head = key;
                    }
                    None => self.freelist = Some(FreeList::singleton(key)),
                }
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
                if self.data[key].is_free() {
                    self.freelist_unlink(key);
                }
                self.data[key].replace(item).into_option()
            } else {
                let start_len = self.data.len();
                // Fill the gap `start_len .. key` with free entries.
                // Currently, the free entries introduced by `insert`
                // is linked at the end of the free list so that hopefully
                // these free entries near the end will get less likely to
                // be allocated than those near the beginning, this may help
                // reduce the memory footprint because we have increased the
                // chance for the underlying vector to be compressed.
                // TODO: explore whether we can reorder the list on the fly
                // to further increase the chance for compressing.
                for idx in start_len..key {
                    // These new free entries will be linked to each other, except:
                    // - the first entry's prev should point to the old tail.
                    // - the last entry's next should be None.
                    self.data.push(IdMapEntry::Free(FreeListLink {
                        prev: if idx == start_len {
                            self.freelist.map(|l| l.tail)
                        } else {
                            Some(idx - 1)
                        },
                        next: if idx == key - 1 { None } else { Some(idx + 1) },
                    }));
                }
                // If `key > start_len`, we have inserted at least one free entry,
                // so we have to update our freelist.
                if key > start_len {
                    let new_tail = key - 1;
                    match self.freelist.as_mut() {
                        Some(FreeList { tail, .. }) => {
                            self.data[*tail].freelist_link_mut().next = Some(start_len);
                            *tail = new_tail;
                        }
                        None => {
                            self.freelist = Some(FreeList::singleton(new_tail));
                        }
                    }
                }
                // And finally we insert our item into the map.
                self.data.push(IdMapEntry::Allocated(item));
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
        pub(crate) fn push(&mut self, item: T) -> Key {
            if let Some(FreeList { head, .. }) = self.freelist.as_mut() {
                let ret = *head;
                let old =
                    std::mem::replace(self.data.get_mut(ret).unwrap(), IdMapEntry::Allocated(item));
                // Update the head of the freelist.
                match old.freelist_link().next {
                    Some(new_head) => *head = new_head,
                    None => self.freelist = None,
                }
                ret
            } else {
                // If we run out of freelist, we simply push a new entry
                // into the underlying vector.
                let key = self.data.len();
                self.data.push(IdMapEntry::Allocated(item));
                key
            }
        }

        /// Compresses the tail of the internal `Vec`.
        ///
        /// `compress` removes all trailing elements in `data` that are `None`,
        /// shrinking the internal `Vec`.
        fn compress(&mut self) {
            // First, find the last non-free entry.
            if let Some(idx) = self.data.iter().enumerate().rev().find_map(|(k, v)| {
                if v.is_allocated() {
                    Some(k)
                } else {
                    None
                }
            }) {
                // Remove all the trailing free entries.
                for i in idx + 1..self.data.len() {
                    self.freelist_unlink(i);
                }
                self.data.truncate(idx + 1);
            } else {
                // There is nothing left in the vector.
                self.data.clear();
                self.freelist = None;
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
            if key < self.data.len() && self.data[key].is_allocated() {
                Entry::Occupied(OccupiedEntry { key, id_map: self })
            } else {
                Entry::Vacant(VacantEntry { key, id_map: self })
            }
        }

        /// Update the elements of the map in-place, retaining only the elements
        /// for which `f` returns true.
        ///
        /// `update_return` invokes `f` on each element of the map, and removes
        /// from the map those elements for which `f` returns false. The return
        /// value is an iterator over the removed elements.
        ///
        /// WARNING: The mutation will only occur when the returned iterator is
        /// executed. Simply calling `update_retain` and then discarding the
        /// return value will do nothing!
        pub(crate) fn update_retain<'a, F: 'a + FnMut(&mut T) -> bool>(
            &'a mut self,
            mut f: F,
        ) -> impl 'a + Iterator<Item = (Key, T)>
        where
            T: 'a,
        {
            (0..self.data.len()).filter_map(move |k| {
                let ret = if let IdMapEntry::Allocated(t) = self.data.get_mut(k).unwrap() {
                    if !f(t) {
                        // Note the use of `remove_inner` rather than `remove`
                        // here. `remove` calls `self.compress()`, which is an
                        // O(n) operation. Instead, we postpone that operation
                        // and perform it once during the last iteration so that
                        // the overall complexity is O(n) rather than O(n^2).
                        //
                        // TODO(joshlf): Could we improve the performance here
                        // by doing something smarter than just calling
                        // `remove_inner`? E.g., perhaps we could build up a
                        // separate linked list that we only insert into the
                        // existing free list once at the end? That there is a
                        // performance issue here at all is pure speculation,
                        // and will need to be measured to determine whether
                        // such an optimization is worth it.
                        Some((k, self.remove_inner(k).unwrap()))
                    } else {
                        None
                    }
                } else {
                    None
                };

                // Compress once at the very end (see the comment above about
                // `remove_inner`).
                if k == self.data.len() - 1 {
                    self.compress();
                }

                ret
            })
        }

        /// Unlink an entry from the freelist.
        ///
        /// We want to do so whenever a freed block turns allocated.
        fn freelist_unlink(&mut self, idx: usize) {
            let FreeListLink { prev, next } = self.data[idx].freelist_link().clone();
            match (prev, next) {
                (Some(prev), Some(next)) => {
                    // A normal node in the middle of a list.
                    self.data[prev].freelist_link_mut().next = Some(next);
                    self.data[next].freelist_link_mut().prev = Some(prev);
                }
                (Some(prev), None) => {
                    // The node at the tail.
                    self.data[prev].freelist_link_mut().next = next;
                    self.freelist.as_mut().unwrap().tail = prev;
                }
                (None, Some(next)) => {
                    // The node at the head.
                    self.data[next].freelist_link_mut().prev = prev;
                    self.freelist.as_mut().unwrap().head = next;
                }
                (None, None) => {
                    // We are the last node.
                    self.freelist = None;
                }
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

    /// A view into a vacant entry in a map. It is part of the [`Entry`] enum.
    pub struct VacantEntry<'a, K, T> {
        key: K,
        id_map: &'a mut IdMap<T>,
    }

    impl<'a, K, T> VacantEntry<'a, K, T> {
        /// Sets the value of the entry with the VacantEntry's key, and returns
        /// a mutable reference to it.
        pub fn insert(self, value: T) -> &'a mut T
        where
            K: EntryKey,
        {
            assert!(self.id_map.insert(self.key.get_key_index(), value).is_none());
            self.id_map.data[self.key.get_key_index()].as_mut().unwrap()
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
            VacantEntry { key, id_map: self.id_map }
        }
    }

    /// A view into an occupied entry in a map. It is part of the
    /// [`Entry`] enum.
    pub struct OccupiedEntry<'a, K, T> {
        key: K,
        id_map: &'a mut IdMap<T>,
    }

    impl<'a, K: EntryKey, T> OccupiedEntry<'a, K, T> {
        /// Gets a reference to the key in the entry.
        pub fn key(&self) -> &K {
            &self.key
        }

        /// Gets a reference to the value in the entry.
        pub fn get(&self) -> &T {
            // we can unwrap because value is always Some for OccupiedEntry
            self.id_map.get(self.key.get_key_index()).unwrap()
        }

        /// Gets a mutable reference to the value in the entry.
        ///
        /// If you need a reference to the `OccupiedEntry` which may outlive the
        /// destruction of the entry value, see [`OccupiedEntry::into_mut`].
        pub fn get_mut(&mut self) -> &mut T {
            // we can unwrap because value is always Some for OccupiedEntry
            self.id_map.get_mut(self.key.get_key_index()).unwrap()
        }

        /// Converts the `OccupiedEntry` into a mutable reference to the value
        /// in the entry with a lifetime bound to the map itself.
        ///
        /// If you need multiple references to the `OccupiedEntry`, see
        /// [`OccupiedEntry::get_mut`].
        pub fn into_mut(self) -> &'a mut T {
            // we can unwrap because value is always Some for OccupiedEntry
            self.id_map.get_mut(self.key.get_key_index()).unwrap()
        }

        /// Sets the value of the entry, and returns the entry's old value.
        pub fn insert(&mut self, value: T) -> T {
            // we can unwrap because value is always Some for OccupiedEntry
            self.id_map.insert(self.key.get_key_index(), value).unwrap()
        }

        /// Takes the value out of the entry, and returns it.
        pub fn remove(self) -> T {
            // we can unwrap because value is always Some for OccupiedEntry
            self.id_map.remove(self.key.get_key_index()).unwrap()
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
            OccupiedEntry { key, id_map: self.id_map }
        }
    }

    /// A view into an in-place entry in a map that can be vacant or occupied.
    pub enum Entry<'a, K, T> {
        Vacant(VacantEntry<'a, K, T>),
        Occupied(OccupiedEntry<'a, K, T>),
    }

    impl<'a, K: EntryKey, T> Entry<'a, K, T> {
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
        use super::IdMapEntry::{self, Allocated, Free};
        use super::{Entry, IdMap};
        use super::{FreeList, FreeListLink};

        // Smart constructors
        fn free<T>(prev: usize, next: usize) -> IdMapEntry<T> {
            Free(FreeListLink { prev: Some(prev), next: Some(next) })
        }

        fn free_head<T>(next: usize) -> IdMapEntry<T> {
            Free(FreeListLink { prev: None, next: Some(next) })
        }

        fn free_tail<T>(prev: usize) -> IdMapEntry<T> {
            Free(FreeListLink { prev: Some(prev), next: None })
        }

        fn free_none<T>() -> IdMapEntry<T> {
            Free(FreeListLink::default())
        }

        #[test]
        fn test_push() {
            let mut map = IdMap::new();
            map.insert(1, 2);
            assert_eq!(map.data, vec![free_none(), Allocated(2)]);
            assert_eq!(map.freelist, Some(FreeList::singleton(0)));
            assert_eq!(map.push(1), 0);
            assert_eq!(map.data, vec![Allocated(1), Allocated(2)]);
            assert_eq!(map.freelist, None);
            assert_eq!(map.push(3), 2);
            assert_eq!(map.data, vec![Allocated(1), Allocated(2), Allocated(3)]);
            assert_eq!(map.freelist, None);
        }

        #[test]
        fn test_get() {
            let mut map = IdMap::new();
            map.push(1);
            map.insert(2, 3);
            assert_eq!(map.data, vec![Allocated(1), free_none(), Allocated(3)]);
            assert_eq!(map.freelist, Some(FreeList::singleton(1)));
            assert_eq!(*map.get(0).unwrap(), 1);
            assert!(map.get(1).is_none());
            assert_eq!(*map.get(2).unwrap(), 3);
            assert!(map.get(3).is_none());
        }

        #[test]
        fn test_get_mut() {
            let mut map = IdMap::new();
            map.push(1);
            map.insert(2, 3);
            assert_eq!(map.data, vec![Allocated(1), free_none(), Allocated(3)]);
            assert_eq!(map.freelist, Some(FreeList::singleton(1)));
            *map.get_mut(2).unwrap() = 10;
            assert_eq!(*map.get(0).unwrap(), 1);
            assert_eq!(*map.get(2).unwrap(), 10);

            assert!(map.get_mut(1).is_none());
            assert!(map.get_mut(3).is_none());
        }

        #[test]
        fn test_is_empty() {
            let mut map = IdMap::<i32>::new();
            assert!(map.is_empty());
            map.push(1);
            assert!(!map.is_empty());
        }

        #[test]
        fn test_remove() {
            let mut map = IdMap::new();
            map.push(1);
            map.push(2);
            map.push(3);
            assert_eq!(map.data, vec![Allocated(1), Allocated(2), Allocated(3)]);
            assert_eq!(map.freelist, None);
            assert_eq!(map.remove(1).unwrap(), 2);

            assert!(map.remove(1).is_none());
            assert_eq!(map.data, vec![Allocated(1), free_none(), Allocated(3)]);
            assert_eq!(map.freelist, Some(FreeList::singleton(1)));
        }

        #[test]
        fn test_remove_compress() {
            let mut map = IdMap::new();
            map.insert(0, 1);
            map.insert(2, 3);
            assert_eq!(map.data, vec![Allocated(1), free_none(), Allocated(3)]);
            assert_eq!(map.freelist, Some(FreeList::singleton(1)));
            assert_eq!(map.remove(2).unwrap(), 3);
            assert_eq!(map.data, vec![Allocated(1)]);
            assert_eq!(map.freelist, None);
            assert_eq!(map.remove(0).unwrap(), 1);
            assert!(map.data.is_empty());
        }

        #[test]
        fn test_insert() {
            let mut map = IdMap::new();
            assert!(map.insert(1, 2).is_none());
            assert_eq!(map.data, vec![free_none(), Allocated(2)]);
            assert_eq!(map.freelist, Some(FreeList::singleton(0)));
            assert!(map.insert(3, 4).is_none());
            assert_eq!(map.data, vec![free_head(2), Allocated(2), free_tail(0), Allocated(4)]);
            assert_eq!(map.freelist, Some(FreeList { head: 0, tail: 2 }));
            assert!(map.insert(0, 1).is_none());
            assert_eq!(map.data, vec![Allocated(1), Allocated(2), free_none(), Allocated(4)]);
            assert_eq!(map.freelist, Some(FreeList::singleton(2)));
            assert_eq!(map.insert(3, 5).unwrap(), 4);
            assert_eq!(map.data, vec![Allocated(1), Allocated(2), free_none(), Allocated(5)]);
            assert_eq!(map.freelist, Some(FreeList::singleton(2)));
        }

        #[test]
        fn test_iter() {
            let mut map = IdMap::new();
            map.insert(1, 0);
            map.insert(3, 1);
            map.insert(6, 2);
            assert_eq!(
                map.data,
                vec![
                    free_head(2),
                    Allocated(0),
                    free(0, 4),
                    Allocated(1),
                    free(2, 5),
                    free_tail(4),
                    Allocated(2),
                ]
            );
            assert_eq!(map.freelist, Some(FreeList { head: 0, tail: 5 }));
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
            map.insert(1, 0);
            map.insert(3, 1);
            map.insert(6, 2);
            assert_eq!(
                map.data,
                vec![
                    free_head(2),
                    Allocated(0),
                    free(0, 4),
                    Allocated(1),
                    free(2, 5),
                    free_tail(4),
                    Allocated(2),
                ]
            );
            assert_eq!(map.freelist, Some(FreeList { head: 0, tail: 5 }));
            for (k, v) in map.iter_mut() {
                *v += k as u32;
            }
            assert_eq!(
                map.data,
                vec![
                    free_head(2),
                    Allocated(1),
                    free(0, 4),
                    Allocated(4),
                    free(2, 5),
                    free_tail(4),
                    Allocated(8),
                ]
            );
            assert_eq!(map.freelist, Some(FreeList { head: 0, tail: 5 }));
        }

        #[test]
        fn test_update_retain() {
            // First, test that removed entries are actually removed, and that
            // the remaining entries are actually left there.

            let mut map = IdMap::new();
            for i in 0..8 {
                map.push(i);
            }

            let old_map = map.clone();

            // Keep only the even entries, and double the rest.
            let f = |x: &mut usize| {
                if *x % 2 == 0 {
                    false
                } else {
                    *x *= 2;
                    true
                }
            };

            // First, construct the iterator but then discard it, and test that
            // nothing has been modified.
            map.update_retain(f);
            assert_eq!(map.data, old_map.data);
            assert_eq!(map.freelist, old_map.freelist);

            // Now actually execute the iterator.
            let taken: Vec<_> = map.update_retain(f).collect();
            let remaining: Vec<_> = map.iter().map(|(key, entry)| (key, entry.clone())).collect();

            assert_eq!(taken.as_slice(), [(0, 0), (2, 2), (4, 4), (6, 6)]);
            assert_eq!(remaining.as_slice(), [(1, 2), (3, 6), (5, 10), (7, 14)]);

            // Second, test that the underlying vector is compressed after the
            // iterator has been consumed.

            // Make sure that the buffer is laid out as we expect it
            // so that this test is actually valid.
            assert_eq!(
                map.data,
                [
                    free_tail(2),
                    Allocated(2),
                    free(4, 0),
                    Allocated(6),
                    free(6, 2),
                    Allocated(10),
                    free_head(4),
                    Allocated(14),
                ]
            );

            let taken: Vec<_> = map.update_retain(|x| *x < 10).collect();
            assert_eq!(taken, [(5, 10), (7, 14)]);

            // Make sure that the underlying vector has been compressed.
            assert_eq!(map.data, [free_tail(2), Allocated(2), free_head(0), Allocated(6),]);
        }

        #[test]
        fn test_entry() {
            let mut map = IdMap::new();
            assert_eq!(*map.entry(1).or_insert(2), 2);
            assert_eq!(map.data, vec![free_none(), Allocated(2)]);
            assert_eq!(map.freelist, Some(FreeList::singleton(0)));
            assert_eq!(
                *map.entry(1)
                    .and_modify(|v| {
                        *v = 10;
                    })
                    .or_insert(5),
                10
            );
            assert_eq!(map.data, vec![free_none(), Allocated(10)]);
            assert_eq!(map.freelist, Some(FreeList::singleton(0)));
            assert_eq!(
                *map.entry(2)
                    .and_modify(|v| {
                        *v = 10;
                    })
                    .or_insert(5),
                5
            );
            assert_eq!(map.data, vec![free_none(), Allocated(10), Allocated(5)]);
            assert_eq!(map.freelist, Some(FreeList::singleton(0)));
            assert_eq!(*map.entry(4).or_default(), 0);
            assert_eq!(
                map.data,
                vec![free_head(3), Allocated(10), Allocated(5), free_tail(0), Allocated(0)]
            );
            assert_eq!(map.freelist, Some(FreeList { head: 0, tail: 3 }));
            assert_eq!(*map.entry(3).or_insert_with(|| 7), 7);
            assert_eq!(
                map.data,
                vec![free_none(), Allocated(10), Allocated(5), Allocated(7), Allocated(0)]
            );
            assert_eq!(map.freelist, Some(FreeList::singleton(0)));
            assert_eq!(*map.entry(0).or_insert(1), 1);
            assert_eq!(
                map.data,
                vec![Allocated(1), Allocated(10), Allocated(5), Allocated(7), Allocated(0)]
            );
            assert_eq!(map.freelist, None);
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
            assert_eq!(
                map.data,
                vec![free_none(), Allocated(10), Allocated(5), Allocated(7), Allocated(0)]
            );
            assert_eq!(map.freelist, Some(FreeList::singleton(0)));

            match map.entry(0) {
                Entry::Vacant(mut e) => {
                    assert_eq!(*e.key(), 0);
                    assert_eq!(*e.insert(4), 4);
                }
                _ => panic!("Wrong entry type, should be vacant"),
            }
            assert_eq!(
                map.data,
                vec![Allocated(4), Allocated(10), Allocated(5), Allocated(7), Allocated(0)]
            );

            assert_eq!(map.freelist, None)
        }

        #[test]
        fn test_freelist_order() {
            let mut rng = crate::testutil::new_rng(1234981);
            use rand::seq::SliceRandom;
            const NELEMS: usize = 1_000;
            for _ in 0..1_000 {
                let mut map = IdMap::new();
                for i in 0..NELEMS {
                    assert_eq!(map.push(i), i);
                }
                // don't remove the last one to prevent compressing.
                let mut remove_seq: Vec<usize> = (0..NELEMS - 1).collect();
                remove_seq.shuffle(&mut rng);
                for i in &remove_seq {
                    map.remove(*i);
                }
                for i in remove_seq.iter().rev() {
                    // We should be able to push into the array in the same order.
                    assert_eq!(map.push(*i), *i);
                }
                map.remove(NELEMS - 1);
                for i in &remove_seq {
                    map.remove(*i);
                }
                assert!(map.is_empty());
            }
        }

        #[test]
        fn test_compress_freelist() {
            let mut map = IdMap::new();
            for _ in 0..100 {
                map.push(0);
            }
            for i in 0..100 {
                map.remove(i);
            }
            assert_eq!(map.data.len(), 0);
            assert_eq!(map.freelist, None);
        }

        #[test]
        fn test_insert_beyond_end_freelist() {
            let mut map = IdMap::new();
            for i in 0..10 {
                map.insert(2 * i + 1, 0);
            }
            for i in 0..10 {
                assert_eq!(map.push(1), 2 * i);
            }
        }

        #[test]
        fn test_double_free() {
            const MAX_KEY: usize = 100;
            let mut map1 = IdMap::new();
            map1.insert(MAX_KEY, 2);
            let mut map2 = IdMap::new();
            map2.insert(MAX_KEY, 2);
            for i in 0..MAX_KEY {
                assert!(map1.remove(i).is_none());
                // Removing an already free entry should be a no-op.
                assert_eq!(map1.data, map2.data);
                assert_eq!(map1.freelist, map2.freelist);
            }
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

pub(crate) mod token_bucket {
    use std::convert::TryFrom;
    use std::time::Duration;

    use crate::context::InstantContext;

    // TODO(joshlf): Remove this once Duration::SECOND is stabilized.
    const SECOND: Duration = Duration::from_secs(1);

    /// Instead of actually storing the number of tokens, we store the number of
    /// fractions of `1 / TOKEN_MULTIPLIER`. If we stored the number of tokens,
    /// then under heavy load scenarios, the actual observed rate could be far
    /// off from the ideal rate due to integer rounding issues. Storing
    /// fractions instead limits the inaccuracy to at most `1 /
    /// TOKEN_MULTIPLIER` away from the ideal rate. See the comment in
    /// `try_take` for more details.
    ///
    /// Note that the choice of 256 for `TOKEN_MULTIPLIER` provides us with good
    /// accuracy (only deviating from the ideal rate by 1/256) while still
    /// allowing for a maximum rate of 2^56 tokens per second.
    const TOKEN_MULTIPLIER: u64 = 256;

    /// A [token bucket] used for rate limiting.
    ///
    /// `TokenBucket` implements rate limiting by "filling" a bucket with
    /// "tokens" at a constant rate, and allowing tokens to be consumed from the
    /// bucket until it is empty. This guarantees that a consumer may only
    /// maintain a rate of consumption faster than the rate of refilling for a
    /// bounded amount of time before they will catch up and find the bucket
    /// empty.
    ///
    /// Note that the bucket has a maximum size beyond which no new tokens will
    /// be added. This prevents a long quiet period from building up a large
    /// backlog of tokens which can then be used in an intense and sustained
    /// burst.
    ///
    /// This implementation does not require any background threads or timers to
    /// operate; it refills the bucket during calls to `try_take`, so no extra
    /// infrastructure is required to use it.
    ///
    /// [token bucket]: https://en.wikipedia.org/wiki/Token_bucket
    pub(crate) struct TokenBucket<I> {
        // The last time that the bucket was refilled, or `None` if the bucket
        // has never been refilled.
        last_refilled: Option<I>,
        token_fractions: u64,
        token_fractions_per_second: u64,
    }

    impl<I> TokenBucket<I> {
        /// Constructs a new `TokenBucket` and initializes it with one second's
        /// worth of tokens.
        ///
        /// # Panics
        ///
        /// `new` panics if `tokens_per_second` is greater than 2^56 - 1.
        pub(crate) fn new(tokens_per_second: u64) -> TokenBucket<I> {
            let token_fractions_per_second =
                tokens_per_second.checked_mul(TOKEN_MULTIPLIER).unwrap();
            TokenBucket {
                last_refilled: None,
                // Initialize to 0 so that the first call to `try_take` will
                // initialize the `last_refilled` time and fill the bucket. If
                // we initialized this to a full bucket, then an immediate burst
                // of calls to `try_take` would appear as though they'd happened
                // over the course of a second, and the client would effectively
                // get double the ideal rate until the second round of tokens
                // expired.
                token_fractions: 0,
                token_fractions_per_second,
            }
        }
    }

    impl<I: crate::Instant> TokenBucket<I> {
        /// Attempt to take a token from the bucket.
        ///
        /// `try_take` attempts to take a token from the bucket. If the bucket
        /// is currently empty, then no token is available to be taken, and
        /// `try_take` return false.
        pub(crate) fn try_take<C: InstantContext<Instant = I>>(&mut self, ctx: &C) -> bool {
            if self.token_fractions >= TOKEN_MULTIPLIER {
                self.token_fractions -= TOKEN_MULTIPLIER;
                return true;
            }

            // The algorithm implemented here is as follows: Whenever the bucket
            // empties, refill it immediately. In order not to violate the
            // requirement that tokens are added at a particular rate, we only
            // add the number of tokens that "should have been" added since the
            // last refill. We never add more than one second's worth of tokens
            // at a time in order to guarantee that the bucket never has more
            // than one second's worth of tokens in it.
            //
            // If tokens are being consumed at a rate slower than they are being
            // added, then we will exhaust the bucket less often than once per
            // second, and every refill will be a complete refill. If tokens are
            // being consumed at a rate faster than they are being added, then
            // the duration between refills will continuously decrease until
            // every call to `try_take` adds 0 or t in [1, 2) tokens.
            //
            // Consider, for example, a production rate of 32 tokens per second
            // and a consumption rate of 64 tokens per second:
            // - First, there are 32 tokens in the bucket.
            // - After 0.5 seconds, all 32 have been exhausted.
            // - The call to `try_take` which exhausts the bucket refills the
            //   bucket with 0.5 seconds' worth of tokens, or 16 tokens.
            //
            // This process repeats itself, halving the number of tokens added
            // (and halving the amount of time to exhaust the bucket) until,
            // after an amount of time which is linear in the rate of tokens
            // being added, a call to `try_take` adds only 0 or t in [1, 2)
            // tokens. In either case, the bucket is left with less than 1 token
            // (if `try_take` adds >= 1 token, it also consumes 1 token
            // immediately).
            //
            // This has the potential downside of, under heavy load, executing a
            // slightly more complex algorithm on every call to `try_take`,
            // which includes querying for the current time. I (joshlf)
            // speculate that this isn't an issue in practice, but it's worth
            // calling out in case it becomes an issue in the future.

            let now = ctx.now();
            // The duration since the last refill, or 1 second, whichever is
            // shorter. If this is the first fill, pretend that a full second
            // has elapsed since the previous refill. In reality, there was no
            // previous refill, which means it's fine to fill the bucket
            // completely.
            let dur_since_last_refilled = self.last_refilled.map_or(SECOND, |last_refilled| {
                let dur = now.duration_since(last_refilled);
                if dur > SECOND {
                    SECOND
                } else {
                    dur
                }
            });

            // Do math in u128 to avoid overflow. Be careful to multiply first
            // and then divide to minimize integer division rounding error. The
            // result of the calculation should always fit in a `u64` because
            // the ratio `dur_since_last_refilled / SECOND` is guaranteed not to
            // be greater than 1.
            let added_token_fractions = u64::try_from(
                (u128::from(self.token_fractions_per_second) * dur_since_last_refilled.as_nanos())
                    / SECOND.as_nanos(),
            )
            .unwrap();

            // Only refill the bucket if we can add at least 1 token. This
            // avoids two failure modes:
            // - If we always blindly added however many token fractions are
            //   available, then under heavy load, we might constantly add 0
            //   token fractions (because less time has elapsed since
            //   `last_refilled` than is required to add a single token
            //   fraction) while still updating `last_refilled` each time. This
            //   would drop the observed rate to 0 in the worst case.
            // - If we always added >= 1 token fraction (as opposed to >= 1 full
            //   token), then we would run into integer math inaccuracy issues.
            //   In the worst case, `try_take` would be called after just less
            //   than the amount of time required to add two token fractions.
            //   The actual number of token fractions added would be rounded
            //   down to 1, and the observed rate would be slightly more than
            //   1/2 of the ideal rate.
            //
            // By always adding at least 1 token, we ensure that the worst case
            // behavior is when `try_take` is called after just less than the
            // amount of time required to add `TOKEN_MULTIPLIER + 1` token
            // fractions has elapsed. In this case, the actual number of token
            // fractions added is rounded down to 1, and the observed rate is
            // within `1 / TOKEN_MULTIPLIER` of the ideal rate.
            if let Some(new_token_fractions) =
                (self.token_fractions + added_token_fractions).checked_sub(TOKEN_MULTIPLIER)
            {
                self.token_fractions = new_token_fractions;
                self.last_refilled = Some(now);
                true
            } else {
                return false;
            }
        }
    }

    #[cfg(test)]
    mod tests {
        use super::*;
        use crate::context::testutil::{DummyInstant, DummyInstantContext};
        use crate::testutil::benchmarks::{black_box, Bencher};

        impl<I: crate::Instant> TokenBucket<I> {
            /// Call `try_take` `n` times, and assert that it succeeds every
            /// time.
            fn assert_take_n<C: InstantContext<Instant = I>>(&mut self, ctx: &C, n: usize) {
                for _ in 0..n {
                    assert!(self.try_take(ctx));
                }
            }
        }

        #[test]
        fn test_token_bucket() {
            /// Construct a `DummyInstantContext` and a `TokenBucket` with a
            /// rate of 64 tokens per second, and pass them to `f`.
            fn test<F: FnOnce(DummyInstantContext, TokenBucket<DummyInstant>)>(f: F) {
                f(DummyInstantContext::default(), TokenBucket::new(64));
            }

            // Test that, if we consume all of the tokens in the bucket, but do
            // not attempt to consume any more than that, the bucket will not be
            // updated.
            test(|mut ctx, mut bucket| {
                let epoch = ctx.now();
                assert!(bucket.try_take(&ctx));
                assert_eq!(bucket.last_refilled.unwrap(), epoch);
                assert_eq!(bucket.token_fractions, 63 * TOKEN_MULTIPLIER);

                // Sleep so that the current time will be different than the
                // time at which the `last_refilled` time was initialized. That
                // way, we can tell whether the `last_refilled` field was
                // updated or not.
                ctx.sleep(SECOND);
                bucket.assert_take_n(&ctx, 63);
                assert_eq!(bucket.last_refilled.unwrap(), epoch);
                assert_eq!(bucket.token_fractions, 0);
            });

            // Test that, if we try to consume a token when the bucket is empty,
            // it will get refilled.
            test(|mut ctx, mut bucket| {
                let epoch = ctx.now();
                assert!(bucket.try_take(&ctx));
                assert_eq!(bucket.last_refilled.unwrap(), epoch);
                assert_eq!(bucket.token_fractions, 63 * TOKEN_MULTIPLIER);

                // Sleep for one second so that the bucket will be completely
                // refilled.
                ctx.sleep(SECOND);
                bucket.assert_take_n(&ctx, 64);
                assert_eq!(bucket.last_refilled.unwrap(), DummyInstant::from(SECOND));
                // 1 token was consumed by the last call to `try_take`.
                assert_eq!(bucket.token_fractions, 63 * TOKEN_MULTIPLIER);
            });

            // Test that, if more than 1 second has elapsed since the previous
            // refill, we still only fill with 1 second's worth of tokens.
            test(|mut ctx, mut bucket| {
                let epoch = ctx.now();
                assert!(bucket.try_take(&ctx));
                assert_eq!(bucket.last_refilled.unwrap(), epoch);
                assert_eq!(bucket.token_fractions, 63 * TOKEN_MULTIPLIER);

                ctx.sleep(SECOND * 2);
                bucket.assert_take_n(&ctx, 64);
                assert_eq!(bucket.last_refilled.unwrap(), DummyInstant::from(SECOND * 2));
                // 1 token was consumed by the last call to `try_take`.
                assert_eq!(bucket.token_fractions, 63 * TOKEN_MULTIPLIER);
            });

            // Test that, if we refill the bucket when less then a second has
            // elapsed, a proportional amount of the bucket is refilled.
            test(|mut ctx, mut bucket| {
                let epoch = ctx.now();
                assert!(bucket.try_take(&ctx));
                assert_eq!(bucket.last_refilled.unwrap(), epoch);
                assert_eq!(bucket.token_fractions, 63 * TOKEN_MULTIPLIER);

                ctx.sleep(SECOND / 2);
                bucket.assert_take_n(&ctx, 64);
                assert_eq!(bucket.last_refilled.unwrap(), DummyInstant::from(SECOND / 2));
                // Since only half a second had elapsed since the previous
                // refill, only half of the tokens were refilled. 1 was consumed
                // by the last call to `try_take`.
                assert_eq!(bucket.token_fractions, 31 * TOKEN_MULTIPLIER);
            });

            // Test that, if we try to consume a token when the bucket is empty
            // and not enough time has elapsed to allow for any tokens to be
            // added, `try_take` will fail and the bucket will remain empty.
            test(|mut ctx, mut bucket| {
                // Allow 1/65 of a second to elapse so we know we're not just
                // dealing with a consequence of no time having elapsed. The
                // "correct" number of tokens to add after 1/65 of a second is
                // 64/65, which will be rounded down to 0.
                let epoch = ctx.now();
                bucket.assert_take_n(&ctx, 64);
                ctx.sleep(SECOND / 128);
                assert!(!bucket.try_take(&ctx));
                assert_eq!(bucket.last_refilled.unwrap(), epoch);
                assert_eq!(bucket.token_fractions, 0);
            });

            // Test that, as long as we consume tokens at exactly the right
            // rate, we never fail to consume a token.
            test(|mut ctx, mut bucket| {
                // Initialize the `last_refilled` time and then drain the
                // bucket, leaving the `last_refilled` time at t=0 and the
                // bucket empty.
                bucket.assert_take_n(&ctx, 64);
                for _ in 0..1_000 {
                    // `Duration`s store nanoseconds under the hood, and 64
                    // divides 1e9 evenly, so this is lossless.
                    ctx.sleep(SECOND / 64);
                    assert!(bucket.try_take(&ctx));
                    assert_eq!(bucket.token_fractions, 0);
                    assert_eq!(bucket.last_refilled.unwrap(), ctx.now());
                }
            });

            // Test that, if we consume tokens too quickly, we succeed in
            // consuming tokens the correct proportion of the time.
            //
            // Test with rates close to 1 (2/1 through 5/4) and rates much
            // larger than 1 (3/1 through 6/1).
            for (numer, denom) in
                [(2, 1), (3, 2), (4, 3), (5, 4), (3, 1), (4, 1), (5, 1), (6, 1)].into_iter()
            {
                test(|mut ctx, mut bucket| {
                    // Initialize the `last_refilled` time and then drain the
                    // bucket, leaving the `last_refilled` time at t=0 and the
                    // bucket empty.
                    bucket.assert_take_n(&ctx, 64);

                    const ATTEMPTS: u32 = 1_000;
                    let mut successes = 0;
                    for _ in 0..ATTEMPTS {
                        // In order to speed up by a factor of numer/denom, we
                        // multiply the duration between tries by its inverse,
                        // denom/numer.
                        ctx.sleep((SECOND * *denom) / (64 * *numer));
                        if bucket.try_take(&ctx) {
                            successes += 1;
                            assert_eq!(bucket.last_refilled.unwrap(), ctx.now());
                        }
                    }

                    // The observed rate can be up to 1/TOKEN_MULTIPLIER off in
                    // either direction.
                    let ideal_successes = (ATTEMPTS * denom) / numer;
                    let mult = u32::try_from(TOKEN_MULTIPLIER).unwrap();
                    assert!(successes <= (ideal_successes * (mult + 1)) / mult);
                    assert!(successes >= (ideal_successes * (mult - 1)) / mult);
                });
            }
        }

        fn bench_try_take<B: Bencher>(b: &mut B, enforced_rate: u64, try_rate: u32) {
            let sleep = SECOND / try_rate;
            let mut ctx = DummyInstantContext::default();
            let mut bucket = TokenBucket::new(enforced_rate);
            b.iter(|| {
                ctx.sleep(sleep);
                black_box(bucket.try_take(black_box(&ctx)));
            });
        }

        // These benchmarks use the fastest possible `InstantContext`, and
        // should be considered an upper bound on performance.

        // Call `try_take` at 1/64 the enforced rate.
        bench!(bench_try_take_slow, |b| bench_try_take(b, 64, 1));
        // Call `try_take` at 1/2 the enforced rate.
        bench!(bench_try_take_half_rate, |b| bench_try_take(b, 64, 32));
        // Call `try_take` at the enforced rate.
        bench!(bench_try_take_equal_rate, |b| bench_try_take(b, 64, 64));
        // Call `try_take` at 65/64 the enforced rate.
        bench!(bench_try_take_almost_equal_rate, |b| bench_try_take(b, 64, 65));
        // Call `try_take` at 2x the enforced rate.
        bench!(bench_try_take_double_rate, |b| bench_try_take(b, 64, 64 * 2));
    }

    #[test]
    fn test_token_bucket_new() {
        // Test that `new` doesn't panic if given 2^56 - 1.
        TokenBucket::<()>::new((1 << 56) - 1);
    }

    #[test]
    #[should_panic]
    fn test_token_bucket_new_panics() {
        // Test that `new` panics if given 2^56
        TokenBucket::<()>::new(1 << 56);
    }
}

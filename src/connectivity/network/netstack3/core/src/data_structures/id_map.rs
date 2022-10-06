// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Identifier map data structure.
//!
//! Defines the [`IdMap`] data structure: A generic container mapped keyed by an
//! internally managed pool of identifiers kept densely packed.

use alloc::vec::Vec;
use core::fmt::Debug;

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

/// Stores positions of the head and tail of the free-list linked by
/// `FreeListLink`.
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

impl<T> IdMapEntry<T> {
    /// Returns a reference to the freelist link if the entry is free, otherwise None.
    fn as_free_or_none(&self) -> Option<&FreeListLink> {
        match self {
            IdMapEntry::Free(link) => Some(link),
            IdMapEntry::Allocated(_) => None,
        }
    }

    /// Returns a mutable reference to the freelist link if the entry is free, otherwise None.
    fn as_free_or_none_mut(&mut self) -> Option<&mut FreeListLink> {
        match self {
            IdMapEntry::Free(link) => Some(link),
            IdMapEntry::Allocated(_) => None,
        }
    }
}

/// A generic container for `T` keyed by densely packed integers.
///
/// `IdMap` is a generic container keyed by `usize` that manages its own key
/// pool. `IdMap` reuses keys that are free to keep its key pool as dense as
/// possible.
///
/// The main guarantee provided by `IdMap` is that all `get` operations are
/// provided in O(1) without the need to hash the keys. The only operations of
/// `IdMap` that are used in the hot path are the `get` operations.
///
/// All operations that mutate the `IdMap` are O(log(n)) average.
///
/// `push` will grab the lowest free `id` and assign it to the given value,
/// returning the assigned `id`. `insert` can be used for assigning a specific
/// `id` to an object, and returns the previous object at that `id` if any.
#[cfg_attr(test, derive(Clone))]
pub struct IdMap<T> {
    freelist: Option<FreeList>,
    data: Vec<IdMapEntry<T>>,
}

impl<T> IdMap<T> {
    /// Creates a new empty [`IdMap`].
    pub fn new() -> Self {
        Self { freelist: None, data: Vec::new() }
    }

    /// Returns `true` if there are no items in [`IdMap`].
    pub fn is_empty(&self) -> bool {
        // Because of `compress`, our map is empty if and only if the underlying
        // vector is empty. If the underlying vector is not empty but our map is
        // empty, it must be the case where the underlying vector contains
        // nothing but free entries, and all these entries should be reclaimed
        // when the last allocated entry is removed.
        self.data.is_empty()
    }

    /// Returns a reference to the item indexed by `key`, or `None` if the `key`
    /// doesn't exist.
    pub fn get(&self, key: Key) -> Option<&T> {
        self.data.get(key).and_then(|v| match v {
            IdMapEntry::Allocated(t) => Some(t),
            IdMapEntry::Free(_) => None,
        })
    }

    /// Returns a mutable reference to the item indexed by `key`, or `None` if
    /// the `key` doesn't exist.
    pub fn get_mut(&mut self, key: Key) -> Option<&mut T> {
        self.data.get_mut(key).and_then(|v| match v {
            IdMapEntry::Allocated(t) => Some(t),
            IdMapEntry::Free(_) => None,
        })
    }

    /// Removes item indexed by `key` from the container.
    ///
    /// Returns the removed item if it exists, or `None` otherwise.
    ///
    /// Note: the worst case complexity of `remove` is O(key) if the backing
    /// data structure of the [`IdMap`] is too sparse.
    pub fn remove(&mut self, key: Key) -> Option<T> {
        let r = self.remove_inner(key);
        if r.is_some() {
            self.compress();
        }
        r
    }

    fn remove_inner(&mut self, key: Key) -> Option<T> {
        let Self { data, freelist } = self;
        let r = data.get_mut(key).and_then(|v| {
            match v {
                IdMapEntry::Allocated(_) => {
                    let old_head = freelist.map(|l| l.head);
                    let new_link = IdMapEntry::Free(FreeListLink { prev: None, next: old_head });
                    match core::mem::replace(v, new_link) {
                        IdMapEntry::Allocated(t) => Some(t),
                        IdMapEntry::Free(_) => unreachable!("already matched"),
                    }
                }
                // If it is currently free, we don't want to unlink the entry and
                // link it back at the head again.
                IdMapEntry::Free(_) => None,
            }
        });
        if r.is_some() {
            // If it was allocated, we add the removed entry to the head of the
            // free-list.
            match freelist.as_mut() {
                Some(FreeList { head, .. }) => {
                    data[*head]
                        .as_free_or_none_mut()
                        .unwrap_or_else(|| panic!("freelist head node {} is not free", head))
                        .prev = Some(key);
                    *head = key;
                }
                None => *freelist = Some(FreeList::singleton(key)),
            }
        }
        r
    }

    /// Inserts `item` at `key`.
    ///
    /// If the [`IdMap`] already contained an item indexed by `key`, `insert`
    /// returns it, or `None` otherwise.
    ///
    /// Note: The worst case complexity of `insert` is O(key) if `key` is larger
    /// than the number of items currently held by the [`IdMap`].
    pub fn insert(&mut self, key: Key, item: T) -> Option<T> {
        if key < self.data.len() {
            let prev = core::mem::replace(&mut self.data[key], IdMapEntry::Allocated(item));
            match prev {
                IdMapEntry::Free(link) => {
                    self.freelist_unlink(link);
                    None
                }
                IdMapEntry::Allocated(t) => Some(t),
            }
        } else {
            let start_len = self.data.len();
            // Fill the gap `start_len .. key` with free entries. Currently, the
            // free entries introduced by `insert` is linked at the end of the
            // free list so that hopefully these free entries near the end will
            // get less likely to be allocated than those near the beginning,
            // this may help reduce the memory footprint because we have
            // increased the chance for the underlying vector to be compressed.
            // TODO: explore whether we can reorder the list on the fly to
            // further increase the chance for compressing.
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
                        self.data[*tail]
                            .as_free_or_none_mut()
                            .unwrap_or_else(|| panic!("freelist tail node {} is not free", tail))
                            .next = Some(start_len);
                        *tail = new_tail;
                    }
                    None => {
                        self.freelist = Some(FreeList { head: start_len, tail: new_tail });
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
    /// `push` inserts a new `item` into the [`IdMap`] and returns the key value
    /// allocated for `item`. `push` will allocate *any* key that is currently
    /// free in the internal structure, so it may return a key that was used
    /// previously but has since been removed.
    ///
    /// Note: The worst case complexity of `push` is O(n) where n is the number
    /// of items held by the [`IdMap`]. This can happen if the internal
    /// structure gets fragmented.
    pub fn push(&mut self, item: T) -> Key {
        *self.push_entry(item).key()
    }

    /// Inserts `item` into the [`IdMap`] and returns an [`OccupiedEntry`] for
    /// it.
    ///
    /// Like [`IdMap::push`] except that it returns an entry instead of an
    /// index.
    pub fn push_entry(&mut self, item: T) -> OccupiedEntry<'_, usize, T> {
        if let Some(FreeList { head, .. }) = self.freelist.as_mut() {
            let ret = *head;
            let old =
                core::mem::replace(self.data.get_mut(ret).unwrap(), IdMapEntry::Allocated(item));
            let old_link = old
                .as_free_or_none()
                .unwrap_or_else(|| panic!("freelist head node {} is not free", head));
            // Update the head of the freelist.
            match old_link.next {
                Some(new_head) => {
                    *head = new_head;
                    self.data[new_head]
                        .as_free_or_none_mut()
                        .unwrap_or_else(|| panic!("new free list head {} is not free", new_head))
                        .prev = None;
                }
                None => self.freelist = None,
            }
            OccupiedEntry { key: ret, id_map: self }
        } else {
            // If we run out of freelist, we simply push a new entry into the
            // underlying vector.
            let key = self.data.len();
            self.data.push(IdMapEntry::Allocated(item));
            OccupiedEntry { key, id_map: self }
        }
    }

    /// Compresses the tail of the internal `Vec`.
    ///
    /// `compress` removes all trailing elements in `data` that are `None`,
    /// shrinking the internal `Vec`.
    fn compress(&mut self) {
        // First, find the last non-free entry.
        if let Some(idx) = self.data.iter().enumerate().rev().find_map(|(k, v)| match v {
            IdMapEntry::Allocated(_) => Some(k),
            IdMapEntry::Free(_) => None,
        }) {
            // Remove all the trailing free entries.
            for i in idx + 1..self.data.len() {
                let link = *self.data[i].as_free_or_none().expect("already confirmed as free");
                self.freelist_unlink(link);
            }
            self.data.truncate(idx + 1);
        } else {
            // There is nothing left in the vector.
            self.data.clear();
            self.freelist = None;
        }
    }

    /// Creates an iterator over the containing items and their associated keys.
    pub fn iter(&self) -> impl Iterator<Item = (Key, &T)> {
        self.data.iter().enumerate().filter_map(|(k, v)| match v {
            IdMapEntry::Allocated(t) => Some((k, t)),
            IdMapEntry::Free(_) => None,
        })
    }

    /// Creates a mutable iterator over the containing items and their
    /// associated keys.
    pub fn iter_mut(&mut self) -> impl Iterator<Item = (Key, &mut T)> {
        self.data.iter_mut().enumerate().filter_map(|(k, v)| match v {
            IdMapEntry::Allocated(t) => Some((k, t)),
            IdMapEntry::Free(_) => None,
        })
    }

    /// Gets the given key's corresponding entry in the map for in-place
    /// manipulation.
    pub fn entry(&mut self, key: usize) -> Entry<'_, usize, T> {
        if let Some(IdMapEntry::Allocated(_)) = self.data.get(key) {
            Entry::Occupied(OccupiedEntry { key, id_map: self })
        } else {
            Entry::Vacant(VacantEntry { key, id_map: self })
        }
    }

    /// Update the elements of the map in-place, retaining only the elements for
    /// which `f` returns `Ok`.
    ///
    /// `update_return` returns an iterator that invokes `f` on each element of
    /// the map, and removes from the map those elements for which `f` returns
    /// `Err`. The returned iterator iterates over the removed elements, each
    /// paired with the error that `f` returned that caused it to be removed.
    /// The removal only happens as the iterator is executed, so calling
    /// `update_retain` and not executing the returned iterator will do nothing.
    pub fn update_retain<'a, E: 'a, F: 'a + FnMut(&mut T) -> Result<(), E>>(
        &'a mut self,
        mut f: F,
    ) -> impl 'a + Iterator<Item = (Key, T, E)>
    where
        T: 'a,
    {
        (0..self.data.len()).filter_map(move |k| {
            let ret = if let IdMapEntry::Allocated(t) = self.data.get_mut(k).unwrap() {
                match f(t) {
                    Ok(()) => None,
                    Err(err) => {
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
                        Some((k, self.remove_inner(k).unwrap(), err))
                    }
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
    fn freelist_unlink(&mut self, link: FreeListLink) {
        let FreeListLink { prev, next } = link;

        match (prev, next) {
            (Some(prev), Some(next)) => {
                // A normal node in the middle of a list.
                self.data[prev]
                    .as_free_or_none_mut()
                    .unwrap_or_else(|| panic!("free's prev {} is not free", prev))
                    .next = Some(next);
                self.data[next]
                    .as_free_or_none_mut()
                    .unwrap_or_else(|| panic!("free's next {} is not free", next))
                    .prev = Some(prev);
            }
            (Some(prev), None) => {
                // The node at the tail.
                self.data[prev]
                    .as_free_or_none_mut()
                    .unwrap_or_else(|| panic!("tail's prev {} is not free", prev))
                    .next = next;
                self.freelist.as_mut().unwrap().tail = prev;
            }
            (None, Some(next)) => {
                // The node at the head.
                self.data[next]
                    .as_free_or_none_mut()
                    .unwrap_or_else(|| panic!("head's next {} is not free", next))
                    .prev = prev;
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

/// A key providing an index into an [`IdMap`].
pub trait EntryKey {
    /// Returns the index for this key.
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
    /// Sets the value of the entry with the VacantEntry's key, and returns a
    /// mutable reference to it.
    pub fn insert(self, value: T) -> &'a mut T
    where
        K: EntryKey,
    {
        assert!(self.id_map.insert(self.key.get_key_index(), value).is_none());
        match &mut self.id_map.data[self.key.get_key_index()] {
            IdMapEntry::Allocated(t) => t,
            IdMapEntry::Free(_) => unreachable!("entry is known to be vacant"),
        }
    }

    /// Gets a reference to the key that would be used when inserting a value
    /// through the `VacantEntry`.
    pub fn key(&self) -> &K {
        &self.key
    }

    /// Take ownership of the key.
    pub fn into_key(self) -> K {
        self.key
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
        let idx = self.key.get_key_index();
        let key = f(self.key);
        assert_eq!(idx, key.get_key_index());
        VacantEntry { key, id_map: self.id_map }
    }
}

/// A view into an occupied entry in a map. It is part of the [`Entry`] enum.
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
        // We can unwrap because value is always Some for OccupiedEntry.
        self.id_map.get(self.key.get_key_index()).unwrap()
    }

    /// Gets a mutable reference to the value in the entry.
    ///
    /// If you need a reference to the `OccupiedEntry` which may outlive the
    /// destruction of the entry value, see [`OccupiedEntry::into_mut`].
    pub fn get_mut(&mut self) -> &mut T {
        // We can unwrap because value is always Some for OccupiedEntry.
        self.id_map.get_mut(self.key.get_key_index()).unwrap()
    }

    /// Converts the `OccupiedEntry` into a mutable reference to the value in
    /// the entry with a lifetime bound to the map itself.
    ///
    /// If you need multiple references to the `OccupiedEntry`, see
    /// [`OccupiedEntry::get_mut`].
    pub fn into_mut(self) -> &'a mut T {
        // We can unwrap because value is always Some for OccupiedEntry.
        self.id_map.get_mut(self.key.get_key_index()).unwrap()
    }

    /// Sets the value of the entry, and returns the entry's old value.
    pub fn insert(&mut self, value: T) -> T {
        // We can unwrap because value is always Some for OccupiedEntry.
        self.id_map.insert(self.key.get_key_index(), value).unwrap()
    }

    /// Takes the value out of the entry, and returns it.
    pub fn remove(self) -> T {
        // We can unwrap because value is always Some for OccupiedEntry.
        self.id_map.remove(self.key.get_key_index()).unwrap()
    }

    /// Changes the key type of this `OccupiedEntry` to another key `X` that
    /// still maps to the same index in an `IdMap`.
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
        let idx = self.key.get_key_index();
        let key = f(self.key);
        assert_eq!(idx, key.get_key_index());
        OccupiedEntry { key, id_map: self.id_map }
    }
}

impl<'a, K: Debug, T> Debug for OccupiedEntry<'a, K, T> {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        let Self { key, id_map: _ } = self;
        f.debug_struct("OccupiedEntry").field("key", key).field("id_map", &"_").finish()
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
            Entry::Vacant(e) => e.key(),
            Entry::Occupied(e) => e.key(),
        }
    }

    /// Ensures a value is in the entry by inserting `default` if empty, and
    /// returns a mutable reference to the value in the entry.
    pub fn or_insert(self, default: T) -> &'a mut T
    where
        K: EntryKey,
    {
        match self {
            Entry::Vacant(e) => e.insert(default),
            Entry::Occupied(e) => e.into_mut(),
        }
    }

    /// Ensures a value is in the entry by inserting the result of the function
    /// `f` if empty, and returns a mutable reference to the value in the entry.
    pub fn or_insert_with<F: FnOnce() -> T>(self, f: F) -> &'a mut T
    where
        K: EntryKey,
    {
        match self {
            Entry::Vacant(e) => e.insert(f()),
            Entry::Occupied(e) => e.into_mut(),
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
            Entry::Vacant(e) => Entry::Vacant(e),
            Entry::Occupied(mut e) => {
                f(e.get_mut());
                Entry::Occupied(e)
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use alloc::{collections::HashMap, vec, vec::Vec};

    use rand::seq::SliceRandom as _;

    use super::{
        IdMapEntry::{Allocated, Free},
        *,
    };
    use crate::testutil::assert_empty;

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
        assert_eq!(map.insert(1, 2), None);
        let IdMap { data, freelist } = &map;
        assert_eq!(data, &vec![free_none(), Allocated(2)]);
        assert_eq!(freelist, &Some(FreeList::singleton(0)));
        assert_eq!(map.push(1), 0);
        let IdMap { data, freelist } = &map;
        assert_eq!(data, &vec![Allocated(1), Allocated(2)]);
        assert_eq!(freelist, &None);
        assert_eq!(map.push(3), 2);
        let IdMap { data, freelist } = &map;
        assert_eq!(data, &vec![Allocated(1), Allocated(2), Allocated(3)]);
        assert_eq!(freelist, &None);
    }

    #[test]
    fn test_get() {
        let mut map = IdMap::new();
        assert_eq!(map.push(1), 0);
        assert_eq!(map.insert(2, 3), None);
        let IdMap { data, freelist } = &map;
        assert_eq!(data, &vec![Allocated(1), free_none(), Allocated(3)]);
        assert_eq!(freelist, &Some(FreeList::singleton(1)));
        assert_eq!(*map.get(0).unwrap(), 1);
        assert_eq!(map.get(1), None);
        assert_eq!(*map.get(2).unwrap(), 3);
        assert_eq!(map.get(3), None);
    }

    #[test]
    fn test_get_mut() {
        let mut map = IdMap::new();
        assert_eq!(map.push(1), 0);
        assert_eq!(map.insert(2, 3), None);
        let IdMap { data, freelist } = &map;
        assert_eq!(data, &vec![Allocated(1), free_none(), Allocated(3)]);
        assert_eq!(freelist, &Some(FreeList::singleton(1)));
        *map.get_mut(2).unwrap() = 10;
        assert_eq!(*map.get(0).unwrap(), 1);
        assert_eq!(*map.get(2).unwrap(), 10);

        assert_eq!(map.get_mut(1), None);
        assert_eq!(map.get_mut(3), None);
    }

    #[test]
    fn test_is_empty() {
        let mut map = IdMap::<i32>::new();
        assert!(map.is_empty());
        assert_eq!(map.push(1), 0);
        assert!(!map.is_empty());
    }

    #[test]
    fn test_remove() {
        let mut map = IdMap::new();
        assert_eq!(map.push(1), 0);
        assert_eq!(map.push(2), 1);
        assert_eq!(map.push(3), 2);
        let IdMap { data, freelist } = &map;
        assert_eq!(data, &vec![Allocated(1), Allocated(2), Allocated(3)]);
        assert_eq!(freelist, &None);
        assert_eq!(map.remove(1).unwrap(), 2);

        assert_eq!(map.remove(1), None);
        let IdMap { data, freelist } = &map;
        assert_eq!(data, &vec![Allocated(1), free_none(), Allocated(3)]);
        assert_eq!(freelist, &Some(FreeList::singleton(1)));
    }

    #[test]
    fn test_remove_compress() {
        let mut map = IdMap::new();
        assert_eq!(map.insert(0, 1), None);
        assert_eq!(map.insert(2, 3), None);
        let IdMap { data, freelist } = &map;
        assert_eq!(data, &vec![Allocated(1), free_none(), Allocated(3)]);
        assert_eq!(freelist, &Some(FreeList::singleton(1)));
        assert_eq!(map.remove(2).unwrap(), 3);
        let IdMap { data, freelist } = &map;
        assert_eq!(data, &vec![Allocated(1)]);
        assert_eq!(freelist, &None);
        assert_eq!(map.remove(0).unwrap(), 1);
        assert_empty(map.data);
    }

    #[test]
    fn test_insert() {
        let mut map = IdMap::new();
        assert_eq!(map.insert(1, 2), None);
        let IdMap { data, freelist } = &map;
        assert_eq!(data, &vec![free_none(), Allocated(2)]);
        assert_eq!(freelist, &Some(FreeList::singleton(0)));
        assert_eq!(map.insert(3, 4), None);
        let IdMap { data, freelist } = &map;
        assert_eq!(data, &vec![free_head(2), Allocated(2), free_tail(0), Allocated(4)]);
        assert_eq!(freelist, &Some(FreeList { head: 0, tail: 2 }));
        assert_eq!(map.insert(0, 1), None);
        let IdMap { data, freelist } = &map;
        assert_eq!(data, &vec![Allocated(1), Allocated(2), free_none(), Allocated(4)]);
        assert_eq!(freelist, &Some(FreeList::singleton(2)));
        assert_eq!(map.insert(3, 5).unwrap(), 4);
        let IdMap { data, freelist } = &map;
        assert_eq!(data, &vec![Allocated(1), Allocated(2), free_none(), Allocated(5)]);
        assert_eq!(freelist, &Some(FreeList::singleton(2)));
    }

    #[test]
    fn test_insert_gap() {
        // Regression test for https://fxbug.dev/89714: a sequence of inserts that creates a run of
        // free elements with size > 1 followed by removes can result in `freelist` = None even
        // though `data` contains FreeListLink entries.
        let mut map = IdMap::new();
        assert_eq!(map.insert(0, 0), None);
        assert_eq!(map.insert(3, 5), None);
        let IdMap { data, freelist } = &map;
        assert_eq!(data, &vec![Allocated(0), free_head(2), free_tail(1), Allocated(5)]);
        assert_eq!(freelist, &Some(FreeList { head: 1, tail: 2 }));

        assert_eq!(map.push(6), 1);
        assert_eq!(map.remove(1), Some(6));
        assert_eq!(map.remove(3), Some(5));

        // The remove() call compresses the list, which leaves just the 0 element.
        let IdMap { data, freelist } = &map;
        assert_eq!(data, &vec![Allocated(0)]);
        assert_eq!(freelist, &None);
    }

    #[test]
    fn test_iter() {
        let mut map = IdMap::new();
        assert_eq!(map.insert(1, 0), None);
        assert_eq!(map.insert(3, 1), None);
        assert_eq!(map.insert(6, 2), None);
        let IdMap { data, freelist } = &map;
        assert_eq!(
            data,
            &vec![
                free_head(2),
                Allocated(0),
                free(0, 4),
                Allocated(1),
                free(2, 5),
                free_tail(4),
                Allocated(2),
            ]
        );
        assert_eq!(freelist, &Some(FreeList { head: 0, tail: 5 }));
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
        assert_eq!(map.insert(1, 0), None);
        assert_eq!(map.insert(3, 1), None);
        assert_eq!(map.insert(6, 2), None);
        let IdMap { data, freelist } = &map;
        assert_eq!(
            data,
            &vec![
                free_head(2),
                Allocated(0),
                free(0, 4),
                Allocated(1),
                free(2, 5),
                free_tail(4),
                Allocated(2),
            ]
        );
        assert_eq!(freelist, &Some(FreeList { head: 0, tail: 5 }));
        for (k, v) in map.iter_mut() {
            *v += k as u32;
        }
        let IdMap { data, freelist } = &map;
        assert_eq!(
            data,
            &vec![
                free_head(2),
                Allocated(1),
                free(0, 4),
                Allocated(4),
                free(2, 5),
                free_tail(4),
                Allocated(8),
            ]
        );
        assert_eq!(freelist, &Some(FreeList { head: 0, tail: 5 }));
    }

    #[test]
    fn test_update_retain() {
        // First, test that removed entries are actually removed, and that the
        // remaining entries are actually left there.

        let mut map = IdMap::new();
        for i in 0..8 {
            assert_eq!(map.push(i), i);
        }

        let old_map = map.clone();

        // Keep only the even entries, and double the rest. For the rejected
        // entries, return their square.
        let f = |x: &mut usize| {
            if *x % 2 == 0 {
                Err((*x) * (*x))
            } else {
                *x *= 2;
                Ok(())
            }
        };

        // First, construct the iterator but then discard it, and test that
        // nothing has been modified.
        let _ = map.update_retain(f);
        let IdMap { data, freelist } = &map;
        assert_eq!(data, &old_map.data);
        assert_eq!(freelist, &old_map.freelist);

        // Now actually execute the iterator.
        let taken: Vec<_> = map.update_retain(f).collect();
        let remaining: Vec<_> = map.iter().map(|(key, entry)| (key, entry.clone())).collect();

        assert_eq!(taken.as_slice(), [(0, 0, 0), (2, 2, 4), (4, 4, 16), (6, 6, 36)]);
        assert_eq!(remaining.as_slice(), [(1, 2), (3, 6), (5, 10), (7, 14)]);

        // Second, test that the underlying vector is compressed after the
        // iterator has been consumed.

        // Make sure that the buffer is laid out as we expect it so that this
        // test is actually valid.
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

        let taken: Vec<_> = map.update_retain(|x| if *x < 10 { Ok(()) } else { Err(()) }).collect();
        assert_eq!(taken, [(5, 10, ()), (7, 14, ())]);

        // Make sure that the underlying vector has been compressed.
        let IdMap { data, freelist: _ } = &map;
        assert_eq!(data, &[free_tail(2), Allocated(2), free_head(0), Allocated(6),]);
    }

    #[test]
    fn test_entry() {
        let mut map = IdMap::new();
        assert_eq!(*map.entry(1).or_insert(2), 2);
        let IdMap { data, freelist } = &map;
        assert_eq!(data, &vec![free_none(), Allocated(2)]);
        assert_eq!(freelist, &Some(FreeList::singleton(0)));
        assert_eq!(
            *map.entry(1)
                .and_modify(|v| {
                    *v = 10;
                })
                .or_insert(5),
            10
        );
        let IdMap { data, freelist } = &map;
        assert_eq!(data, &vec![free_none(), Allocated(10)]);
        assert_eq!(freelist, &Some(FreeList::singleton(0)));
        assert_eq!(
            *map.entry(2)
                .and_modify(|v| {
                    *v = 10;
                })
                .or_insert(5),
            5
        );
        let IdMap { data, freelist } = &map;
        assert_eq!(data, &vec![free_none(), Allocated(10), Allocated(5)]);
        assert_eq!(freelist, &Some(FreeList::singleton(0)));
        assert_eq!(*map.entry(4).or_default(), 0);
        let IdMap { data, freelist } = &map;
        assert_eq!(
            data,
            &vec![free_head(3), Allocated(10), Allocated(5), free_tail(0), Allocated(0)]
        );
        assert_eq!(freelist, &Some(FreeList { head: 0, tail: 3 }));
        assert_eq!(*map.entry(3).or_insert_with(|| 7), 7);
        let IdMap { data, freelist } = &map;
        assert_eq!(
            data,
            &vec![free_none(), Allocated(10), Allocated(5), Allocated(7), Allocated(0)]
        );
        assert_eq!(freelist, &Some(FreeList::singleton(0)));
        assert_eq!(*map.entry(0).or_insert(1), 1);
        let IdMap { data, freelist } = &map;
        assert_eq!(
            data,
            &vec![Allocated(1), Allocated(10), Allocated(5), Allocated(7), Allocated(0)]
        );
        assert_eq!(freelist, &None);
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
        let IdMap { data, freelist } = &map;
        assert_eq!(
            data,
            &vec![free_none(), Allocated(10), Allocated(5), Allocated(7), Allocated(0)]
        );
        assert_eq!(freelist, &Some(FreeList::singleton(0)));

        match map.entry(0) {
            Entry::Vacant(e) => {
                assert_eq!(*e.key(), 0);
                assert_eq!(*e.insert(4), 4);
            }
            _ => panic!("Wrong entry type, should be vacant"),
        }
        let IdMap { data, freelist } = &map;
        assert_eq!(
            data,
            &vec![Allocated(4), Allocated(10), Allocated(5), Allocated(7), Allocated(0)]
        );

        assert_eq!(freelist, &None)
    }

    #[test]
    fn test_freelist_order() {
        let mut rng = crate::testutil::new_rng(1234981);
        const NELEMS: usize = 1_000;
        for _ in 0..1_000 {
            let mut map = IdMap::new();
            for i in 0..NELEMS {
                assert_eq!(map.push(i), i);
            }
            // Don't remove the last one to prevent compressing.
            let mut remove_seq: Vec<usize> = (0..NELEMS - 1).collect();
            remove_seq.shuffle(&mut rng);
            for i in &remove_seq {
                assert_ne!(map.remove(*i), None);
            }
            for i in remove_seq.iter().rev() {
                // We should be able to push into the array in the same order.
                assert_eq!(map.push(*i), *i);
            }
            assert_ne!(map.remove(NELEMS - 1), None);
            for i in &remove_seq {
                assert_ne!(map.remove(*i), None);
            }
            assert_empty(map.iter());
        }
    }

    #[test]
    fn test_compress_freelist() {
        let mut map = IdMap::new();
        for i in 0..100 {
            assert_eq!(map.push(0), i);
        }
        for i in 0..100 {
            assert_eq!(map.remove(i), Some(0));
        }
        let IdMap { data, freelist } = &map;
        assert_empty(data.iter());
        assert_eq!(freelist, &None);
    }

    #[test]
    fn test_insert_beyond_end_freelist() {
        let mut map = IdMap::new();
        for i in 0..10 {
            assert_eq!(map.insert(2 * i + 1, 0), None);
        }
        for i in 0..10 {
            assert_eq!(map.push(1), 2 * i);
        }
    }

    #[test]
    fn test_double_free() {
        const MAX_KEY: usize = 100;
        let mut map1 = IdMap::new();
        assert_eq!(map1.insert(MAX_KEY, 2), None);
        let mut map2 = IdMap::new();
        assert_eq!(map2.insert(MAX_KEY, 2), None);
        for i in 0..MAX_KEY {
            assert_eq!(map1.remove(i), None);
            // Removing an already free entry should be a no-op.
            assert_eq!(map1.data, map2.data);
            assert_eq!(map1.freelist, map2.freelist);
        }
    }

    #[derive(Debug)]
    enum Operation<K, V> {
        Get { key: K },
        Insert { key: K, value: V },
        Remove { key: K },
        Push { value: V },
    }

    impl<V> Operation<usize, V>
    where
        V: Copy + core::cmp::PartialEq + core::fmt::Debug,
    {
        fn apply(self, map: &mut IdMap<V>, source_of_truth: &mut HashMap<usize, V>) {
            match self {
                Self::Get { key } => {
                    assert_eq!(
                        map.get(key),
                        source_of_truth.get(&key),
                        "key={} map.get == truth.get",
                        key
                    );
                }
                Self::Insert { key, value } => {
                    assert_eq!(
                        map.insert(key, value),
                        source_of_truth.insert(key, value),
                        "key={}, map.insert == truth.insert",
                        key
                    );
                }
                Self::Remove { key } => {
                    assert_eq!(
                        map.remove(key),
                        source_of_truth.remove(&key),
                        "key={} map.remove == truth.remove",
                        key,
                    );
                }
                Self::Push { value } => {
                    let key = map.push(value);
                    assert_eq!(
                        source_of_truth.insert(key, value),
                        None,
                        "pushed key={}, value={:?}",
                        key,
                        value
                    );
                }
            }
        }
    }

    use proptest::strategy::Strategy;

    fn operation_strategy() -> impl Strategy<Value = Operation<usize, i32>> {
        let key_strategy = || 0..20usize;
        // Use a small range for values since we don't do anything fancy with them
        // so a larger range probably won't expose additional issues.
        let value_strategy = || 0..10i32;

        proptest::prop_oneof![
            key_strategy().prop_map(|key| Operation::Get { key }),
            (key_strategy(), value_strategy())
                .prop_map(|(key, value)| Operation::Insert { key, value }),
            key_strategy().prop_map(|key| Operation::Remove { key }),
            value_strategy().prop_map(|value| Operation::Push { value }),
        ]
    }

    /// Searches through the given data entries to identify the free list. Returns the indices of
    /// elements in the free list in order, panicking if there is any inconsistency in the list.
    fn find_free_elements<T>(data: &[IdMapEntry<T>]) -> Vec<usize> {
        let head = data.iter().enumerate().find_map(|(i, e)| match e {
            IdMapEntry::Free(link) => {
                let FreeListLink { prev, next: _ } = link;
                if prev == &None {
                    Some((i, link))
                } else {
                    None
                }
            }
            IdMapEntry::Allocated(_) => None,
        });
        let mut found = Vec::new();
        let mut next = head;

        // Traverse the free list, collecting all indices into `found`.
        while let Some((index, link)) = next {
            found.push(index);
            next = link.next.map(|next_i| {
                let next_free = match &data[next_i] {
                    IdMapEntry::Free(f) => f,
                    IdMapEntry::Allocated(_) => panic!("free list element is not free"),
                };
                assert_eq!(Some(index), next_free.prev, "data[{}] and data[{}]", index, next_i);
                (next_i, next_free)
            })
        }

        // The freelist should contain all of the free data elements.
        data.iter().enumerate().for_each(|(i, e)| match e {
            IdMapEntry::Free(_) => {
                assert!(found.contains(&i), "data[{}] is free but not in the list", i)
            }
            IdMapEntry::Allocated(_) => (),
        });
        found
    }

    #[test]
    fn test_find_free_elements() {
        let data = vec![Allocated(1), free_tail(2), free(3, 1), free_head(2)];
        assert_eq!(find_free_elements(&data), vec![3, 2, 1]);
    }

    #[test]
    fn test_find_free_elements_none_free() {
        let data = vec![Allocated(1), Allocated(2), Allocated(3), Allocated(2)];
        assert_eq!(find_free_elements(&data), vec![]);
    }

    #[test]
    #[should_panic(expected = "not free")]
    fn test_find_free_elements_includes_allocated() {
        let data = vec![Allocated(1), free_head(0), free_tail(0)];
        let _ = find_free_elements(&data);
    }

    #[test]
    #[should_panic(expected = "is free but not in the list")]
    fn test_find_free_elements_in_cycle() {
        let data = vec![free(2, 1), free(0, 2), free(1, 0), Allocated(5)];
        let _ = find_free_elements(&data);
    }

    #[test]
    #[should_panic(expected = "is free but not in the list")]
    fn test_find_free_elements_multiple_lists() {
        let data = vec![free_head(1), free_tail(0), Allocated(13), free_head(4), free_tail(3)];
        let _ = find_free_elements(&data);
    }

    proptest::proptest! {
        #![proptest_config(proptest::test_runner::Config {
            // Add all failed seeds here.
            failure_persistence: proptest_support::failed_seeds!(),
            ..proptest::test_runner::Config::default()
        })]

        #[test]
        fn test_arbitrary_operations(operations in proptest::collection::vec(operation_strategy(), 10)) {
            let mut map = IdMap::new();
            let mut reference = HashMap::new();
            for op in operations {
                op.apply(&mut map, &mut reference);

                // Now check the invariants that the map should be guaranteeing.
                let IdMap {data, freelist} = &map;

                match freelist {
                    None => {
                        // No freelist means all nodes are allocated.
                        data.iter().enumerate().for_each(|(i, d)| match d {
                            IdMapEntry::Free(_) => panic!("no freelist but data[{}] is free", i),
                            IdMapEntry::Allocated(_) => (),
                        })
                    },
                    Some(FreeList {head, tail}) => {
                        let traversed = find_free_elements(data);
                        assert_eq!(traversed.first(), Some(head));
                        assert_eq!(traversed.last(), Some(tail));
                    }
                }
            }

            // After all operations have completed, the contents of the map should match the source of truth.
            let elements : HashMap<_, i32> = map.iter().map(|(a, b)| (a, *b)).collect();
            assert_eq!(elements, reference);
        }

    }
}

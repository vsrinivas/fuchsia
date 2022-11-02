// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use alloc::collections::hash_map::{Entry, HashMap};
use core::{hash::Hash, num::NonZeroUsize};

use nonzero_ext::nonzero;

/// The result of inserting an element into a [`RefCountedHashMap`].
#[cfg_attr(test, derive(Debug, Eq, PartialEq))]
pub(crate) enum InsertResult<O> {
    /// The key was not previously in the map, so it was inserted.
    Inserted(O),
    /// The key was already in the map, so we incremented the entry's reference
    /// count.
    AlreadyPresent,
}

/// The result of removing an entry from a [`RefCountedHashMap`].
#[cfg_attr(test, derive(Debug, Eq, PartialEq))]
pub(crate) enum RemoveResult<V> {
    /// The reference count reached 0, so the entry was removed.
    Removed(V),
    /// The reference count did not reach 0, so the entry still exists in the map.
    StillPresent,
    /// The key was not in the map.
    NotPresent,
}

/// A [`HashMap`] which keeps a reference count for each entry.
#[cfg_attr(test, derive(Debug))]
pub(crate) struct RefCountedHashMap<K, V> {
    inner: HashMap<K, (NonZeroUsize, V)>,
}

impl<K, V> Default for RefCountedHashMap<K, V> {
    fn default() -> RefCountedHashMap<K, V> {
        RefCountedHashMap { inner: HashMap::default() }
    }
}

impl<K: Eq + Hash, V> RefCountedHashMap<K, V> {
    /// Increments the reference count of the entry with the given key.
    ///
    /// If the key isn't in the map, the given function is called to create its
    /// associated value.
    pub(crate) fn insert_with<O, F: FnOnce() -> (V, O)>(
        &mut self,
        key: K,
        f: F,
    ) -> InsertResult<O> {
        match self.inner.entry(key) {
            Entry::Occupied(mut entry) => {
                let (refcnt, _): &mut (NonZeroUsize, V) = entry.get_mut();
                *refcnt = refcnt.checked_add(1).unwrap();
                InsertResult::AlreadyPresent
            }
            Entry::Vacant(entry) => {
                let (value, output) = f();
                let _: &mut (NonZeroUsize, V) = entry.insert((nonzero!(1usize), value));
                InsertResult::Inserted(output)
            }
        }
    }

    /// Decrements the reference count of the entry with the given key.
    ///
    /// If the reference count reaches 0, the entry will be removed and its
    /// value returned.
    pub(crate) fn remove(&mut self, key: K) -> RemoveResult<V> {
        match self.inner.entry(key) {
            Entry::Vacant(_) => RemoveResult::NotPresent,
            Entry::Occupied(mut entry) => {
                let (refcnt, _): &mut (NonZeroUsize, V) = entry.get_mut();
                match NonZeroUsize::new(refcnt.get() - 1) {
                    None => {
                        let (_, value): (NonZeroUsize, V) = entry.remove();
                        RemoveResult::Removed(value)
                    }
                    Some(new_refcnt) => {
                        *refcnt = new_refcnt;
                        RemoveResult::StillPresent
                    }
                }
            }
        }
    }

    /// Returns `true` if the map contains a value for the specified key.
    pub(crate) fn contains_key(&self, key: &K) -> bool {
        self.inner.contains_key(key)
    }

    /// Returns a reference to the value corresponding to the key.
    #[cfg(test)]
    pub(crate) fn get(&self, key: &K) -> Option<&V> {
        self.inner.get(key).map(|(_, value)| value)
    }

    /// Returns a mutable reference to the value corresponding to the key.
    pub(crate) fn get_mut(&mut self, key: &K) -> Option<&mut V> {
        self.inner.get_mut(key).map(|(_, value)| value)
    }

    /// An iterator visiting all key-value pairs in arbitrary order, with
    /// immutable references to the values.
    pub(crate) fn iter<'a>(&'a self) -> impl 'a + Iterator<Item = (&'a K, &'a V)> {
        self.inner.iter().map(|(key, (_, value))| (key, value))
    }

    /// An iterator visiting all key-value pairs in arbitrary order, with
    /// immutable references to the values and the count for each pair.
    #[cfg(test)]
    pub(crate) fn iter_with_counts<'a>(
        &'a self,
    ) -> impl 'a + Iterator<Item = (&'a K, &'a V, NonZeroUsize)> {
        self.inner.iter().map(|(key, (count, value))| (key, value, *count))
    }

    /// An iterator visiting all key-value pairs in arbitrary order, with
    /// mutable references to the values.
    pub(crate) fn iter_mut<'a>(&'a mut self) -> impl 'a + Iterator<Item = (&'a K, &'a mut V)> {
        self.inner.iter_mut().map(|(key, (_, value))| (key, value))
    }
}

/// A [`RefCountedHashMap`] where the value is `()`.
pub(crate) struct RefCountedHashSet<T> {
    inner: RefCountedHashMap<T, ()>,
}

impl<T> Default for RefCountedHashSet<T> {
    fn default() -> RefCountedHashSet<T> {
        RefCountedHashSet { inner: RefCountedHashMap::default() }
    }
}

impl<T: Eq + Hash> RefCountedHashSet<T> {
    /// Increments the reference count of the given value.
    pub(crate) fn insert(&mut self, value: T) -> InsertResult<()> {
        self.inner.insert_with(value, || ((), ()))
    }

    /// Decrements the reference count of the given value.
    ///
    /// If the reference count reaches 0, the value will be removed from the
    /// set.
    pub(crate) fn remove(&mut self, value: T) -> RemoveResult<()> {
        self.inner.remove(value)
    }

    /// Returns `true` if the set contains the given value.
    pub(crate) fn contains(&self, value: &T) -> bool {
        self.inner.contains_key(value)
    }

    /// Returns the number of values in the set.
    #[cfg(test)]
    pub(crate) fn len(&self) -> usize {
        self.inner.inner.len()
    }

    /// Iterates over values and reference counts.
    #[cfg(test)]
    pub(crate) fn iter_counts(&self) -> impl Iterator<Item = (&'_ T, NonZeroUsize)> + '_ {
        self.inner.inner.iter().map(|(key, (count, ()))| (key, *count))
    }
}

impl<T: Eq + Hash> core::iter::FromIterator<T> for RefCountedHashSet<T> {
    fn from_iter<I: IntoIterator<Item = T>>(iter: I) -> Self {
        iter.into_iter().fold(Self::default(), |mut set, t| {
            let _: InsertResult<()> = set.insert(t);
            set
        })
    }
}

#[cfg(test)]
mod test {
    use alloc::format;

    use super::*;

    #[test]
    fn test_ref_counted_hash_map() {
        let mut map = RefCountedHashMap::<&str, ()>::default();
        let key = "key";

        // Test refcounts 1 and 2. The behavioral difference is that testing
        // only with a refcount of 1 doesn't exercise the refcount incrementing
        // functionality - it only exercises the functionality of initializing a
        // new entry with a refcount of 1.
        for refcount in 1..=2 {
            assert!(!map.contains_key(&key));

            // Insert an entry for the first time, initializing the refcount to
            // 1.
            assert_eq!(map.insert_with(key, || ((), ())), InsertResult::Inserted(()));
            assert!(map.contains_key(&key));
            assert_refcount(&map, key, 1, "after initial insert");

            // Increase the refcount to `refcount`.
            for i in 1..refcount {
                // Since the refcount starts at 1, the entry is always already
                // in the map.
                assert_eq!(map.insert_with(key, || ((), ())), InsertResult::AlreadyPresent);
                assert!(map.contains_key(&key));
                assert_refcount(&map, key, i + 1, "after subsequent insert");
            }

            // Decrement the refcount to 1.
            for i in 1..refcount {
                // Since we don't decrement the refcount past 1, the entry is
                // always still present.
                assert_eq!(map.remove(key), RemoveResult::StillPresent);
                assert!(map.contains_key(&key));
                assert_refcount(&map, key, refcount - i, "after decrement refcount");
            }

            assert_refcount(&map, key, 1, "before entry removed");
            // Remove the entry when the refcount is 1.
            assert_eq!(map.remove(key), RemoveResult::Removed(()));
            assert!(!map.contains_key(&key));

            // Try to remove an entry that no longer exists.
            assert_eq!(map.remove(key), RemoveResult::NotPresent);
        }
    }

    fn assert_refcount(
        map: &RefCountedHashMap<&str, ()>,
        key: &str,
        expected_refcount: usize,
        context: &str,
    ) {
        let (actual_refcount, _value) =
            map.inner.get(key).expect(&format!("refcount should be non-zero {}", context));
        assert_eq!(actual_refcount.get(), expected_refcount);
    }
}

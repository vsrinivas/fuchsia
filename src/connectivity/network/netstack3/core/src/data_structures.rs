// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Common data structures.

/// Identifer map data structure.
///
/// Defines the [`IdMap`] data structure: A generic container mapped keyed
/// by an internally managed pool of identifiers kept densily packed.
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
            // invariant: the only way to remove items from IdMap is through
            // the remove method, which compresses the unverlying vec getting
            // rid of unused items, so IdMap should only be empty if
            // data is empty.
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
    }

    impl<T> Default for IdMap<T> {
        fn default() -> Self {
            Self::new()
        }
    }

    #[cfg(test)]
    mod tests {
        use super::IdMap;

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

    }
}

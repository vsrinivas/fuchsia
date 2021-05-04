// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use parking_lot::RwLock;
use std::{
    borrow::Borrow,
    collections::{hash_map::Entry, HashMap},
    hash::Hash,
    sync::atomic::{AtomicBool, Ordering},
    sync::{Arc, Weak},
};

// TODO(fxbug.dev/74427): This would be so so much easier with Arc::new_cyclic.

/// A weak reference to an entry in a DetachableMap. This is a weak reference, if the entry is
/// detached before it is upgraded, the entry can be gone.
pub struct DetachableWeak<K, V> {
    /// Weak reference to the value, upgrading this doesn't guarantee it's in the map, but usually
    /// it is.
    inner: Weak<V>,
    parent: Arc<RwLock<HashMap<K, (Arc<V>, Arc<AtomicBool>)>>>,
    /// Key reference, shared with all DetachableWeaks that reference the same entry in `parent`.
    /// false if anyone detached the entry or the key was replaced in `parent`, to prevent removing
    /// an entry at the same key.
    key_ref: Arc<AtomicBool>,
    /// A copy of `key` that can be used whenever.
    key: K,
}

impl<K: Clone, V> Clone for DetachableWeak<K, V> {
    fn clone(&self) -> Self {
        Self {
            inner: self.inner.clone(),
            parent: self.parent.clone(),
            key_ref: self.key_ref.clone(),
            key: self.key.clone(),
        }
    }
}

impl<K: Hash + Eq, V> DetachableWeak<K, V> {
    fn new(
        item: &Arc<V>,
        parent: Arc<RwLock<HashMap<K, (Arc<V>, Arc<AtomicBool>)>>>,
        key: K,
        key_ref: Arc<AtomicBool>,
    ) -> Self {
        Self { inner: Arc::downgrade(item), parent, key, key_ref }
    }

    /// Attempt to upgrade the weak pointer to an Arc, extending the lifetime of the value if
    /// successful.  Returns None if the item has been dropped (by another client detaching this)
    pub fn upgrade(&self) -> Option<Arc<V>> {
        self.inner.upgrade()
    }

    /// Destroys the original reference to this vended item.
    /// If other references to the item exist (from `upgrade`), it will not be dropped until those
    /// references are dropped.
    pub fn detach(self) {
        let mut lock = self.parent.write();
        if self.key_ref.swap(false, Ordering::Relaxed) {
            // Dropping this reference only frees the key_ref and value if no other references are
            // held.
            let _ = lock.remove(&self.key);
        }
    }

    /// Get a reference to the key for this entry.
    pub fn key(&self) -> &K {
        &self.key
    }
}

pub struct LazyEntry<K, V> {
    parent: Arc<RwLock<HashMap<K, (Arc<V>, Arc<AtomicBool>)>>>,
    key: K,
}

impl<K: Clone, V> Clone for LazyEntry<K, V> {
    fn clone(&self) -> Self {
        Self { parent: self.parent.clone(), key: self.key.clone() }
    }
}

impl<K: Hash + Eq + Clone, V> LazyEntry<K, V> {
    fn new(parent: Arc<RwLock<HashMap<K, (Arc<V>, Arc<AtomicBool>)>>>, key: K) -> Self {
        Self { parent, key }
    }

    /// Get a reference to the key that this entry is built for.
    pub fn key(&self) -> &K {
        &self.key
    }

    /// Attempt to insert into the map at `key`. Returns a detachable weak entry if the value was
    /// inserted, and Err(value) if the item already existed.
    /// Err(value) if the value was not able to be inserted.
    pub fn try_insert(&self, value: V) -> Result<DetachableWeak<K, V>, V> {
        match self.parent.write().entry(self.key.clone()) {
            Entry::Occupied(_) => return Err(value),
            Entry::Vacant(entry) => {
                let _ = entry.insert((Arc::new(value), Arc::new(AtomicBool::new(true))));
            }
        };
        Ok(self.get().unwrap())
    }

    /// Attempt to resolve the entry to a weak reference, as returned by `DetachableMap::get`.
    /// Returns None if the key does not exist.
    pub fn get(&self) -> Option<DetachableWeak<K, V>> {
        self.parent.read().get(&self.key).and_then(|(v, key_ref)| {
            let map = self.parent.clone();
            let key_ref = key_ref.clone();
            let key = self.key.clone();
            Some(DetachableWeak::new(&v, map, key, key_ref))
        })
    }
}

/// A Map with detachable entries.  After retrieval, entries can be "detached", removing them from
/// the map, allowing any client to expire the key in the map.  They are weak, and can be upgraded
/// to a strong reference to the stored object.
pub struct DetachableMap<K, V> {
    /// The map. For each key, it holds a reference to the value, and a shared bool indicating if
    /// the paired value is still in the map.
    map: Arc<RwLock<HashMap<K, (Arc<V>, Arc<AtomicBool>)>>>,
}

impl<K: Hash + Eq + Clone, V> Default for DetachableMap<K, V> {
    fn default() -> Self {
        Self { map: Arc::new(RwLock::new(HashMap::new())) }
    }
}

impl<K: Hash + Eq + Clone, V> DetachableMap<K, V> {
    /// Creates an empty `DetachableMap`.   The map is initially empty.
    pub fn new() -> DetachableMap<K, V> {
        Default::default()
    }

    /// Inserts a new item into the map at `key`
    /// Returns a reference to the old item at `key` if one existed or None otherwise.
    pub fn insert(&mut self, key: K, value: V) -> Option<Arc<V>> {
        let mut lock = self.map.write();
        let new_pair = (Arc::new(value), Arc::new(AtomicBool::new(true)));
        if let Some((prev_val, old_key_ref)) = lock.insert(key, new_pair) {
            old_key_ref.store(false, Ordering::Relaxed);
            Some(prev_val)
        } else {
            None
        }
    }

    /// True if the map contains a value for the specified key  The key may be any borrowed form of
    /// the key's type, with `Hash` and `Eq` matching the type.
    pub fn contains_key<Q: ?Sized>(&self, key: &Q) -> bool
    where
        K: Borrow<Q>,
        Q: Hash + Eq,
    {
        self.map.read().contains_key(key)
    }

    /// Returns a detachable reference to the value at the given key, if it exists.
    pub fn get(&self, key: &K) -> Option<DetachableWeak<K, V>> {
        self.map.read().get(key).and_then(|(v, key_ref)| {
            let map = self.map.clone();
            let key_ref = key_ref.clone();
            let key = key.clone();
            Some(DetachableWeak::new(&v, map, key, key_ref))
        })
    }

    /// Returns a lazy entry. Lazy Entries can be used later to attempt to insert into the map if
    /// the key doesn't exist.
    /// They can also be resolved to a detachable reference (as returned by `DetachableMap::get`) if
    /// the key already exists.
    pub fn lazy_entry(&self, key: &K) -> LazyEntry<K, V> {
        LazyEntry::new(self.map.clone(), key.clone())
    }
}

#[cfg(test)]
mod test {
    use super::*;

    #[derive(Default, Debug, PartialEq)]
    struct TestStruct {
        data: u32,
    }

    #[test]
    fn contains_keys() {
        let mut map = DetachableMap::default();

        assert!(map.insert(0, TestStruct { data: 45 }).is_none());

        assert!(map.contains_key(&0));

        let detached = map.get(&0);
        assert!(detached.is_some());
        // Detaching removes it from the map.
        detached.unwrap().detach();

        assert_eq!(false, map.contains_key(&0));
    }

    #[test]
    fn upgrade_detached() {
        let mut map = DetachableMap::default();

        assert!(map.insert(0, TestStruct { data: 45 }).is_none());

        let detached = map.get(&0);
        assert!(detached.is_some());
        let detached = detached.unwrap();
        let detached_clone = detached.clone();

        let second = map.get(&0);
        assert!(second.is_some());
        let second = second.unwrap();

        let upgraded = detached.upgrade().expect("should be able to upgrade");

        // Detaching should mean we can't get it from the map anymore (and consumes second)
        second.detach();

        assert!(map.get(&0).is_none());

        // We can still upgrade because it's still around from the strong ref.
        let second_up = detached.upgrade().expect("should be able to upgrade");
        let third_up = detached_clone.upgrade().expect("should be able to upgrade");

        // Dropping all the strong refs means neither can upgrade anymore though.
        drop(upgraded);
        drop(second_up);
        drop(third_up);

        assert!(detached.upgrade().is_none());

        // Detaching twice doesn't do anything (and doesn't panic)
        detached.detach();
    }

    #[test]
    fn cant_detach_replaced_key() {
        let mut map = DetachableMap::default();

        assert!(map.insert(0, TestStruct { data: 12 }).is_none());

        let detached = map.get(&0).expect("key is in");

        // Replace the struct in the original map, keeping the data around.
        let replaced = map.insert(0, TestStruct { data: 34 });

        // Can still upgrade to the old data, since it's still alive.
        assert_eq!(TestStruct { data: 12 }, *detached.upgrade().expect("data is still alive"));

        // dropping the old data means upgrade fails though.
        drop(replaced);

        assert!(detached.upgrade().is_none());

        // Trying to detach from an earlier generation will not remove the new data.
        detached.detach();

        let new_detached = map.get(&0).expect("key was replaced");

        assert_eq!(TestStruct { data: 34 }, *new_detached.upgrade().expect("should be there"));

        // Detaching from a cloned one has no effect, even if the key was re-added before we try.
        let cloned = new_detached.clone();
        let another = map.get(&0).expect("still there");

        new_detached.detach();

        assert!(map.get(&0).is_none());

        // We don't replace any data, it got detached above.
        let replaced = map.insert(0, TestStruct { data: 45 });
        assert!(replaced.is_none());

        // This doesn't remove the new data either though.
        cloned.detach();
        assert!(map.get(&0).is_some());
        // Even if we didn't clone it from the same DetachedWeak.
        another.detach();
        assert!(map.get(&0).is_some());
    }

    #[test]
    fn lazy_entry() {
        let map = DetachableMap::default();

        // Should be able to get an entry before the key exists.
        let entry = map.lazy_entry(&1);

        // Can't get a reference if the key doesn't exist.
        assert!(entry.get().is_none());

        // We can insert though.
        let detachable =
            entry.try_insert(TestStruct { data: 45 }).expect("should be able to insert");

        // Can't insert if there's something there though.
        let second_val = TestStruct { data: 56 };
        let returned_val =
            entry.try_insert(second_val).err().expect("should get an error when trying to insert");
        assert_eq!(56, returned_val.data);

        assert!(entry.get().is_some());

        // If we detach though, the entry is empty again, and we can insert again.
        detachable.detach();

        assert!(entry.get().is_none());

        let new = entry.try_insert(returned_val).expect("should be able to insert after removal");

        // Deopping the new entry doesn't remove it from the map.
        drop(new);

        let still_there = map.get(&1).expect("should be there");
        assert!(still_there.upgrade().is_some());
    }
}

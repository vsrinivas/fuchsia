// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use parking_lot::RwLock;
use std::{
    borrow::Borrow,
    collections::HashMap,
    hash::Hash,
    sync::{Arc, Weak},
};

pub struct DetachableWeak<K, V> {
    inner: Weak<V>,
    parent: Arc<RwLock<HashMap<K, Arc<V>>>>,
    key: K,
}

impl<K: Hash + Eq, V> DetachableWeak<K, V> {
    pub fn new(item: &Arc<V>, parent: Arc<RwLock<HashMap<K, Arc<V>>>>, key: K) -> Self {
        Self { inner: Arc::downgrade(item), parent, key }
    }

    pub fn upgrade(&self) -> Option<Arc<V>> {
        self.inner.upgrade()
    }

    /// Destroys the original reference to this vended item.
    /// If other references to the item exist (from `upgrade`),
    /// it will not be dropped until those references are dropped.
    pub fn detach(self) {
        self.parent.write().remove(&self.key);
    }
}

pub struct DetachableMap<K, V> {
    map: Arc<RwLock<HashMap<K, Arc<V>>>>,
}

impl<K: Hash + Eq + Clone, V> DetachableMap<K, V> {
    pub fn new() -> Self {
        Self { map: Arc::new(RwLock::new(HashMap::new())) }
    }

    /// Inserts a new item into the map at `key`
    /// Returns a reference to the old item at `key` if one existed,
    /// or None otherwise.
    pub fn insert(&mut self, key: K, value: V) -> Option<Arc<V>> {
        self.map.write().insert(key, Arc::new(value))
    }

    /// True if the map contains a key at `key`
    pub fn contains_key<Q: ?Sized>(&self, key: &Q) -> bool
    where
        K: Borrow<Q>,
        Q: Hash + Eq,
    {
        self.map.read().contains_key(key)
    }

    /// Returns a detachable reference to the value at the given key, if it exists.
    pub fn get(&self, key: &K) -> Option<DetachableWeak<K, V>> {
        self.map.read().get(key).and_then(|v| {
            let map = self.map.clone();
            let key = key.clone();
            Some(DetachableWeak::new(&v, map, key))
        })
    }
}

#[cfg(test)]
mod test {
    use super::*;

    #[derive(Default, Debug)]
    struct TestStruct {
        data: u32,
    }

    #[test]
    fn contains_keys() {
        let mut map = DetachableMap::new();

        map.insert(0, TestStruct { data: 45 });

        assert!(map.contains_key(&0));

        let detached = map.get(&0);
        assert!(detached.is_some());
        // Detaching removes it from the map.
        detached.unwrap().detach();

        assert_eq!(false, map.contains_key(&0));
    }

    #[test]
    fn upgrade_detached() {
        let mut map = DetachableMap::new();

        map.insert(0, TestStruct { data: 45 });

        let detached = map.get(&0);
        assert!(detached.is_some());
        let detached = detached.unwrap();

        let second = map.get(&0);
        assert!(second.is_some());
        let second = second.unwrap();

        let upgraded = detached.upgrade().expect("should be able to upgrade");

        // Detaching should mean we can't get it from the map anymore (and consumes second)
        second.detach();

        assert!(map.get(&0).is_none());

        // We can still upgrade because it's stlll around from the strong ref.
        let second_up = detached.upgrade().expect("should be able to upgrade");

        // Dropping all the strong refs means neither can upgrade anymore though.
        drop(upgraded);
        drop(second_up);

        assert!(detached.upgrade().is_none());

        // Detatching twice doesn't do anyting (and doesn't panic)
        detached.detach();
    }
}

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(dead_code)]

use std::collections::{hash_map, HashMap};
use std::hash::Hash;

pub enum WatcherResult {
    KeepWatching,
    StopWatching
}

impl WatcherResult {
    fn should_keep_watching(self) -> bool {
        match self {
            WatcherResult::KeepWatching => true,
            WatcherResult::StopWatching => false,
        }
    }
}

pub trait MapWatcher<K> {
    fn on_add_key(&self, key: &K) -> WatcherResult;
    fn on_remove_key(&self, key: &K) -> WatcherResult;
}

pub struct WatchableMap<K, V, W> {
    map: HashMap<K, V>,
    watchers: HashMap<u64, W>,
    next_watcher_id: u64,
}

impl<K, V, W> WatchableMap<K, V, W>
    where K: Clone + Hash + Eq, W: MapWatcher<K>
{
    pub fn new() -> Self {
        WatchableMap {
            map: HashMap::new(),
            watchers: HashMap::new(),
            next_watcher_id: 0,
        }
    }

    pub fn insert(&mut self, key: K, value: V) {
        self.map.insert(key.clone(), value);
        self.watchers.retain(|_, w| w.on_add_key(&key).should_keep_watching());
    }

    pub fn remove(&mut self, key: &K) {
        if self.map.remove(key).is_some() {
            self.watchers.retain(|_, w| w.on_remove_key(&key).should_keep_watching());
        }
    }

    pub fn add_watcher(&mut self, watcher: W) -> Option<u64> {
        for key in self.map.keys() {
            if !watcher.on_add_key(key).should_keep_watching() {
                return None;
            }
        }
        let watcher_id = self.next_watcher_id;
        self.next_watcher_id += 1;
        self.watchers.insert(watcher_id, watcher);
        Some(watcher_id)
    }

    pub fn remove_watcher(&mut self, id: u64) {
        self.watchers.remove(&id);
    }

    pub fn iter(&self) -> hash_map::Iter<K, V> {
        self.map.iter()
    }

    pub fn get<Q: ?Sized>(&self, k: &Q) -> Option<&V>
        where K: ::std::borrow::Borrow<Q>, Q: Hash + Eq
    {
        self.map.get(k)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::cell::RefCell;
    use std::rc::Rc;

    #[derive(Debug, Eq, PartialEq)]
    enum Event {
        Add(u16),
        Remove(u16)
    }

    impl MapWatcher<u16> for Rc<RefCell<Vec<Event>>> {
        fn on_add_key(&self, key: &u16) -> WatcherResult {
            self.borrow_mut().push(Event::Add(*key));
            return if key % 2 == 1 { WatcherResult::KeepWatching } else { WatcherResult::StopWatching };
        }
        fn on_remove_key(&self, key: &u16) -> WatcherResult {
            self.borrow_mut().push(Event::Remove(*key));
            return if key % 2 == 1 { WatcherResult::KeepWatching } else { WatcherResult::StopWatching };
        }
    }

    #[test]
    fn insert_get_remove() {
        let mut map = WatchableMap::<_, _, Rc<RefCell<Vec<Event>>>>::new();
        map.insert(3u16, "foo");
        assert_eq!(Some(&"foo"), map.get(&3u16));
        map.remove(&3u16);
        assert_eq!(None, map.get(&3u16));
    }

    #[test]
    fn notified_on_insert_and_remove() {
        let mut map = WatchableMap::new();
        let watcher = Rc::new(RefCell::new(Vec::<Event>::new()));
        map.add_watcher(watcher.clone());
        map.insert(3u16, "foo");
        map.insert(7u16, "bar");
        map.remove(&3u16);
        assert_eq!(*watcher.borrow(), vec![Event::Add(3u16), Event::Add(7u16), Event::Remove(3u16)]);
    }

    #[test]
    fn not_notified_after_returning_stop() {
        let mut map = WatchableMap::new();
        let watcher = Rc::new(RefCell::new(Vec::<Event>::new()));
        map.add_watcher(watcher.clone());
        map.insert(3u16, "foo");
        // Our watcher returns 'StopWatching' for even numbers
        map.insert(4u16, "bar");
        map.insert(7u16, "baz");
        // 7 should not be recorded
        assert_eq!(*watcher.borrow(), vec![Event::Add(3u16), Event::Add(4u16)]);
    }

    #[test]
    fn not_notified_after_removing_watcher() {
        let mut map = WatchableMap::new();
        let watcher = Rc::new(RefCell::new(Vec::<Event>::new()));
        let watcher_id = map.add_watcher(watcher.clone()).expect("add_watcher returned None");
        map.insert(3u16, "foo");
        map.insert(5u16, "bar");
        map.remove_watcher(watcher_id);
        map.insert(7u16, "baz");
        // 7 should not be recorded
        assert_eq!(*watcher.borrow(), vec![Event::Add(3u16), Event::Add(5u16)]);
    }

    #[test]
    fn notified_of_existing_keys() {
        let mut map = WatchableMap::new();
        map.insert(3u16, "foo");
        map.insert(5u16, "bar");
        let watcher = Rc::new(RefCell::new(Vec::<Event>::new()));
        map.add_watcher(watcher.clone()).expect("add_watcher returned None");
        let mut sorted = watcher.borrow().iter().map(|e| {
            match e {
                Event::Add(id) => *id,
                Event::Remove(id) => panic!("Unexpected Remove event with id {}", id),
            }
        }).collect::<Vec<_>>();
        sorted.sort();
        assert_eq!(sorted, vec![3u16, 5u16]);
    }

    #[test]
    fn watcher_not_added_if_returned_stop() {
        let mut map = WatchableMap::new();
        map.insert(4u16, "foo");
        let watcher = Rc::new(RefCell::new(Vec::<Event>::new()));
        assert_eq!(None, map.add_watcher(watcher.clone()));
    }
}

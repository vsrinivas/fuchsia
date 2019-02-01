// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use futures::channel::mpsc::{self, UnboundedReceiver, UnboundedSender};
use parking_lot::Mutex;
use std::collections::HashMap;
use std::hash::Hash;
use std::sync::Arc;

/// A synchronized hash map that serializes all mutations into
/// an unbounded queue of events.
///
/// Also supports taking snapshots of its current state and pushing
/// them into the queue.
///
/// All mutations and snapshots are totally ordered, and events in the
/// queue are guaranteed to follow the same order.
pub struct WatchableMap<K, V>
where
    K: Hash + Eq,
{
    inner: Mutex<Inner<K, V>>,
}

#[derive(Debug)]
pub enum MapEvent<K, V>
where
    K: Hash + Eq,
{
    KeyInserted(K),
    KeyRemoved(K),
    Snapshot(Arc<HashMap<K, Arc<V>>>),
}

struct Inner<K, V>
where
    K: Hash + Eq,
{
    // Storing the map in an Arc allows us to use copy-on-write:
    // taking a snapshot is simply cloning an Arc, and all mutations
    // use Arc::make_mut() which avoids a copy if no snapshots
    // of the current state are alive.
    map: Arc<HashMap<K, Arc<V>>>,
    sender: UnboundedSender<MapEvent<K, V>>,
}

impl<K, V> WatchableMap<K, V>
where
    K: Clone + Hash + Eq,
{
    /// Returns an empty map and the receiving end of the event queue
    pub fn new() -> (Self, UnboundedReceiver<MapEvent<K, V>>) {
        let (sender, receiver) = mpsc::unbounded();
        let map =
            WatchableMap { inner: Mutex::new(Inner { map: Arc::new(HashMap::new()), sender }) };
        (map, receiver)
    }

    // Insert an element and push a KeyInserted event to the queue
    pub fn insert(&self, key: K, value: V) {
        let mut inner = self.inner.lock();
        Arc::make_mut(&mut inner.map).insert(key.clone(), Arc::new(value));
        inner
            .sender
            .unbounded_send(MapEvent::KeyInserted(key))
            .expect("failed to enqueue KeyInserted");
    }

    // Remove an element and push a KeyRemoved event to the queue
    pub fn remove(&self, key: &K) {
        let mut inner = self.inner.lock();
        Arc::make_mut(&mut inner.map).remove(key);
        inner
            .sender
            .unbounded_send(MapEvent::KeyRemoved(key.clone()))
            .expect("failed to enqueue KeyRemoved");
    }

    /// Take a snapshot and push it to the queue
    pub fn request_snapshot(&self) {
        let inner = self.inner.lock();
        inner
            .sender
            .unbounded_send(MapEvent::Snapshot(inner.map.clone()))
            .expect("failed to enqueue Snapshot");
    }

    /// Get a snapshot without pushing it to the queue
    pub fn get_snapshot(&self) -> Arc<HashMap<K, Arc<V>>> {
        self.inner.lock().map.clone()
    }

    pub fn get<Q: ?Sized>(&self, k: &Q) -> Option<Arc<V>>
    where
        K: ::std::borrow::Borrow<Q>,
        Q: Hash + Eq,
    {
        self.inner.lock().map.get(k).map(|v| v.clone())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn insert_remove_get() {
        let (map, _recv) = WatchableMap::new();
        map.insert(3u16, "foo");
        assert_eq!("foo", *map.get(&3u16).expect("expected a value"));
        map.remove(&3u16);
        assert_eq!(None, map.get(&3u16));
    }

    #[test]
    fn get_snapshot() {
        let (map, _recv) = WatchableMap::new();
        map.insert(3u16, "foo");
        let snapshot_one = map.get_snapshot();
        map.remove(&3u16);
        map.insert(4u16, "bar");
        let snapshot_two = map.get_snapshot();

        assert_eq!(
            vec![(3u16, "foo")],
            snapshot_one.iter().map(|(k, v)| (*k, **v)).collect::<Vec<_>>()
        );
        assert_eq!(
            vec![(4u16, "bar")],
            snapshot_two.iter().map(|(k, v)| (*k, **v)).collect::<Vec<_>>()
        );
    }

    #[test]
    fn events() {
        let (map, mut recv) = WatchableMap::new();
        map.insert(3u16, "foo");
        match recv.try_next() {
            Ok(Some(MapEvent::KeyInserted(3u16))) => {}
            other => panic!("expected KeyInserted(3), got {:?}", other),
        }

        map.request_snapshot();
        let snapshot_one = match recv.try_next() {
            Ok(Some(MapEvent::Snapshot(s))) => s,
            other => panic!("expected Snapshot, got {:?}", other),
        };

        map.remove(&3u16);
        match recv.try_next() {
            Ok(Some(MapEvent::KeyRemoved(3u16))) => {}
            other => panic!("expected KeyRemoved(3), got {:?}", other),
        }

        map.insert(4u16, "bar");
        match recv.try_next() {
            Ok(Some(MapEvent::KeyInserted(4u16))) => {}
            other => panic!("expected KeyInserted(4), got {:?}", other),
        }

        map.request_snapshot();
        let snapshot_two = match recv.try_next() {
            Ok(Some(MapEvent::Snapshot(s))) => s,
            other => panic!("expected Snapshot, got {:?}", other),
        };

        assert!(recv.try_next().is_err());

        assert_eq!(
            vec![(3u16, "foo")],
            snapshot_one.iter().map(|(k, v)| (*k, **v)).collect::<Vec<_>>()
        );
        assert_eq!(
            vec![(4u16, "bar")],
            snapshot_two.iter().map(|(k, v)| (*k, **v)).collect::<Vec<_>>()
        );
    }
}

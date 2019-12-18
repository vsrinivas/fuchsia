// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{collections::vec_deque, iter::FromIterator};

use crate::types::MAX_NOTIFICATION_EVENT_QUEUE_SIZE;

#[allow(unused)]
/// Mutable Iterator over the elements in a `BoundedQueue`
type IterMut<'a, T> = vec_deque::IterMut<'a, T>;

/// A `BoundedQueue` is a collection that holds elements with eviction minimums on the size (in
/// quantity) of the collection. If the `max_size` is exceeded, the oldest
/// element will be dropped to make room for the new element.
///
/// The `BoundedQueue` only supports inserting new elements to the end of the queue.
#[derive(Debug, Clone)]
pub(crate) struct BoundedQueue<T> {
    max_size: usize,
    current_count: usize,
    /// A monotonically increasing count of the total number of items inserted over the lifetime of
    /// the queue.
    monotonic_count: usize,
    inner: vec_deque::VecDeque<T>,
}
impl<T> BoundedQueue<T> {
    /// Create an empty `BoundedQueue` with the specified eviction minimums.
    pub fn new(max_size: usize) -> BoundedQueue<T> {
        BoundedQueue {
            max_size,
            current_count: 0,
            monotonic_count: 0,
            inner: vec_deque::VecDeque::new(),
        }
    }
    /// Calculate whether adding `item` would make this queue large enough to evict old items.
    fn max_size_reached(&self) -> bool {
        self.current_count == self.max_size
    }

    /// Insert a new item to the end of the `BoundedQueue`, removing an item as needed to make room.
    /// If an item is removed, it is returned.
    pub fn insert(&mut self, item: T) -> Option<T> {
        let mut evicted = None;
        if self.max_size_reached() {
            self.current_count -= self
                .inner
                .pop_front()
                .map(|ev| {
                    evicted = Some(ev);
                    1
                })
                .unwrap_or(0);
        }
        self.monotonic_count += 1;
        self.current_count += 1;
        self.inner.push_back(item);

        evicted
    }

    /// Remove an element from the `BoundedQueue`.
    /// If the `item` is present, decrement the size and return the removed element.
    /// If the `item` is not present, return None.
    #[cfg(test)]
    pub fn remove(&mut self, index: usize) -> Option<T> {
        self.inner.remove(index).map(|item| {
            self.current_count -= 1;
            item
        })
    }

    /// Return an `Iterator` over mutable references to elements ordered from oldest to newest.
    #[cfg(test)]
    pub fn iter_mut(&mut self) -> IterMut<'_, T> {
        self.inner.iter_mut()
    }

    /// A monotonic count of the number of items that have been inserted into the data structure
    /// over the lifetime of the queue. The count is 1 after the first element is inserted.
    #[cfg(test)]
    pub fn get_monotonic_count(&self) -> usize {
        self.monotonic_count
    }
    #[cfg(test)]
    /// Returns the number of elements in the queue
    pub fn len(&self) -> usize {
        self.inner.len()
    }
}

impl<T> IntoIterator for BoundedQueue<T> {
    type Item = T;
    type IntoIter = vec_deque::IntoIter<Self::Item>;

    fn into_iter(self) -> Self::IntoIter {
        self.inner.into_iter()
    }
}

impl<T> FromIterator<T> for BoundedQueue<T> {
    fn from_iter<I: IntoIterator<Item = T>>(iter: I) -> Self {
        let mut queue = BoundedQueue::new(MAX_NOTIFICATION_EVENT_QUEUE_SIZE);

        for i in iter {
            queue.insert(i);
        }
        queue
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    // Dummy struct that implements the SizeOf trait required for elements of
    // a `BoundedQueue`
    #[derive(Debug, PartialEq, Clone)]
    struct Record {
        value: u64,
    }
    impl From<u64> for Record {
        fn from(value: u64) -> Self {
            Record { value: value }
        }
    }
    // Return a vec of the values of a record. For use in asserting the state of a `BoundedQueue` in
    // tests.
    fn to_values(queue: &mut BoundedQueue<Record>) -> Vec<u64> {
        queue.iter_mut().map(|n| n.value).collect()
    }

    #[test]
    fn test_bounded_queue() {
        // Normal case, insertions and deletions.
        let mut queue: BoundedQueue<Record> = BoundedQueue::new(3);
        assert!(!queue.max_size_reached());
        assert_eq!(to_values(&mut queue), vec![]);
        assert_eq!(queue.len(), 0);
        assert_eq!(queue.get_monotonic_count(), 0);
        let res0 = queue.insert(0.into());
        assert!(res0.is_none());
        assert!(!queue.max_size_reached());
        assert_eq!(queue.get_monotonic_count(), 1);
        let res1 = queue.insert(1.into());
        let res2 = queue.insert(2.into());
        assert!(res1.is_none());
        assert!(res2.is_none());
        assert_eq!(queue.get_monotonic_count(), 3);
        assert!(queue.max_size_reached());
        assert_eq!(to_values(&mut queue), vec![0, 1, 2]);
        assert_eq!(queue.len(), 3);
        let res4 = queue.insert(3.into());
        assert!(res4.is_some());
        assert!(queue.max_size_reached());
        assert_eq!(to_values(&mut queue), vec![1, 2, 3]);
        queue.insert(4.into());
        assert_eq!(to_values(&mut queue), vec![2, 3, 4]);
        assert_eq!(queue.len(), 3);
        assert_eq!(queue.get_monotonic_count(), 5);

        // Create a queue with some size and test inserting elements into it.
        // Test removing elements as well.
        let mut queue: BoundedQueue<Record> = BoundedQueue::new(3);
        assert_eq!(to_values(&mut queue), vec![]);
        queue.insert(0.into());
        queue.insert(1.into());
        queue.insert(2.into());
        assert_eq!(to_values(&mut queue), vec![0, 1, 2]);
        queue.insert(3.into());
        assert_eq!(to_values(&mut queue), vec![1, 2, 3]);
        // The expected behavior is to evict old items as new ones are added
        assert!(queue.max_size_reached());
        queue.insert(4.into());
        assert_eq!(to_values(&mut queue), vec![2, 3, 4]);
        queue.insert(6.into());
        assert_eq!(to_values(&mut queue), vec![3, 4, 6]);
        // Evict the oldest element (front of queue) success.
        let evicted1 = queue.remove(0);
        assert_eq!(evicted1, Some(3.into()));
        assert_eq!(to_values(&mut queue), vec![4, 6]);
        assert_eq!(queue.len(), 2);
        assert_eq!(queue.get_monotonic_count(), 6);
        // Try to evict an invalid index.
        let evicted2 = queue.remove(5);
        assert!(evicted2.is_none());
        assert_eq!(to_values(&mut queue), vec![4, 6]);
        assert_eq!(queue.len(), 2);
        queue.insert(7.into());
        assert_eq!(to_values(&mut queue), vec![4, 6, 7]);
        // Evict element in the middle.
        let evicted3 = queue.remove(1);
        assert_eq!(to_values(&mut queue), vec![4, 7]);
        assert_eq!(evicted3, Some(6.into()));
        // Evict remaining elements.
        queue.remove(0);
        queue.remove(0);
        assert_eq!(queue.len(), 0);
        assert_eq!(to_values(&mut queue), vec![]);
        // Evict index from an empty queue.
        let evicted6 = queue.remove(0);
        assert_eq!(queue.len(), 0);
        assert_eq!(to_values(&mut queue), vec![]);
        assert_eq!(evicted6, None);
    }
}

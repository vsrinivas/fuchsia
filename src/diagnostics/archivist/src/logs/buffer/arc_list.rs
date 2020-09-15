// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use futures::prelude::*;
// we use parking_lot here instead of futures::lock because Cursor::get_next is recursive
use parking_lot::Mutex;
use std::{
    default::Default,
    pin::Pin,
    sync::{Arc, Weak},
    task::{Context, Poll},
};

/// A singly-linked-list which allows for concurrent lazy iteration and mutation of its
/// contents.
pub struct ArcList<T> {
    inner: Arc<Mutex<Root<T>>>,
}

impl<T> Default for ArcList<T> {
    fn default() -> Self {
        Self { inner: Arc::new(Mutex::new(Default::default())) }
    }
}

impl<T> ArcList<T> {
    pub fn push_back(&self, item: T) {
        self.inner.lock().push_back(item);
    }

    pub fn pop_front(&self) -> Option<Arc<T>> {
        self.inner.lock().pop_front()
    }

    pub fn snapshot(&self) -> Cursor<T> {
        Cursor::new_snapshot(self.clone())
    }
}

impl<T> Clone for ArcList<T> {
    fn clone(&self) -> Self {
        Self { inner: self.inner.clone() }
    }
}

struct Node<T> {
    /// The value of `ArcList::entries_seen` when this node was added to the list. IDs start at 1.
    id: u64,
    /// The value stored.
    inner: Arc<T>,
    /// The next node in the list.
    next: Mutex<Option<Arc<Node<T>>>>,
}

impl<T> Node<T> {
    fn next(&self) -> Weak<Node<T>> {
        self.next.lock().as_ref().map(Arc::downgrade).unwrap_or_default()
    }
}

struct Root<T> {
    head: Option<Arc<Node<T>>>,
    tail: Weak<Node<T>>,
    /// The number of entries ever inserted into the list.
    entries_seen: u64,
    /// The number of entries ever removed from the list.
    entries_popped: u64,
}

impl<T> Default for Root<T> {
    fn default() -> Self {
        Self { head: None, tail: Weak::new(), entries_seen: 0, entries_popped: 0 }
    }
}

impl<T> Root<T> {
    fn push_back(&mut self, item: T) {
        self.entries_seen += 1;
        let new_node =
            Arc::new(Node { id: self.entries_seen, inner: Arc::new(item), next: Mutex::new(None) });
        let new_tail = Arc::downgrade(&new_node);

        if let Some(prev_tail) = self.tail.upgrade() {
            *prev_tail.next.lock() = Some(new_node);
        } else {
            assert!(self.head.is_none(), "if tail is empty then head must be too");
            self.head = Some(new_node);
        }

        self.tail = new_tail;
    }

    fn pop_front(&mut self) -> Option<Arc<T>> {
        self.entries_popped += 1;
        let prev_front = self.head.take();
        if let Some(prev) = prev_front.as_ref() {
            self.head = prev.next.lock().clone();
        }
        prev_front.map(|f| f.inner.clone())
    }
}

/// A weak pointer into the buffer which is being concurrently iterated and modified.
///
/// A cursor iterates over nodes in the linked list, holding a weak pointer to the next node. If
/// that pointer turns out to be stale the cursor starts again at the beginning of the list and
/// first returns a count of items dropped.
///
/// The count is maintained by giving each successive item in a list a monotonically increasing ID
/// and tracking the "high-water mark" of the largest/last ID seen. These IDs are also how we
/// control snapshotting vs. subscribing. A snapshot cursor is bound to those IDs known at the time
/// of its creation.
///
/// # Wraparound
///
/// IDs are stored as `u64` and the list would need to receive a new entry once every nanosecond
/// for ~580 consecutive years before ID allocations will wrap around. This eventuality is *not*
/// accounted for in the implementation. If you're reading this after observing a bug due to that,
/// congratulations.
pub struct Cursor<T> {
    next: Weak<Node<T>>,
    last_id_seen: u64,
    until_id: u64,
    list: ArcList<T>,
}

impl<T> Cursor<T> {
    /// A snapshot cursor will return items until the ID is greater than the tail ID when the
    /// snapshot was taken.
    fn new_snapshot(list: ArcList<T>) -> Self {
        let until_id = list.inner.lock().tail.upgrade().map(|t| t.id).unwrap_or_default();
        Self::new(list, 0, until_id)
    }

    /// A cursor will return results with IDs equal to or greater than `from` and less than or
    /// equal to `to`.
    fn new(list: ArcList<T>, from: u64, to: u64) -> Self {
        Self { list, last_id_seen: from, until_id: to, next: Default::default() }
    }
}

impl<T> Stream for Cursor<T> {
    type Item = LazyItem<T>;
    fn poll_next(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        if self.last_id_seen >= self.until_id {
            return Poll::Ready(None);
        }

        if let Some(to_return) = self.next.upgrade() {
            // self.next is alive
            if to_return.id > self.until_id {
                self.last_id_seen = to_return.id;
                // we're past the end of this cursor's valid range
                return Poll::Ready(None);
            }

            // the number we missed is equal to the difference between
            // the last ID we saw and the ID *just before* the current value
            let num_missed = (to_return.id - 1) - self.last_id_seen;
            let item = if num_missed > 0 {
                // advance the cursor's high-water mark by the number we missed
                // so we only report each dropped item once
                self.last_id_seen += num_missed;
                LazyItem::ItemsDropped(num_missed)
            } else {
                // we haven't missed anything, proceed normally
                self.last_id_seen = to_return.id;
                self.next = to_return.next();
                LazyItem::Next(to_return.inner.clone())
            };

            Poll::Ready(Some(item))
        } else {
            // self.next is stale, either we fell off the list or this is our first call
            let head = self.list.inner.lock().head.clone();
            if let Some(head) = head {
                // the list is not empty
                if head.id > self.last_id_seen {
                    // start playing catch-up
                    self.next = Arc::downgrade(&head);
                    self.poll_next(cx)
                } else {
                    // we're at the tail
                    Poll::Pending
                }
            } else {
                // the list is empty
                Poll::Pending
            }
        }
    }
}

/// The next element in the stream or a marker of the number of items dropped since last polled.
#[derive(Debug, PartialEq)]
pub enum LazyItem<T> {
    /// The next item in the stream.
    Next(Arc<T>),
    /// A count of the items dropped between the last call to poll_next and this one.
    ItemsDropped(u64),
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fmt::Debug;

    impl<T: Debug> LazyItem<T> {
        #[track_caller]
        fn unwrap(self) -> Arc<T> {
            match self {
                LazyItem::Next(i) => i,
                LazyItem::ItemsDropped(n) => panic!("{} unexpected dropped items in test", n),
            }
        }

        #[track_caller]
        fn expect_dropped(self, expected: u64) {
            match self {
                LazyItem::Next(i) => {
                    panic!("expected {} dropped items, found Next({:#?})", expected, i)
                }
                LazyItem::ItemsDropped(n) => {
                    assert_eq!(n, expected, "wrong number of dropped items")
                }
            }
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn all_delivered_without_drops() {
        let list = ArcList::default();
        let mut dead_cursor = list.snapshot();
        assert_eq!(dead_cursor.next().await, None, "no items in the list");

        list.push_back(1);
        list.push_back(2);
        list.push_back(3);

        let mut middle_cursor = list.snapshot();
        assert_eq!(*middle_cursor.next().await.unwrap().unwrap(), 1);
        assert_eq!(*middle_cursor.next().await.unwrap().unwrap(), 2);
        assert_eq!(*middle_cursor.next().await.unwrap().unwrap(), 3);
        assert_eq!(dead_cursor.next().await, None, "no items in the list at snapshot");
        assert_eq!(dead_cursor.next().await, None, "no items in list at snapshot");
        assert_eq!(middle_cursor.next().await, None, "no items left in list");

        list.push_back(4);
        list.push_back(5);

        assert_eq!(dead_cursor.next().await, None, "no items in list at snapshot");
        assert_eq!(middle_cursor.next().await, None, "no items left in list at snapshot");

        let mut full_cursor = list.snapshot();
        assert_eq!(*full_cursor.next().await.unwrap().unwrap(), 1);
        assert_eq!(*full_cursor.next().await.unwrap().unwrap(), 2);
        assert_eq!(*full_cursor.next().await.unwrap().unwrap(), 3);
        assert_eq!(*full_cursor.next().await.unwrap().unwrap(), 4);
        assert_eq!(*full_cursor.next().await.unwrap().unwrap(), 5);
        assert_eq!(full_cursor.next().await, None, "no items left");
        assert_eq!(dead_cursor.next().await, None, "no items in list at snapshot");
        assert_eq!(middle_cursor.next().await, None, "no items left in list at snapshot");
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn drops_are_counted() {
        let list: ArcList<i32> = ArcList::default();
        let mut dead_cursor = list.snapshot();
        assert!(dead_cursor.next().await.is_none(), "no items in the list");

        list.push_back(1);
        list.push_back(2);
        list.push_back(3);
        list.push_back(4);
        list.push_back(5);

        let mut middle_cursor = list.snapshot();
        list.pop_front();
        list.pop_front();

        middle_cursor.next().await.unwrap().expect_dropped(2);
        assert_eq!(*middle_cursor.next().await.unwrap().unwrap(), 3);
        assert_eq!(*middle_cursor.next().await.unwrap().unwrap(), 4);
        assert_eq!(*middle_cursor.next().await.unwrap().unwrap(), 5);
        assert!(dead_cursor.next().await.is_none(), "no items in list at snapshot");
        assert!(middle_cursor.next().await.is_none(), "no items left in list");

        let mut full_cursor = list.snapshot();
        full_cursor.next().await.unwrap().expect_dropped(2);
        assert_eq!(*full_cursor.next().await.unwrap().unwrap(), 3);
        assert_eq!(*full_cursor.next().await.unwrap().unwrap(), 4);
        assert_eq!(*full_cursor.next().await.unwrap().unwrap(), 5);
        assert!(full_cursor.next().await.is_none(), "no items left");
        assert!(dead_cursor.next().await.is_none(), "no items in list at snapshot");
        assert!(middle_cursor.next().await.is_none(), "no items left in list at snapshot");
    }
}

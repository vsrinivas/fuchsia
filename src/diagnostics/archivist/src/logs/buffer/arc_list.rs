// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_diagnostics::StreamMode;
use futures::prelude::*;
use log::trace;
// we use parking_lot here instead of futures::lock because Cursor::get_next is recursive
use parking_lot::Mutex;
use std::{
    default::Default,
    pin::Pin,
    sync::{Arc, Weak},
    task::{Context, Poll, Waker},
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

    pub fn cursor(&self, mode: StreamMode) -> Cursor<T> {
        let id = self.inner.lock().new_cursor_id();
        Cursor::new(id, self.clone(), mode)
    }

    /// End the stream, ignoring new values and causing Cursors to return None after the current ID.
    pub fn terminate(&self) {
        self.inner.lock().terminate();
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

struct Root<T> {
    head: Option<Arc<Node<T>>>,
    tail: Weak<Node<T>>,
    /// The number of entries ever inserted into the list.
    entries_seen: u64,
    /// The number of entries ever removed from the list.
    entries_popped: u64,
    /// The last entry this list will yield.
    final_entry: u64,
    /// The next cursor this list will yield.
    next_cursor_id: CursorId,
    /// Wakers from subscribed cursors blocked on their next message.
    pending_cursors: Vec<(CursorId, Waker)>,
}

impl<T> Default for Root<T> {
    fn default() -> Self {
        Self {
            head: None,
            tail: Weak::new(),
            entries_seen: 0,
            entries_popped: 0,
            final_entry: std::u64::MAX,
            next_cursor_id: CursorId::default(),
            pending_cursors: Vec::new(),
        }
    }
}

impl<T> Root<T> {
    fn push_back(&mut self, item: T) {
        self.entries_seen += 1;
        assert!(
            self.entries_seen <= self.final_entry,
            "push_back() must not be called after terminate()"
        );

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
        self.wake_pending();
    }

    fn pop_front(&mut self) -> Option<Arc<T>> {
        self.entries_popped += 1;
        let prev_front = self.head.take();
        if let Some(prev) = prev_front.as_ref() {
            self.head = prev.next.lock().clone();
        }
        prev_front.map(|f| f.inner.clone())
    }

    fn new_cursor_id(&mut self) -> CursorId {
        let new_next = CursorId(self.next_cursor_id.0 + 1);
        std::mem::replace(&mut self.next_cursor_id, new_next)
    }

    fn wake_pending(&mut self) {
        for (id, waker) in self.pending_cursors.drain(..) {
            trace!("Waking {:?} for entry {}.", id, self.entries_seen);
            waker.wake();
        }
    }

    fn terminate(&mut self) {
        self.final_entry = self.entries_seen;
        self.wake_pending();
    }
}

/// A unique identifier for each cursor used to manage wakers.
#[derive(Clone, Copy, Debug, Default, Eq, Hash, PartialEq, PartialOrd, Ord)]
struct CursorId(u64);

/// A weak pointer into the buffer which is being concurrently iterated and modified.
///
/// A cursor iterates over nodes in the linked list, holding a weak pointer to the next node. If
/// that pointer turns out to be stale the cursor starts again at the beginning of the list and
/// first returns a count of items dropped.
///
/// The count is maintained by giving each successive item in a list a monotonically increasing ID
/// and tracking the "high-water mark" of the largest/last ID seen.
///
/// # Modes
///
/// These IDs are also how we express snapshotting vs. subscribing. The mode determines the minimum
/// and maximum IDs the cursor will yield.
///
/// # Wraparound
///
/// IDs are stored as `u64` and the list would need to receive a new entry once every nanosecond
/// for ~580 consecutive years before ID allocations will wrap around. This eventuality is *not*
/// accounted for in the implementation. If you're reading this after observing a bug due to that,
/// congratulations.
pub struct Cursor<T> {
    id: CursorId,
    last_visited: Weak<Node<T>>,
    last_id_seen: u64,
    until_id: u64,
    list: ArcList<T>,
}

impl<T> Cursor<T> {
    /// Construct a new cursor into the logs buffer. The `mode` passed determines the range over
    /// which the cursor operates:
    ///
    /// | mode      | first ID yielded        | last ID yielded         |
    /// |-----------|-------------------------|-------------------------|
    /// | snapshot  | 0                       | max at time of snapshot |
    /// | subscribe | max at time of snapshot | max ID possible         |
    /// | both      | 0                       | max ID possible         |
    fn new(id: CursorId, list: ArcList<T>, mode: StreamMode) -> Self {
        let (from, last_visited) = match mode {
            StreamMode::Snapshot | StreamMode::SnapshotThenSubscribe => (0, Default::default()),
            StreamMode::Subscribe => {
                let inner = list.inner.lock();
                (inner.entries_seen, inner.tail.clone())
            }
        };

        let to = match mode {
            StreamMode::Snapshot => list.inner.lock().entries_seen,
            StreamMode::SnapshotThenSubscribe | StreamMode::Subscribe => std::u64::MAX,
        };

        Self { id, list, last_id_seen: from, until_id: to, last_visited }
    }

    fn register_for_wakeup(&self, cx: &mut Context<'_>) -> Poll<Option<LazyItem<T>>> {
        let mut root = self.list.inner.lock();
        if self.last_id_seen == root.final_entry {
            trace!("{:?} has reached the end of the terminated stream.", self.id);
            Poll::Ready(None)
        } else {
            trace!("Registering {:?} for wakeup.", self.id);
            root.pending_cursors.push((self.id, cx.waker().clone()));
            root.pending_cursors.sort_by_key(|&(id, _)| id);
            root.pending_cursors.dedup_by_key(|&mut (id, _)| id);
            Poll::Pending
        }
    }
}

impl<T> Stream for Cursor<T> {
    type Item = LazyItem<T>;
    fn poll_next(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        trace!("{:?} polled.", self.id);
        if self.last_id_seen >= self.until_id {
            return Poll::Ready(None);
        }

        let next = if let Some(last) = self.last_visited.upgrade() {
            // get the next node from our last visited one
            last.next.lock().clone()
        } else {
            // otherwise start again at the head
            trace!("{:?} starting from the head of the list.", self.id);
            self.list.inner.lock().head.clone()
        };

        if let Some(to_return) = next {
            if to_return.id > self.until_id {
                self.last_id_seen = to_return.id;
                // we're past the end of this cursor's valid range
                trace!("{:?} is done.", self.id);
                return Poll::Ready(None);
            }

            // the number we missed is equal to the difference between
            // the last ID we saw and the ID *just before* the current value
            let num_missed = (to_return.id - 1) - self.last_id_seen;
            let item = if num_missed > 0 {
                // advance the cursor's high-water mark by the number we missed
                // so we only report each dropped item once
                trace!("{:?} reporting {} missed items.", self.id, num_missed);
                self.last_id_seen += num_missed;
                LazyItem::ItemsDropped(num_missed)
            } else {
                // we haven't missed anything, proceed normally
                trace!("{:?} yielding item {}.", self.id, to_return.id);
                self.last_id_seen = to_return.id;
                self.last_visited = Arc::downgrade(&to_return);
                LazyItem::Next(to_return.inner.clone())
            };

            Poll::Ready(Some(item))
        } else {
            self.register_for_wakeup(cx)
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
    use futures::poll;
    use std::{fmt::Debug, task::Poll};

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
    async fn subscription_delivered_without_drops() {
        let list = ArcList::default();
        let mut early_sub = list.cursor(StreamMode::Subscribe);

        let mut first_from_early_sub = early_sub.next();
        assert_eq!(poll!(&mut first_from_early_sub), Poll::Pending);

        list.push_back(1);
        assert_eq!(*first_from_early_sub.await.unwrap().unwrap(), 1);

        // this subscription starts after the 1 we just pushed
        let mut middle_sub = list.cursor(StreamMode::Subscribe);

        let (mut second_from_early_sub, mut second_from_middle_sub) =
            (early_sub.next(), middle_sub.next());
        assert_eq!(poll!(&mut second_from_early_sub), Poll::Pending);
        assert_eq!(poll!(&mut second_from_middle_sub), Poll::Pending);

        list.push_back(2);
        assert_eq!(*second_from_early_sub.await.unwrap().unwrap(), 2);
        assert_eq!(*second_from_middle_sub.await.unwrap().unwrap(), 2);

        let mut late_sub = list.cursor(StreamMode::Subscribe);

        let (mut third_from_early_sub, mut third_from_middle_sub, mut third_from_late_sub) =
            (early_sub.next(), middle_sub.next(), late_sub.next());
        assert_eq!(poll!(&mut third_from_early_sub), Poll::Pending);
        assert_eq!(poll!(&mut third_from_middle_sub), Poll::Pending);
        assert_eq!(poll!(&mut third_from_late_sub), Poll::Pending);

        list.push_back(3);
        assert_eq!(*third_from_early_sub.await.unwrap().unwrap(), 3);
        assert_eq!(*third_from_middle_sub.await.unwrap().unwrap(), 3);
        assert_eq!(*third_from_late_sub.await.unwrap().unwrap(), 3);

        let mut nop_sub = list.cursor(StreamMode::Subscribe);

        let (
            mut fourth_from_early_sub,
            mut fourth_from_middle_sub,
            mut fourth_from_late_sub,
            mut fourth_from_nop_sub,
        ) = (early_sub.next(), middle_sub.next(), late_sub.next(), nop_sub.next());
        assert_eq!(poll!(&mut fourth_from_early_sub), Poll::Pending);
        assert_eq!(poll!(&mut fourth_from_middle_sub), Poll::Pending);
        assert_eq!(poll!(&mut fourth_from_late_sub), Poll::Pending);
        assert_eq!(poll!(&mut fourth_from_nop_sub), Poll::Pending);

        list.terminate();
        assert_eq!(fourth_from_early_sub.await, None);
        assert_eq!(fourth_from_middle_sub.await, None);
        assert_eq!(fourth_from_late_sub.await, None);
        assert_eq!(fourth_from_nop_sub.await, None);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn snapshot_delivered_without_drops() {
        let list = ArcList::default();
        let mut dead_cursor = list.cursor(StreamMode::Snapshot);
        assert_eq!(dead_cursor.next().await, None, "no items in the list");

        list.push_back(1);
        list.push_back(2);
        list.push_back(3);

        let mut middle_cursor = list.cursor(StreamMode::Snapshot);
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

        let mut full_cursor = list.cursor(StreamMode::Snapshot);
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
    async fn snapshot_then_subscribe_gets_before_and_after() {
        let list: ArcList<i32> = ArcList::default();

        list.push_back(1);
        list.push_back(2);
        list.push_back(3);

        let mut middle_cursor = list.cursor(StreamMode::SnapshotThenSubscribe);
        assert_eq!(*middle_cursor.next().await.unwrap().unwrap(), 1);
        assert_eq!(*middle_cursor.next().await.unwrap().unwrap(), 2);
        assert_eq!(*middle_cursor.next().await.unwrap().unwrap(), 3);

        list.push_back(4);
        assert_eq!(*middle_cursor.next().await.unwrap().unwrap(), 4);

        list.push_back(5);
        assert_eq!(*middle_cursor.next().await.unwrap().unwrap(), 5);

        list.push_back(6);
        assert_eq!(*middle_cursor.next().await.unwrap().unwrap(), 6);

        list.terminate();
        assert_eq!(middle_cursor.next().await, None);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn subscription_drops_are_counted() {
        let list: ArcList<i32> = ArcList::default();
        let mut early_cursor = list.cursor(StreamMode::Subscribe);

        list.push_back(1);
        list.push_back(2);
        list.push_back(3);
        assert_eq!(*list.pop_front().unwrap(), 1);

        early_cursor.next().await.unwrap().expect_dropped(1);
        assert_eq!(*early_cursor.next().await.unwrap().unwrap(), 2);

        let mut middle_cursor = list.cursor(StreamMode::Subscribe);

        assert_eq!(*list.pop_front().unwrap(), 2);
        assert_eq!(*list.pop_front().unwrap(), 3);
        list.push_back(4);
        list.push_back(5);

        early_cursor.next().await.unwrap().expect_dropped(1);
        assert_eq!(*early_cursor.next().await.unwrap().unwrap(), 4);
        assert_eq!(*early_cursor.next().await.unwrap().unwrap(), 5);

        assert_eq!(*list.pop_front().unwrap(), 4);
        middle_cursor.next().await.unwrap().expect_dropped(1);
        assert_eq!(*middle_cursor.next().await.unwrap().unwrap(), 5);

        list.terminate();
        assert_eq!(middle_cursor.next().await, None);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn snapshot_drops_are_counted() {
        let list: ArcList<i32> = ArcList::default();
        let mut dead_cursor = list.cursor(StreamMode::Snapshot);
        assert!(dead_cursor.next().await.is_none(), "no items in the list");

        list.push_back(1);
        list.push_back(2);
        list.push_back(3);
        list.push_back(4);
        list.push_back(5);

        let mut middle_cursor = list.cursor(StreamMode::Snapshot);
        list.pop_front();
        list.pop_front();

        middle_cursor.next().await.unwrap().expect_dropped(2);
        assert_eq!(*middle_cursor.next().await.unwrap().unwrap(), 3);
        assert_eq!(*middle_cursor.next().await.unwrap().unwrap(), 4);
        assert_eq!(*middle_cursor.next().await.unwrap().unwrap(), 5);
        assert!(dead_cursor.next().await.is_none(), "no items in list at snapshot");
        assert!(middle_cursor.next().await.is_none(), "no items left in list");

        let mut full_cursor = list.cursor(StreamMode::Snapshot);
        full_cursor.next().await.unwrap().expect_dropped(2);
        assert_eq!(*full_cursor.next().await.unwrap().unwrap(), 3);
        assert_eq!(*full_cursor.next().await.unwrap().unwrap(), 4);
        assert_eq!(*full_cursor.next().await.unwrap().unwrap(), 5);
        assert!(full_cursor.next().await.is_none(), "no items left");
        assert!(dead_cursor.next().await.is_none(), "no items in list at snapshot");
        assert!(middle_cursor.next().await.is_none(), "no items left in list at snapshot");
    }
}

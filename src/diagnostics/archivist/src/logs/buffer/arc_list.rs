// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_diagnostics::StreamMode;
use futures::prelude::*;
// we use parking_lot here instead of futures::lock because Cursor::get_next is recursive
use parking_lot::Mutex;
use std::{
    collections::VecDeque,
    default::Default,
    pin::Pin,
    sync::Arc,
    task::{Context, Poll, Waker},
};
use tracing::trace;

/// A list that can be iterated despite concurrent insertions and deletions.
pub struct ArcList<T> {
    inner: Arc<Mutex<InnerArcList<T>>>,
}

impl<T> Default for ArcList<T> {
    fn default() -> Self {
        Self { inner: Arc::new(Mutex::new(Default::default())) }
    }
}

impl<T> ArcList<T> {
    pub fn is_empty(&self) -> bool {
        self.inner.lock().items.is_empty()
    }

    pub fn push_back(&self, item: T) {
        self.inner.lock().push_back(item);
    }

    pub fn pop_front(&self) -> Option<Arc<T>> {
        self.inner.lock().pop_front()
    }

    pub fn peek_front(&self) -> Option<Arc<T>> {
        self.inner.lock().peek_front().map(|item| item.value)
    }

    pub fn cursor(&self, mode: StreamMode) -> Cursor<T> {
        let id = self.inner.lock().new_cursor_id();
        Cursor::new(id, self.clone(), mode)
    }

    /// End the stream, ignoring new values and causing Cursors to return None after the current ID.
    pub fn terminate(&self) {
        self.inner.lock().terminate();
    }

    #[cfg(test)]
    pub fn final_entry(&self) -> u64 {
        self.inner.lock().final_entry
    }
}

impl<T> Clone for ArcList<T> {
    fn clone(&self) -> Self {
        Self { inner: self.inner.clone() }
    }
}

struct ArcListItem<T> {
    id: u64,
    value: Arc<T>,
}

impl<T> Clone for ArcListItem<T> {
    fn clone(&self) -> Self {
        Self { id: self.id, value: self.value.clone() }
    }
}

struct InnerArcList<T> {
    /// Map from entries_seen at the point that the entry is added to the list, to the value with
    /// that id.
    items: VecDeque<ArcListItem<T>>,
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

impl<T> Default for InnerArcList<T> {
    fn default() -> Self {
        Self {
            items: VecDeque::new(),
            entries_seen: 0,
            entries_popped: 0,
            final_entry: std::u64::MAX,
            next_cursor_id: CursorId::default(),
            pending_cursors: Vec::new(),
        }
    }
}

impl<T> InnerArcList<T> {
    fn push_back(&mut self, item: T) {
        self.entries_seen += 1;
        assert!(
            self.entries_seen <= self.final_entry,
            "push_back() must not be called after terminate()"
        );

        let id = self.entries_seen;
        self.items.push_back(ArcListItem { id, value: Arc::new(item) });

        self.wake_pending();
    }

    fn pop_front(&mut self) -> Option<Arc<T>> {
        self.items.pop_front().and_then(|item| {
            self.entries_popped += 1;
            Some(item.value)
        })
    }

    fn peek_front(&self) -> Option<ArcListItem<T>> {
        self.items.front().map(|item| item.clone())
    }

    fn first_starting_at(&self, id: u64) -> Option<ArcListItem<T>> {
        self.items.front().and_then(|front_item| {
            if front_item.id < id {
                let index = id - front_item.id;
                self.items.get(index as usize).map(|item| item.clone())
            } else {
                Some(front_item.clone())
            }
        })
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
        tracing::debug!("terminating buffer");
        self.final_entry = self.entries_seen;
        self.wake_pending();
    }
}

/// A unique identifier for each cursor used to manage wakers.
#[derive(Clone, Copy, Debug, Default, Eq, Hash, PartialEq, PartialOrd, Ord)]
struct CursorId(u64);

/// A weak pointer into the buffer which is being concurrently iterated and modified.
///
/// A cursor iterates over nodes in the list, holding the id of the last node seen.
/// If that id is less than the first id found in the deque, the cursor starts at the beginning of
/// the list and first returns a count of items dropped.
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
    last_id_seen: Option<u64>,
    until_id: u64,
    list: ArcList<T>,
    mode: StreamMode,
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
        let from = match mode {
            StreamMode::Snapshot | StreamMode::SnapshotThenSubscribe => None,
            StreamMode::Subscribe => {
                let inner = list.inner.lock();
                Some(inner.entries_seen)
            }
        };

        let to = match mode {
            StreamMode::Snapshot => list.inner.lock().entries_seen,
            StreamMode::SnapshotThenSubscribe | StreamMode::Subscribe => std::u64::MAX,
        };

        Self { id, list, last_id_seen: from, until_id: to, mode }
    }

    fn maybe_register_for_wakeup(&mut self, cx: &mut Context<'_>) -> Poll<Option<LazyItem<T>>> {
        let mut root = self.list.inner.lock();
        let cursor_at_end = self.last_id_seen.unwrap_or(0) == root.final_entry;
        let list_fully_drained = root.final_entry == root.entries_popped;

        if root.entries_popped > self.last_id_seen.unwrap_or(0) {
            // This happens when entries were popped before they could be returned,
            // but there is not currently anything left to return.  We need
            // to update our position and return the number of entries that
            // were popped.
            let entries_missing = root.entries_popped - self.last_id_seen.unwrap_or(0);
            self.last_id_seen = Some(root.entries_popped);
            Poll::Ready(Some(LazyItem::ItemsDropped(entries_missing)))
        } else if cursor_at_end || list_fully_drained {
            trace!("{:?} has reached the end of the terminated stream.", self.id);
            Poll::Ready(None)
        } else if self.mode == StreamMode::Snapshot {
            // There are no further entries to return, and we are in snapshot mode. Report that we
            // are at the end of the stream rather than registering for a wakeup.
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
        if self.last_id_seen.unwrap_or(0) >= self.until_id {
            return Poll::Ready(None);
        }

        let next = match self.last_id_seen {
            Some(id) => self.list.inner.lock().first_starting_at(id + 1),
            None => {
                // otherwise start again at the head
                trace!("{:?} starting from the head of the list.", self.id);
                self.list.inner.lock().peek_front()
            }
        };

        if let Some(to_return) = next {
            if to_return.id > self.until_id {
                self.last_id_seen = Some(to_return.id);
                // we're past the end of this cursor's valid range
                trace!("{:?} is done.", self.id);
                return Poll::Ready(None);
            }

            // the number we missed is equal to the difference between
            // the last ID we saw and the ID *just before* the current value
            let num_missed = (to_return.id - 1) - self.last_id_seen.unwrap_or(0);
            let item = if num_missed > 0 {
                // advance the cursor's high-water mark by the number we missed
                // so we only report each dropped item once
                trace!("{:?} reporting {} missed items.", self.id, num_missed);
                self.last_id_seen = Some(self.last_id_seen.unwrap_or(0) + num_missed);
                LazyItem::ItemsDropped(num_missed)
            } else {
                // we haven't missed anything, proceed normally
                trace!("{:?} yielding item {}.", self.id, to_return.id);
                self.last_id_seen = Some(to_return.id);
                LazyItem::Next(to_return.value.clone())
            };

            Poll::Ready(Some(item))
        } else {
            // No further data is waiting in the stream. Depending on the stream mode, we may
            // register for a wakeup.
            self.maybe_register_for_wakeup(cx)
        }
    }
}

impl<T> std::fmt::Debug for Cursor<T> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("Cursor")
            .field("id", &self.id)
            .field("last_id_seen", &self.last_id_seen)
            .field("until_id", &self.until_id)
            .finish()
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

    #[test]
    fn list_is_empty() {
        let list = ArcList::default();
        assert!(list.is_empty());
        list.push_back(1);
        assert!(!list.is_empty());
        list.pop_front();
        assert!(list.is_empty());
    }

    #[test]
    fn list_peek_front() {
        let list = ArcList::default();
        assert!(list.peek_front().is_none());
        list.push_back(1);
        list.push_back(2);
        assert_eq!(*list.peek_front().unwrap(), 1);
        list.pop_front();
        assert_eq!(*list.peek_front().unwrap(), 2);
        list.pop_front();
        assert!(list.peek_front().is_none());
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

    #[fuchsia_async::run_singlethreaded(test)]
    async fn post_termination_cursors() {
        let list: ArcList<i32> = ArcList::default();
        list.push_back(1);
        list.push_back(2);
        list.push_back(3);
        list.push_back(4);
        list.push_back(5);
        list.terminate();

        let snapshot: Vec<_> =
            list.cursor(StreamMode::Snapshot).map(|i| *i.unwrap()).collect().await;
        let subscribe: Vec<_> =
            list.cursor(StreamMode::Subscribe).map(|i| *i.unwrap()).collect().await;
        let both: Vec<_> =
            list.cursor(StreamMode::SnapshotThenSubscribe).map(|i| *i.unwrap()).collect().await;

        assert_eq!(snapshot, vec![1, 2, 3, 4, 5]);
        assert!(subscribe.is_empty());
        assert_eq!(both, vec![1, 2, 3, 4, 5]);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn empty_post_termination_cursors() {
        let list: ArcList<i32> = ArcList::default();
        list.terminate();

        let snapshot: Vec<_> =
            list.cursor(StreamMode::Snapshot).map(|i| *i.unwrap()).collect().await;
        let subscribe: Vec<_> =
            list.cursor(StreamMode::Subscribe).map(|i| *i.unwrap()).collect().await;
        let both: Vec<_> =
            list.cursor(StreamMode::SnapshotThenSubscribe).map(|i| *i.unwrap()).collect().await;

        assert!(snapshot.is_empty());
        assert!(subscribe.is_empty());
        assert!(both.is_empty());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn drained_post_termination_cursors() {
        let list: ArcList<i32> = ArcList::default();
        list.push_back(1);
        list.push_back(2);
        list.push_back(3);
        list.push_back(4);
        list.push_back(5);
        list.terminate();
        list.pop_front();
        list.pop_front();
        list.pop_front();
        list.pop_front();
        list.pop_front();

        let mut snapshot_cursor = list.cursor(StreamMode::Snapshot);
        snapshot_cursor.next().await.unwrap().expect_dropped(5);
        let snapshot: Vec<_> = snapshot_cursor.map(|i| *i.unwrap()).collect().await;
        let subscribe: Vec<_> =
            list.cursor(StreamMode::Subscribe).map(|i| *i.unwrap()).collect().await;
        let mut both_cursor = list.cursor(StreamMode::Snapshot);
        both_cursor.next().await.unwrap().expect_dropped(5);
        let both: Vec<_> = both_cursor.map(|i| *i.unwrap()).collect().await;

        assert!(snapshot.is_empty());
        assert!(subscribe.is_empty());
        assert!(both.is_empty());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn snapshot_does_not_hang_when_nothing_is_left() {
        let list: ArcList<i32> = ArcList::default();
        list.push_back(1);
        list.push_back(2);
        list.push_back(3);
        list.push_back(4);
        list.push_back(5);
        list.pop_front();
        list.pop_front();
        list.pop_front();
        list.pop_front();
        list.pop_front();

        let mut cursor = list.cursor(StreamMode::Snapshot);

        cursor.next().await.unwrap().expect_dropped(5);

        let snapshot: Vec<_> = cursor.map(|i| *i.unwrap()).collect().await;

        assert!(snapshot.is_empty());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn snapshot_does_not_hang_when_values_are_popped_before_start() {
        let list: ArcList<i32> = ArcList::default();
        list.push_back(1);
        list.push_back(2);
        list.push_back(3);
        list.push_back(4);
        list.push_back(5);
        list.pop_front();
        list.pop_front();
        list.pop_front();

        let mut cursor = list.cursor(StreamMode::Snapshot);
        cursor.next().await.expect("initial value exists").expect_dropped(3);

        let snapshot: Vec<_> = cursor.map(|i| *i.unwrap()).collect().await;

        assert_eq!(2, snapshot.len());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn popping_more_elements_than_exist_does_not_break_readers() {
        let list: ArcList<i32> = ArcList::default();
        list.push_back(1);
        list.push_back(2);
        list.push_back(3);
        let mut cursor = list.cursor(StreamMode::SnapshotThenSubscribe);

        assert_eq!(*cursor.next().await.unwrap().unwrap(), 1);
        assert_eq!(*cursor.next().await.unwrap().unwrap(), 2);
        assert_eq!(*cursor.next().await.unwrap().unwrap(), 3);
        let mut next = cursor.next();
        assert_eq!(poll!(&mut next), Poll::Pending);

        list.pop_front();
        list.pop_front();
        list.pop_front();
        list.pop_front();
        list.pop_front();

        let mut next = cursor.next();
        assert_eq!(poll!(&mut next), Poll::Pending);
        list.push_back(4);
        assert_eq!(*cursor.next().await.unwrap().unwrap(), 4);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn snapshot_then_subscribe_works_when_only_dropped_notifications_are_returned() {
        let list: ArcList<i32> = ArcList::default();
        list.push_back(1);
        list.push_back(2);
        list.push_back(3);
        list.pop_front();
        list.pop_front();
        list.pop_front();

        let mut cursor = list.cursor(StreamMode::SnapshotThenSubscribe);
        cursor.next().await.unwrap().expect_dropped(3);
        let mut next = cursor.next();
        assert_eq!(poll!(&mut next), Poll::Pending);

        list.push_back(4);
        list.pop_front();

        cursor.next().await.unwrap().expect_dropped(1);
        let mut next = cursor.next();
        assert_eq!(poll!(&mut next), Poll::Pending);

        list.terminate();
        let snapshot: Vec<_> = cursor.map(|i| *i.unwrap()).collect().await;
        assert!(snapshot.is_empty());
    }
}

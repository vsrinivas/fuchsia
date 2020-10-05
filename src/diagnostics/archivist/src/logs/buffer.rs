// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_diagnostics::StreamMode;
use fuchsia_inspect as inspect;
use fuchsia_inspect_derive::Inspect;
use inspect::NumericProperty;

mod arc_list;
use arc_list::ArcList;
pub use arc_list::{Cursor, LazyItem};

/// A value which knows approximately how many bytes it requires to send or store.
pub trait Accounted {
    /// Bytes used by this value.
    fn bytes_used(&self) -> usize;
}

/// A Memory bounded buffer. Sizes are calculated by items' implementation of `Accounted`.
#[derive(Inspect)]
pub(super) struct MemoryBoundedBuffer<T> {
    #[inspect(skip)]
    buffer: ArcList<T>,
    #[inspect(skip)]
    total_size: usize,
    #[inspect(skip)]
    capacity: usize,
    inspect_node: inspect::Node,
    rolled_out_entries: inspect::UintProperty,
}

impl<T> MemoryBoundedBuffer<T>
where
    T: Accounted,
{
    pub fn new(capacity: usize) -> MemoryBoundedBuffer<T> {
        assert!(capacity > 0, "capacity should be more than 0");
        MemoryBoundedBuffer {
            buffer: ArcList::default(),
            capacity: capacity,
            total_size: 0,
            inspect_node: inspect::Node::default(),
            rolled_out_entries: inspect::UintProperty::default(),
        }
    }

    /// Add an item to the buffer.
    ///
    /// If adding the item overflows the capacity, oldest item(s) are deleted until under the limit.
    pub fn push(&mut self, item: T) {
        let size = item.bytes_used();
        self.buffer.push_back(item);
        self.total_size += size;
        while self.total_size > self.capacity {
            let bytes_freed =
                self.buffer.pop_front().expect("there are items if reducing size").bytes_used();
            self.total_size -= bytes_freed;
            self.rolled_out_entries.add(1);
        }
    }

    /// Return a lazy cursor over items in the buffer.
    pub fn cursor(&self, mode: StreamMode) -> Cursor<T> {
        self.buffer.cursor(mode)
    }

    /// Stop accepting new messages, flush cursors.
    pub fn terminate(&mut self) {
        self.buffer.terminate();
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fuchsia_inspect::assert_inspect_tree;
    use fuchsia_inspect_derive::WithInspect;
    use futures::StreamExt;

    impl Accounted for (i32, usize) {
        fn bytes_used(&self) -> usize {
            self.1
        }
    }

    impl<T> MemoryBoundedBuffer<T>
    where
        T: Accounted + Clone,
    {
        /// Returns a Vec of the current items.
        pub async fn collect(&self) -> Vec<T> {
            let mut items = vec![];
            let mut snapshot = self.buffer.cursor(StreamMode::Snapshot);

            while let Some(item) = snapshot.next().await {
                match item {
                    arc_list::LazyItem::Next(i) => items.push((*i).clone()),
                    arc_list::LazyItem::ItemsDropped(n) => {
                        log::warn!("dropped {} messages while collecting a backfill snapshot", n);
                    }
                }
            }

            items
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_simple() {
        let inspector = inspect::Inspector::new();
        let mut m =
            MemoryBoundedBuffer::new(12).with_inspect(inspector.root(), "buffer_stats").unwrap();
        m.push((1, 4));
        m.push((2, 4));
        m.push((3, 4));
        assert_eq!(&m.collect().await[..], &[(1, 4), (2, 4), (3, 4)]);
        assert_inspect_tree!(inspector,
        root: {
            buffer_stats: {
                rolled_out_entries: 0u64,
            }
        });
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_bound() {
        let inspector = inspect::Inspector::new();
        let mut m =
            MemoryBoundedBuffer::new(12).with_inspect(inspector.root(), "buffer_stats").unwrap();
        m.push((1, 4));
        m.push((2, 4));
        m.push((3, 5));
        assert_eq!(&m.collect().await[..], &[(2, 4), (3, 5)]);
        m.push((4, 4));
        m.push((5, 4));
        assert_eq!(&m.collect().await[..], &[(4, 4), (5, 4)]);
        assert_inspect_tree!(inspector,
        root: {
            buffer_stats: {
                rolled_out_entries: 3u64,
            }
        });
    }
}

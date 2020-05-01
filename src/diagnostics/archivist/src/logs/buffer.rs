// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_inspect as inspect;
use fuchsia_inspect_derive::Inspect;
use inspect::NumericProperty;
use std::collections::VecDeque;

/// A value which knows approximately how many bytes it requires to send or store.
pub trait Accounted {
    /// Bytes used by this value.
    fn bytes_used(&self) -> usize;
}

/// A Memory bounded buffer. Sizes are calculated by items' implementation of `Accounted`.
#[derive(Inspect)]
pub(super) struct MemoryBoundedBuffer<T> {
    #[inspect(skip)]
    inner: VecDeque<T>,
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
            inner: VecDeque::new(),
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
        self.inner.push_back(item);
        self.total_size += size;
        while self.total_size > self.capacity {
            let bytes_freed =
                self.inner.pop_front().expect("there are items if reducing size").bytes_used();
            self.total_size -= bytes_freed;
            self.rolled_out_entries.add(1);
        }
    }

    pub fn iter(&mut self) -> impl Iterator<Item = &T> {
        self.inner.iter()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fuchsia_inspect::assert_inspect_tree;

    impl Accounted for (i32, usize) {
        fn bytes_used(&self) -> usize {
            self.1
        }
    }

    #[test]
    fn test_simple() {
        let inspector = inspect::Inspector::new();
        let mut m = MemoryBoundedBuffer::new(12);
        m.iattach(inspector.root(), "buffer_stats").unwrap();
        m.push((1, 4));
        m.push((2, 4));
        m.push((3, 4));
        assert_eq!(&m.iter().collect::<Vec<&(i32, usize)>>()[..], &[&(1, 4), &(2, 4), &(3, 4)]);
        assert_inspect_tree!(inspector,
        root: {
            buffer_stats: {
                rolled_out_entries: 0u64,
            }
        });
    }

    #[test]
    fn test_bound() {
        let inspector = inspect::Inspector::new();
        let mut m = MemoryBoundedBuffer::new(12);
        m.iattach(inspector.root(), "buffer_stats").unwrap();
        m.push((1, 4));
        m.push((2, 4));
        m.push((3, 5));
        assert_eq!(&m.iter().collect::<Vec<&(i32, usize)>>()[..], &[&(2, 4), &(3, 5)]);
        m.push((4, 4));
        m.push((5, 4));
        assert_eq!(&m.iter().collect::<Vec<&(i32, usize)>>()[..], &[&(4, 4), &(5, 4)]);
        assert_inspect_tree!(inspector,
        root: {
            buffer_stats: {
                rolled_out_entries: 3u64,
            }
        });
    }
}

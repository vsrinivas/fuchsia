// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::VecDeque;

/// A Memory bounded buffer. MemoryBoundedBuffer does not calculate the size of `item`,
/// rather it takes the size as argument and then maintains its internal buffer.
/// Oldest item(s) are deleted in the event of buffer overflow.
pub(super) struct MemoryBoundedBuffer<T> {
    inner: VecDeque<(T, usize)>,
    total_size: usize,
    capacity: usize,
}

impl<T> MemoryBoundedBuffer<T> {
    pub fn new(capacity: usize) -> MemoryBoundedBuffer<T> {
        assert!(capacity > 0, "capacity should be more than 0");
        MemoryBoundedBuffer { inner: VecDeque::new(), capacity: capacity, total_size: 0 }
    }

    pub fn push(&mut self, item: T, size: usize) {
        self.inner.push_back((item, size));
        self.total_size += size;
        while self.total_size > self.capacity {
            let removed_size = self.inner.pop_front().expect("there are items if reducing size").1;
            self.total_size -= removed_size;
        }
    }

    pub fn iter(&mut self) -> impl Iterator<Item = &(T, usize)> {
        self.inner.iter()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_simple() {
        let mut m = MemoryBoundedBuffer::new(12);
        m.push(1, 4);
        m.push(2, 4);
        m.push(3, 4);
        assert_eq!(&m.iter().collect::<Vec<&(i32, usize)>>()[..], &[&(1, 4), &(2, 4), &(3, 4)]);
    }

    #[test]
    fn test_bound() {
        let mut m = MemoryBoundedBuffer::new(12);
        m.push(1, 4);
        m.push(2, 4);
        m.push(3, 5);
        assert_eq!(&m.iter().collect::<Vec<&(i32, usize)>>()[..], &[&(2, 4), &(3, 5)]);
        m.push(4, 4);
        m.push(5, 4);
        assert_eq!(&m.iter().collect::<Vec<&(i32, usize)>>()[..], &[&(4, 4), &(5, 4)]);
    }
}

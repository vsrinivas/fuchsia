// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::VecDeque;

/// A Memory bounded buffer. Does not calculate the size of `item`,
/// rather it takes the size as argument and then maintains its internal buffer.
/// Oldest item(s) are deleted in the event of buffer overflow.
// TODO(fxb/48384): use T: SomeSizeTraitWeDefine
pub(super) struct MemoryBoundedBuffer<T> {
    inner: VecDeque<(T, usize)>,
    total_size: usize,
    capacity: usize,
}

impl<T> MemoryBoundedBuffer<T> {
    /// Construct a new buffer with the provided maximum capacity in bytes.
    ///
    /// # Panics
    ///
    /// Panics if `capacity` is 0.
    pub fn new(capacity: usize) -> MemoryBoundedBuffer<T> {
        assert!(capacity > 0, "capacity should be more than 0");
        MemoryBoundedBuffer { inner: VecDeque::new(), capacity: capacity, total_size: 0 }
    }

    /// Add a new item to the tail of the buffer with the provided size cost. Rolls out items from
    /// the head of the buffer until the new item's addition will fit in the buffer's capacity.
    pub fn push(&mut self, item: T, size: usize) {
        self.inner.push_back((item, size));
        self.total_size += size;
        while self.total_size > self.capacity {
            if let Some((_i, s)) = self.inner.pop_front() {
                self.total_size -= s;
            } else {
                unreachable!();
            }
        }
    }

    /// Return an iterator over the items and their sizes.
    pub fn iter(&self) -> impl Iterator<Item = &(T, usize)> {
        self.inner.iter()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_simple() {
        let mut m = MemoryBoundedBuffer::new(12);
        m.push(1i32, 4usize);
        m.push(2, 4);
        m.push(3, 4);
        assert_eq!(
            &m.iter().collect::<Vec<&(i32, usize)>>()[..],
            &[&(1i32, 4usize), &(2, 4), &(3, 4)]
        );
    }

    #[test]
    fn test_bound() {
        let mut m = MemoryBoundedBuffer::new(12);
        m.push(1i32, 4usize);
        m.push(2, 4);
        m.push(3, 5);
        assert_eq!(&m.iter().collect::<Vec<&(i32, usize)>>()[..], &[&(2i32, 4usize), &(3, 5)]);
        m.push(4, 4);
        m.push(5, 4);
        assert_eq!(&m.iter().collect::<Vec<&(i32, usize)>>()[..], &[&(4i32, 4usize), &(5, 4)]);
    }
}

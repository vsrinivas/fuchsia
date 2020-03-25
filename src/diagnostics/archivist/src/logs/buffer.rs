// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::{vec_deque, VecDeque};

/// A Memory bounded buffer. MemoryBoundedBuffer does not calculate the size of `item`,
/// rather it takes the size as argument and then maintains its internal buffer.
/// Oldest item(s) are deleted in the event of buffer overflow.
pub(super) struct MemoryBoundedBuffer<T> {
    inner: VecDeque<(T, usize)>,
    total_size: usize,
    capacity: usize,
}

/// `MemoryBoundedBuffer` mutable iterator.
struct IterMut<'a, T> {
    inner: vec_deque::IterMut<'a, (T, usize)>,
}

impl<'a, T: 'a> Iterator for IterMut<'a, T> {
    type Item = (&'a mut T, usize);

    #[inline]
    fn next(&mut self) -> Option<(&'a mut T, usize)> {
        self.inner.next().map(|(t, s)| (t, *s))
    }

    #[inline]
    fn size_hint(&self) -> (usize, Option<usize>) {
        self.inner.size_hint()
    }
}

impl<T> MemoryBoundedBuffer<T> {
    /// capacity in bytes
    pub fn new(capacity: usize) -> MemoryBoundedBuffer<T> {
        assert!(capacity > 0, "capacity should be more than 0");
        MemoryBoundedBuffer { inner: VecDeque::new(), capacity: capacity, total_size: 0 }
    }

    /// size in bytes
    pub fn push(&mut self, item: T, size: usize) {
        self.inner.push_back((item, size));
        self.total_size += size;
        while self.total_size > self.capacity {
            let removed_size = self.inner.pop_front().expect("there are items if reducing size").1;
            self.total_size -= removed_size;
        }
    }

    pub fn iter_mut(&mut self) -> impl Iterator<Item = (&'_ mut T, usize)> {
        IterMut { inner: self.inner.iter_mut() }
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
        assert_eq!(
            &m.iter_mut().collect::<Vec<(&mut i32, usize)>>()[..],
            &[(&mut 1, 4), (&mut 2, 4), (&mut 3, 4)]
        );
    }

    #[test]
    fn test_bound() {
        let mut m = MemoryBoundedBuffer::new(12);
        m.push(1, 4);
        m.push(2, 4);
        m.push(3, 5);
        assert_eq!(
            &m.iter_mut().collect::<Vec<(&mut i32, usize)>>()[..],
            &[(&mut 2, 4), (&mut 3, 5)]
        );
        m.push(4, 4);
        m.push(5, 4);
        assert_eq!(
            &m.iter_mut().collect::<Vec<(&mut i32, usize)>>()[..],
            &[(&mut 4, 4), (&mut 5, 4)]
        );
    }
}

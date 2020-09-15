// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::constants::SENTINEL;
use slab::Slab;

struct Node<T> {
    value: T,
    next: usize,
    prev: usize,
}

/// Maintain a list of objects referencable by an id.
/// Used for implementing the waiter list for cutex.
pub(crate) struct List<T> {
    slab: Slab<Node<T>>,
    first: usize,
    last: usize,
}

impl<T: std::fmt::Debug> std::fmt::Debug for List<T> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_list().entries(self.iter()).finish()
    }
}

impl<T> List<T> {
    pub(crate) fn new() -> List<T> {
        List { slab: Slab::new(), first: SENTINEL, last: SENTINEL }
    }

    pub(crate) fn get_mut(&mut self, id: usize) -> &mut T {
        &mut self.slab[id].value
    }

    pub(crate) fn push(&mut self, value: T) -> (usize, bool) {
        match (self.first, self.last) {
            (SENTINEL, SENTINEL) => {
                let id = self.slab.insert(Node { value, next: SENTINEL, prev: SENTINEL });
                self.first = id;
                self.last = id;
                (id, true)
            }
            (_, SENTINEL) | (SENTINEL, _) => {
                unreachable!("first/last should either be both SENTINEL or both node indices")
            }
            (_, prev) => {
                let id = self.slab.insert(Node { value, next: SENTINEL, prev });
                self.slab[prev].next = id;
                self.last = id;
                (id, false)
            }
        }
    }

    pub(crate) fn iter(&self) -> Iter<'_, T> {
        Iter { slab: &self.slab, next: self.first }
    }

    // Run `f` on each list element.
    // `f` is passed the id of the element and a mutable reference to its value.
    // if `f` returns `false`, the iteration short circuits.
    // Returns true if all elements were visited, false if the iteration was short circuited.
    pub(crate) fn for_each_until_mut(&mut self, f: impl Fn(usize, &mut T) -> bool) -> bool {
        let mut cur = self.first;
        while cur != SENTINEL {
            let n = &mut self.slab[cur];
            if !f(cur, &mut n.value) {
                return false;
            }
            cur = n.next;
        }
        return true;
    }

    pub(crate) fn remove(&mut self, id: usize) -> (T, bool) {
        let n = self.slab.remove(id);
        let is_last = if n.prev == SENTINEL {
            if n.next == SENTINEL {
                self.first = SENTINEL;
                self.last = SENTINEL;
                true
            } else {
                self.slab[n.next].prev = SENTINEL;
                self.first = n.next;
                false
            }
        } else if n.next == SENTINEL {
            self.slab[n.prev].next = SENTINEL;
            self.last = n.prev;
            false
        } else {
            self.slab[n.next].prev = n.prev;
            self.slab[n.prev].next = n.next;
            false
        };
        (n.value, is_last)
    }

    #[cfg(test)]
    fn to_vec(&self) -> Vec<T>
    where
        T: Clone,
    {
        self.iter().map(Clone::clone).collect()
    }
}

pub(crate) struct Iter<'a, T> {
    slab: &'a Slab<Node<T>>,
    next: usize,
}

impl<'a, T> std::iter::Iterator for Iter<'a, T> {
    type Item = &'a T;
    fn next(&mut self) -> Option<&'a T> {
        if self.next == SENTINEL {
            None
        } else {
            let n = &self.slab[self.next];
            self.next = n.next;
            Some(&n.value)
        }
    }
}

#[cfg(test)]
mod tests {

    use super::List;

    #[test]
    fn works() {
        let mut l = List::new();
        let (idx0, first) = l.push(0u8);
        assert_eq!(first, true);
        assert_eq!(l.to_vec(), vec![0u8]);
        let (idx1, first) = l.push(1u8);
        assert_eq!(first, false);
        assert_eq!(l.to_vec(), vec![0u8, 1u8]);
        assert_eq!(l.remove(idx0), (0u8, false));
        assert_eq!(l.to_vec(), vec![1u8]);
        let (idx2, first) = l.push(2u8);
        assert_eq!(first, false);
        assert_eq!(l.to_vec(), vec![1u8, 2u8]);
        assert_eq!(l.remove(idx2), (2u8, false));
        assert_eq!(l.to_vec(), vec![1u8]);
        assert_eq!(l.remove(idx1), (1u8, true));
        assert_eq!(l.to_vec(), vec![]);
    }
}

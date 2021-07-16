// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{cmp::Ordering, iter::FromIterator, mem};

use rustc_hash::FxHashMap;
use surpass::{self, painter::Props};

const IDENTITY: &[f32; 6] = &[1.0, 0.0, 0.0, 1.0, 0.0, 0.0];

#[derive(Clone, Copy, Debug, Eq, Hash, Ord, PartialEq, PartialOrd)]
pub struct LayerId(pub(crate) u16);

#[derive(Clone, Copy, Debug)]
struct End {
    index: u16,
    other: u16,
}

impl End {
    pub fn next(self) -> Option<Self> {
        match self.index.cmp(&self.other) {
            Ordering::Less => Some(Self { index: self.index + 1, ..self }),
            Ordering::Equal => None,
            Ordering::Greater => Some(Self { index: self.index - 1, ..self }),
        }
    }

    pub fn other(self) -> Self {
        Self { index: self.other, other: self.index }
    }
}

#[derive(Debug)]
pub struct IdSet {
    free_ranges: FxHashMap<u16, u16>,
}

impl IdSet {
    pub fn new() -> Self {
        Self { free_ranges: FxHashMap::from_iter([(0, u16::MAX), (u16::MAX, 0)]) }
    }

    fn first(&self) -> Option<End> {
        self.free_ranges.iter().next().map(|(&index, &other)| End { index, other })
    }

    fn get(&self, index: u16) -> Option<End> {
        self.free_ranges.get(&index).map(|&other| End { index, other })
    }

    fn insert(&mut self, end: End) {
        self.free_ranges.insert(end.index, end.other);
    }

    fn remove(&mut self, end: End) {
        self.free_ranges.remove(&end.index);
    }

    pub fn acquire(&mut self) -> Option<u16> {
        self.first().map(|end| {
            self.remove(end);

            if let Some(next) = end.next() {
                self.insert(next);
                self.insert(next.other());
            }

            end.index
        })
    }

    pub fn release(&mut self, index: u16) {
        let left = index.checked_sub(1).and_then(|index| self.get(index));
        let right = index.checked_add(1).and_then(|index| self.get(index));

        match (left, right) {
            (Some(left), Some(right)) => {
                // Connect ranges by filling the gap.
                self.remove(left);
                self.remove(right);

                let new_end = End { index: left.other, other: right.other };
                self.insert(new_end);
                self.insert(new_end.other());
            }
            (Some(end), None) | (None, Some(end)) => {
                // Enlarge range with id.
                self.remove(end);

                let new_end = End { index, ..end };
                self.insert(new_end);
                self.insert(new_end.other());
            }
            (None, None) => self.insert(End { index, other: index }),
        }
    }
}

type Container = u32;

#[derive(Clone, Debug, Default)]
pub struct SmallBitSet {
    bit_set: Container,
}

impl SmallBitSet {
    pub fn clear(&mut self) {
        self.bit_set = 0;
    }

    pub const fn contains(&self, val: &u8) -> bool {
        (self.bit_set >> *val as Container) & 0b1 != 0
    }

    pub fn insert(&mut self, val: u8) -> bool {
        if val as usize >= mem::size_of_val(&self.bit_set) * 8 {
            return false;
        }

        self.bit_set |= 0b1 << val as Container;

        true
    }

    pub fn remove(&mut self, val: u8) -> bool {
        if val as usize >= mem::size_of_val(&self.bit_set) * 8 {
            return false;
        }

        self.bit_set &= !(0b1 << val as Container);

        true
    }

    pub fn first_empty_slot(&mut self) -> Option<u8> {
        let slot = self.bit_set.trailing_ones() as u8;

        self.insert(slot).then(|| slot)
    }
}

#[derive(Debug, Default)]
pub struct Layer {
    pub(crate) inner: surpass::Layer,
    props: Props,
    pub(crate) is_unchanged: SmallBitSet,
    pub(crate) len: usize,
}

impl Layer {
    #[inline]
    pub fn is_enabled(&self) -> bool {
        self.inner.is_enabled
    }

    #[inline]
    pub fn set_is_enabled(&mut self, is_enabled: bool) -> &mut Self {
        self.inner.is_enabled = is_enabled;
        self
    }

    #[inline]
    pub fn disable(&mut self) -> &mut Self {
        self.inner.is_enabled = false;
        self
    }

    #[inline]
    pub fn enable(&mut self) -> &mut Self {
        self.inner.is_enabled = true;
        self
    }

    #[inline]
    pub fn transform(&self) -> &[f32; 6] {
        self.inner.affine_transform.as_ref().unwrap_or(IDENTITY)
    }

    #[inline]
    pub fn set_transform(&mut self, transform: &[f32; 6]) -> &mut Self {
        let affine_transform = if transform == IDENTITY {
            None
        } else {
            if transform[0] * transform[0] + transform[2] * transform[2] > 1.001
                || transform[1] * transform[1] + transform[3] * transform[3] > 1.001
            {
                panic!("Layer's scaling on each axis must be between -1.0 and 1.0");
            }

            Some(*transform)
        };

        if self.inner.affine_transform != affine_transform {
            self.is_unchanged.clear();
            self.inner.affine_transform = affine_transform;
        }

        self
    }

    #[inline]
    pub fn order(&self) -> u16 {
        self.inner.order.expect("Layers should always have orders")
    }

    #[inline]
    pub fn set_order(&mut self, order: u16) -> &mut Self {
        if self.inner.order != Some(order) {
            self.is_unchanged.clear();
            self.inner.order = Some(order);
        }

        self
    }

    #[inline]
    pub fn props(&self) -> &Props {
        &self.props
    }

    #[inline]
    pub fn set_props(&mut self, props: Props) -> &mut Self {
        if self.props != props {
            self.is_unchanged.clear();
            self.props = props;
        }

        self
    }

    pub(crate) fn is_unchanged(&self, cache_id: u8) -> bool {
        self.is_unchanged.contains(&cache_id)
    }

    pub(crate) fn set_is_unchanged(&mut self, cache_id: u8, is_unchanged: bool) -> bool {
        if is_unchanged {
            self.is_unchanged.insert(cache_id)
        } else {
            self.is_unchanged.remove(cache_id)
        }
    }
}

#[cfg(test)]
mod tests {
    use std::collections::HashSet;

    use super::*;

    const ID_SET_SIZE: u16 = 8;

    fn set() -> IdSet {
        IdSet { free_ranges: FxHashMap::from_iter([(0, ID_SET_SIZE - 1), (ID_SET_SIZE - 1, 0)]) }
    }

    #[test]
    fn acquire_all() {
        let mut set = set();

        for _ in 0..ID_SET_SIZE {
            assert!(set.acquire().is_some());
        }

        assert!(set.acquire().is_none());
    }

    #[test]
    fn acquire_release() {
        let mut set = set();
        let mut removed = HashSet::new();

        for i in 0..ID_SET_SIZE - 1 {
            for _ in i..ID_SET_SIZE {
                removed.insert(set.acquire().unwrap());
            }

            assert_eq!(removed.len(), (ID_SET_SIZE - i) as usize);

            for index in removed.drain() {
                set.release(index);
            }

            assert!(set.acquire().is_some());
        }

        assert!(set.acquire().is_some());
        assert!(set.acquire().is_none());
    }
}

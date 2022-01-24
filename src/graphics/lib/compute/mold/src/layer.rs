// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{cmp::Ordering, convert::TryFrom, error::Error, fmt, iter::FromIterator, mem};

use rustc_hash::FxHashMap;
use surpass::{self, painter::Props, GeomPresTransform, LAYER_LIMIT};

const IDENTITY: &[f32; 6] = &[1.0, 0.0, 0.0, 1.0, 0.0, 0.0];

#[derive(Debug, PartialEq)]
pub enum OrderError {
    ExceededLayerLimit,
}

impl fmt::Display for OrderError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "exceeded layer limit ({})", LAYER_LIMIT)
    }
}

impl Error for OrderError {}

#[derive(Clone, Copy, Debug, Default, Eq, Hash, Ord, PartialEq, PartialOrd)]
pub struct Order(u32);

impl Order {
    pub const MAX: Self = Self(LAYER_LIMIT as u32);

    pub const fn as_u32(&self) -> u32 {
        self.0
    }

    pub const fn new(order: u32) -> Result<Self, OrderError> {
        if order > Self::MAX.as_u32() {
            Err(OrderError::ExceededLayerLimit)
        } else {
            Ok(Self(order))
        }
    }
}

impl TryFrom<u32> for Order {
    type Error = OrderError;

    fn try_from(order: u32) -> Result<Self, OrderError> {
        Self::new(order)
    }
}

impl TryFrom<usize> for Order {
    type Error = OrderError;

    fn try_from(order: usize) -> Result<Self, OrderError> {
        u32::try_from(order).map_err(|_| OrderError::ExceededLayerLimit).and_then(Self::try_from)
    }
}

#[derive(Clone, Copy, Debug)]
struct End {
    index: u32,
    other: u32,
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
    free_ranges: FxHashMap<u32, u32>,
}

impl IdSet {
    pub fn new() -> Self {
        Self {
            free_ranges: FxHashMap::from_iter([(0, Order::MAX.as_u32()), (Order::MAX.as_u32(), 0)]),
        }
    }

    fn first(&self) -> Option<End> {
        self.free_ranges.iter().next().map(|(&index, &other)| End { index, other })
    }

    fn get(&self, index: u32) -> Option<End> {
        self.free_ranges.get(&index).map(|&other| End { index, other })
    }

    fn insert(&mut self, end: End) {
        self.free_ranges.insert(end.index, end.other);
    }

    fn remove(&mut self, end: End) {
        self.free_ranges.remove(&end.index);
    }

    pub fn acquire(&mut self) -> Option<u32> {
        self.first().map(|end| {
            self.remove(end);

            if let Some(next) = end.next() {
                self.insert(next);
                self.insert(next.other());
            }

            end.index
        })
    }

    pub fn release(&mut self, index: u32) {
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
    pub inner: surpass::Layer,
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
    pub fn transform(&self) -> GeomPresTransform {
        self.inner
            .affine_transform
            .map(GeomPresTransform::from_affine)
            .flatten()
            .unwrap_or_default()
    }

    #[inline]
    pub fn set_transform(&mut self, transform: GeomPresTransform) -> &mut Self {
        // We want to perform a cheap check for the common case without hampering this function too
        // much.
        #[allow(clippy::float_cmp)]
        let affine_transform =
            if transform.as_slice() == IDENTITY { None } else { Some(*transform.as_slice()) };

        if self.inner.affine_transform != affine_transform {
            self.is_unchanged.clear();
            self.inner.affine_transform = affine_transform;
        }

        self
    }

    #[inline]
    pub fn order(&self) -> u32 {
        self.inner.order.expect("Layers should always have orders")
    }

    #[inline]
    pub fn set_order(&mut self, order: Order) -> &mut Self {
        if self.inner.order != Some(order.as_u32()) {
            self.is_unchanged.clear();
            self.inner.order = Some(order.as_u32());
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
    use super::*;

    use std::collections::HashSet;

    const ID_SET_SIZE: u32 = 8;

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

    #[test]
    fn wrong_u32_order_value() {
        let order = Order::MAX.as_u32() + 1;

        assert_eq!(Order::try_from(order), Err(OrderError::ExceededLayerLimit));
    }

    #[test]
    fn wrong_usize_order_values() {
        let order = (Order::MAX.as_u32() + 1) as usize;

        assert_eq!(Order::try_from(order), Err(OrderError::ExceededLayerLimit));

        let order = u64::MAX as usize;

        assert_eq!(Order::try_from(order), Err(OrderError::ExceededLayerLimit));
    }

    #[test]
    fn correct_order_value() {
        let order_value = Order::MAX.as_u32();
        let order = Order::try_from(order_value);

        assert_eq!(order, Ok(Order(order_value as u32)));
    }
}

// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::cmp::{max, min};
use std::ops::Range;

/// Provides convenience methods for [`Range`].
pub trait RangeExt {
    /// Returns the intersection of self and rhs. If there is no intersection, the result will
    /// return true for .is_empty().
    fn intersect(&self, rhs: &Self) -> Self;
}

impl<K: Ord + Copy> RangeExt for Range<K> {
    fn intersect(&self, rhs: &Self) -> Self {
        max(self.start, rhs.start)..min(self.end, rhs.end)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn no_intersection() {
        let a = 1..3;
        let b = 3..5;
        assert!(a.intersect(&b).is_empty());
        assert!(b.intersect(&a).is_empty());
    }

    #[test]
    fn partial_intersection() {
        let a = 1..3;
        let b = 2..5;
        assert_eq!(a.intersect(&b), (2..3));
        assert_eq!(b.intersect(&a), (2..3));
    }

    #[test]
    fn full_intersection() {
        let a = 1..4;
        let b = 2..3;
        assert_eq!(a.intersect(&b), (2..3));
        assert_eq!(b.intersect(&a), (2..3));

        let c = 2..4;
        assert_eq!(a.intersect(&c), (2..4));
        assert_eq!(c.intersect(&a), (2..4));

        let d = 1..2;
        assert_eq!(a.intersect(&d), (1..2));
        assert_eq!(d.intersect(&a), (1..2));

        assert_eq!(a.intersect(&a), (1..4));
    }
}

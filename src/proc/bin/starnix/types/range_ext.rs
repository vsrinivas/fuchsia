// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::ops::Range;

/// Provides convenience methods for [`Range`].
pub trait RangeExt {
    /// Returns `true` if this `Range` intersects `rhs`.
    fn intersects(&self, rhs: &Self) -> bool;
}

impl<K> RangeExt for Range<K>
where
    K: Ord,
{
    fn intersects(&self, rhs: &Range<K>) -> bool {
        if self.start < rhs.start {
            rhs.start < self.end
        } else {
            self.start < rhs.end
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn no_intersection() {
        let a = 1..3;
        let b = 3..5;
        assert!(!a.intersects(&b));
        assert!(!b.intersects(&a));
    }

    #[test]
    fn partial_intersection() {
        let a = 1..3;
        let b = 2..5;
        assert!(a.intersects(&b));
        assert!(b.intersects(&a));
    }

    #[test]
    fn full_intersection() {
        let a = 1..4;
        let b = 2..3;
        assert!(a.intersects(&b));
        assert!(b.intersects(&a));

        let c = 2..4;
        assert!(a.intersects(&c));
        assert!(c.intersects(&a));

        let d = 1..2;
        assert!(a.intersects(&d));
        assert!(d.intersects(&a));

        assert!(a.intersects(&a));
    }
}

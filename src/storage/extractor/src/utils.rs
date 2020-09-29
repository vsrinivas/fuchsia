// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{
    io::{Read, Seek},
    ops::Range,
};

pub trait ReadAndSeek: Read + Seek {}
impl<T> ReadAndSeek for T where T: Read + Seek {}

pub trait RangeOps<T> {
    /// Returns `true` if a slice of self is contained in the |other|.
    fn overlaps(&self, other: &Self) -> bool;

    /// Returns `true` if `start` of one equals `end` of the other.
    /// ```
    /// let a: Range<u64> = 1..10;
    /// let b. Range<u64> = 10..14;
    /// assert!(a.is_adjacent(&b));
    /// ```
    fn is_adjacent(&self, other: &Self) -> bool;

    /// Returns `true` if  all of `other` is contained within `self`.
    /// ```
    /// let a: Range<u64> = 1..10;
    /// let b. Range<u64> = 2..4;
    /// assert!(a.contains_range(&b));
    /// assert!(!b.contains_range(&a));
    /// ```
    fn contains_range(&self, other: &Self) -> bool;
    fn is_valid(&self) -> bool;
    fn length(&self) -> T;
}

impl<T: Copy + std::cmp::Ord + std::ops::Sub<Output = T>> RangeOps<T> for Range<T> {
    fn overlaps(&self, other: &Self) -> bool {
        std::cmp::max(self.start, other.start) < std::cmp::min(self.end, other.end)
    }

    fn is_adjacent(&self, other: &Self) -> bool {
        let min_end = std::cmp::min(self.end, other.end);
        let max_start = std::cmp::max(self.start, other.start);
        min_end == max_start
    }

    fn contains_range(&self, other: &Self) -> bool {
        if self.start >= self.end || other.start >= other.end {
            return false;
        }
        self.start <= other.start && self.end >= other.end
    }

    fn is_valid(&self) -> bool {
        self.start < self.end
    }

    fn length(&self) -> T {
        self.end - self.start
    }
}

#[cfg(test)]
pub(crate) fn get_overlapping_ranges() -> (Range<u64>, Range<u64>) {
    (1..5, 4..8)
}

#[cfg(test)]
pub(crate) fn get_containing_ranges() -> (Range<u64>, Range<u64>) {
    (1..5, 2..4)
}

#[cfg(test)]
pub(crate) fn get_adjacent_ranges() -> (Range<u64>, Range<u64>) {
    (1..5, 5..8)
}

#[cfg(test)]
pub(crate) fn get_non_overlapping_ranges() -> (Range<u64>, Range<u64>) {
    (1..5, 6..8)
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn test_range_overlaps() {
        let (r1, r2) = get_overlapping_ranges();
        assert!(r1.overlaps(&r2));
        assert!(r2.overlaps(&r1));

        // Overlaps with itself.
        assert!(r1.overlaps(&r1));

        // Range completely contains the other.
        let (r3, r4) = get_containing_ranges();
        assert!(r4.overlaps(&r3));
        assert!(r3.overlaps(&r4));
    }

    #[test]
    fn test_range_do_not_overlaps() {
        // Adjacent ranges
        let (r1, r2) = get_adjacent_ranges();
        assert!(!r1.overlaps(&r2));
        assert!(!r2.overlaps(&r1));

        // Non-overlapping
        let (r3, r4) = get_non_overlapping_ranges();
        assert!(!r4.overlaps(&r3));
        assert!(!r3.overlaps(&r4));
    }

    #[test]
    fn test_range_is_adjacent() {
        // Adjacent ranges
        let (r1, r2) = get_adjacent_ranges();
        assert!(r1.is_adjacent(&r2));
        assert!(r2.is_adjacent(&r1));

        // Non-overlapping
        let (r3, r4) = get_non_overlapping_ranges();
        assert!(!r4.is_adjacent(&r3));
        assert!(!r3.is_adjacent(&r4));

        // Overlapping
        let (r5, r6) = get_overlapping_ranges();
        assert!(!r5.is_adjacent(&r6));
        assert!(!r6.is_adjacent(&r5));
        assert!(!r5.is_adjacent(&r5));

        // Range completely contains the other.
        let (r7, r8) = get_containing_ranges();
        assert!(!r7.is_adjacent(&r8));
        assert!(!r8.is_adjacent(&r7));
    }

    #[test]
    fn test_range_contains_range() {
        // r1 has r2
        let (r1, r2) = get_containing_ranges();

        // One doesn't contains itself.
        assert!(r1.contains_range(&r1));

        assert!(r1.contains_range(&r2));
        // But doesn't have r1.
        assert!(!r2.contains_range(&r1));

        // Overlapping
        let (r3, r4) = get_overlapping_ranges();
        assert!(!r3.contains_range(&r4));
        assert!(!r4.contains_range(&r3));

        // Non-overlapping
        let (r5, r6) = get_non_overlapping_ranges();
        assert!(!r5.contains_range(&r6));
        assert!(!r6.contains_range(&r5));

        // Adjacent ranges
        let (r7, r8) = get_adjacent_ranges();
        assert!(!r7.contains_range(&r8));
        assert!(!r8.contains_range(&r7));
    }

    #[test]
    fn test_range_is_valid() {
        let r1: Range<u64> = 1..8;
        assert!(r1.is_valid());

        let r2: Range<u64> = 1..2;
        assert!(r2.is_valid());

        let r3: Range<u64> = 0..0;
        assert!(!r3.is_valid());

        let r4: Range<u64> = 1..1;
        assert!(!r4.is_valid());

        let r5: Range<u64> = 5..2;
        assert!(!r5.is_valid());
    }

    #[test]
    fn test_range_length() {
        let r1: Range<u64> = 1..8;
        assert_eq!(r1.length(), 7);

        let r2: Range<u64> = 1..2;
        assert_eq!(r2.length(), 1);
    }

    #[should_panic]
    #[test]
    fn test_invalid_range_length() {
        // Invalid range length
        let r1: Range<u64> = 8..1;
        r1.length();
    }
}

// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use rand::{rngs::SmallRng, Rng};

#[derive(Clone, Eq, PartialEq)]
pub struct VSliceRange {
    // The slice index that this virtual range begins at
    pub start: u64,

    // The slice index that this virtual range ends at (non-inclusive)
    pub end: u64,
}

impl VSliceRange {
    pub fn new(start: u64, end: u64) -> Self {
        assert!(end > start);
        Self { start, end }
    }

    pub fn len(&self) -> u64 {
        self.end - self.start
    }

    pub fn does_overlap(&self, other: &Self) -> bool {
        // Netiher the start nor the end of |other| range should exist inside |self|
        (self.start <= other.start && other.start < self.end)
            || (self.start < other.end && other.end <= self.end)
    }

    // Returns a subrange that ends at the same position, but
    // may be smaller.
    pub fn shrink_from_start(&self, rng: &mut SmallRng) -> VSliceRange {
        let start = rng.gen_range(self.start..self.end);
        VSliceRange::new(start, self.end)
    }

    // Returns a subrange that starts at the same position, but
    // may be smaller.
    pub fn shrink_from_end(&self, rng: &mut SmallRng) -> VSliceRange {
        let end = rng.gen_range(self.start..self.end) + 1;
        VSliceRange::new(self.start, end)
    }

    // Returns a subrange that may be smaller
    pub fn subrange(&self, rng: &mut SmallRng) -> VSliceRange {
        let start = rng.gen_range(self.start..self.end);
        let end = rng.gen_range(start..self.end) + 1;
        VSliceRange::new(start, end)
    }
}

impl std::fmt::Debug for VSliceRange {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "[{}:{})", self.start, self.end)
    }
}

pub struct VSliceRanges {
    // List of virtual slice ranges, sorted by start slice index.
    ranges: Vec<VSliceRange>,

    // Maximum number of allocatable virtual slices
    max_vslice_count: u64,

    // Limits the maximum slices in a single extend operation
    max_slices_in_extend: u64,
}

impl VSliceRanges {
    pub fn new(max_vslice_count: u64, max_slices_in_extend: u64) -> Self {
        Self { ranges: vec![], max_vslice_count, max_slices_in_extend }
    }

    // returns index of the range to the right of |new_range| after insertion.
    pub fn get_insert_position(&self, new_range: &VSliceRange) -> usize {
        for (i, range) in self.ranges.iter().enumerate() {
            assert!(!range.does_overlap(new_range));

            // A new slice will be inserted at position |i| if the i'th range
            // starts after the new range ends.
            //
            //         |------new-------|
            // -----|                      |---------i----------|   |--------i+1---------|
            if new_range.end <= range.start {
                return i;
            }
        }

        // This element must be inserted at the end
        self.ranges.len()
    }

    // Return the indices of extents that have empty space next to them, paired with the ranges
    // that they could be extended into.
    //
    //                             |----------X-----------|
    // |-----------i--------------|                        |-----------i+1-----------|
    //
    // In the above scenario, (i, range X) is returned
    pub fn extensions(&self) -> Vec<(usize, VSliceRange)> {
        assert!(self.ranges.len() > 0);

        let mut iter = self.ranges.iter().enumerate().peekable();
        let mut extensions = vec![];

        // Create extensions between two consecutive allocated ranges
        while let Some((index, cur_range)) = iter.next() {
            if let Some((_, next_range)) = iter.peek() {
                let end = if next_range.start == cur_range.end {
                    // There is no space between these ranges
                    continue;
                } else if next_range.start - cur_range.end > self.max_slices_in_extend {
                    // There is too much space between these ranges.
                    // Limit to |max_slices_in_extend|.
                    cur_range.end + self.max_slices_in_extend
                } else {
                    next_range.start
                };

                let extension = VSliceRange::new(cur_range.end, end);
                extensions.push((index, extension));
            }
        }

        let last_index = self.ranges.len() - 1;
        let last_range = self.ranges.last().unwrap();

        // If possible, create an extension from the last slice
        if last_range.end < self.max_vslice_count {
            let end = if self.max_vslice_count - last_range.end > self.max_slices_in_extend {
                // There is too much space at the end.
                // Limit to |max_slices_in_extend|.
                last_range.end + self.max_slices_in_extend
            } else {
                self.max_vslice_count
            };
            let extension = VSliceRange::new(last_range.end, end);
            extensions.push((last_index, extension));
        }

        extensions
    }

    pub fn is_empty(&self) -> bool {
        self.ranges.is_empty()
    }

    pub fn get_mut(&mut self, index: usize) -> &mut VSliceRange {
        self.ranges.get_mut(index).unwrap()
    }

    pub fn get(&self, index: usize) -> &VSliceRange {
        self.ranges.get(index).unwrap()
    }

    pub fn insert(&mut self, range: VSliceRange) {
        let insert_pos = self.get_insert_position(&range);
        self.ranges.insert(insert_pos, range);
    }

    pub fn remove(&mut self, index: usize) {
        self.ranges.remove(index);
    }

    pub fn random_index(&self, rng: &mut SmallRng) -> usize {
        rng.gen_range(0..self.ranges.len())
    }
}

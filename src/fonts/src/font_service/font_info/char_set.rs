// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::cmp::Ordering;

type BitmapElement = u64;
const BITMAP_ELEMENT_SIZE: usize = 64;
const MAX_RANGE_GAP: u32 = 2048;

/// Represents an ordered set of code points that begin at [CharSetRange.start]. The largest
/// allowed discontinuity between two consecutive code points in the set is [MAX_RANGE_GAP].
#[derive(Debug, Clone)]
struct CharSetRange {
    start: u32,
    bitmap: Vec<BitmapElement>,
}

impl CharSetRange {
    fn new() -> CharSetRange {
        CharSetRange { start: 0, bitmap: vec![] }
    }

    fn start(&self) -> u32 {
        self.start
    }

    fn end(&self) -> u32 {
        self.start + (self.bitmap.len() * BITMAP_ELEMENT_SIZE) as u32
    }

    fn is_empty(&self) -> bool {
        self.bitmap.is_empty()
    }

    fn add(&mut self, val: u32) {
        assert!(val >= self.start);

        if self.bitmap.is_empty() {
            self.start = val;
        }

        let pos = (val - self.start) as usize;
        let element_pos = pos / BITMAP_ELEMENT_SIZE;

        if element_pos >= self.bitmap.len() {
            self.bitmap.resize(element_pos + 1, 0);
        }

        self.bitmap[element_pos] |= 1 << (pos % BITMAP_ELEMENT_SIZE);
    }

    fn contains(&self, c: u32) -> bool {
        if c < self.start || c >= self.end() {
            false
        } else {
            let index = c as usize - self.start as usize;
            (self.bitmap[index / 64] & (1 << (index % 64))) > 0
        }
    }
}

/// Represents an ordered set of code points.
///
/// TODO(kpozin): Evaluate replacing with `MultiCharRange`, which might be more space-efficient for
/// large sets with few discontinuities.
#[derive(Debug, Clone)]
pub struct CharSet {
    ranges: Vec<CharSetRange>,
}

impl CharSet {
    pub fn new(mut code_points: Vec<u32>) -> CharSet {
        code_points.sort_unstable();

        let mut ranges = vec![];
        let mut range = CharSetRange::new();
        for c in code_points {
            if c != 0 && !range.is_empty() && c >= range.end() + MAX_RANGE_GAP {
                ranges.push(range);
                range = CharSetRange::new();
            }
            range.add(c);
        }
        if !range.is_empty() {
            ranges.push(range)
        }
        CharSet { ranges }
    }

    pub fn contains(&self, c: u32) -> bool {
        match self.ranges.binary_search_by(|r| {
            if r.end() < c {
                Ordering::Less
            } else if r.start() > c {
                Ordering::Greater
            } else {
                Ordering::Equal
            }
        }) {
            Ok(r) => self.ranges[r].contains(c),
            Err(_) => false,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_charset() {
        let charset = CharSet::new(vec![1, 2, 3, 10, 500, 5000, 5001, 10000]);
        assert!(!charset.contains(0));
        assert!(charset.contains(1));
        assert!(charset.contains(2));
        assert!(charset.contains(3));
        assert!(!charset.contains(4));
        assert!(!charset.contains(9));
        assert!(charset.contains(10));
        assert!(charset.contains(500));
        assert!(!charset.contains(501));
        assert!(charset.contains(5000));
        assert!(charset.contains(5001));
        assert!(!charset.contains(5002));
        assert!(!charset.contains(9999));
        assert!(charset.contains(10000));
        assert!(!charset.contains(10001));
    }
}

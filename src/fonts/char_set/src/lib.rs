// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    char_collection::CharCollection,
    std::{cmp::Ordering, convert::TryFrom, iter::Iterator},
};

type BitmapElement = u64;
const BITMAP_ELEMENT_SIZE: usize = 64;
/// This value is optimal for memory use, based on some non-scientific experimentation with
/// real-world font files. (Though compared to `2048`, it does add a few nanoseconds to the average
/// `contains` call.)
const MAX_RANGE_GAP: u32 = 256;

/// Represents an ordered set of code points that begin at [CharSetRange.start]. The largest
/// allowed discontinuity between two consecutive code points in the set is [MAX_RANGE_GAP].
#[derive(Debug, Clone, Hash, Eq, PartialEq)]
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
        assert!(char::try_from(val).is_ok());

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
            (self.bitmap[index / BITMAP_ELEMENT_SIZE] & (1 << (index % BITMAP_ELEMENT_SIZE))) > 0
        }
    }

    fn iter(&self) -> CharSetRangeIterator<'_> {
        CharSetRangeIterator { char_set_range: &self, position: self.start.clone() }
    }
}

struct CharSetRangeIterator<'a> {
    char_set_range: &'a CharSetRange,
    position: u32,
}

impl Iterator for CharSetRangeIterator<'_> {
    type Item = char;

    fn next(&mut self) -> Option<char> {
        while self.position < self.char_set_range.end() {
            if self.char_set_range.contains(self.position) {
                self.position += 1;
                return Some(std::char::from_u32(self.position - 1).unwrap());
            }
            self.position += 1;
        }
        None
    }
}

/// Represents an ordered set of code points.
///
/// TODO(kpozin): Add factory method that takes lazy `Iterator<Item = char>` instead of `Vec`.
/// TODO(kpozin): Enforce `char` values.
#[derive(Debug, Clone, Hash, Eq, PartialEq)]
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

    pub fn is_empty(&self) -> bool {
        self.ranges.is_empty()
    }

    /// Iterate over all the characters in the the `CharSet` in code point order.
    pub fn iter(&self) -> impl Iterator<Item = char> + '_ {
        self.ranges.iter().flat_map(CharSetRange::iter)
    }
}

impl Default for CharSet {
    fn default() -> Self {
        CharSet::new(vec![])
    }
}

impl Into<CharCollection> for &CharSet {
    fn into(self) -> CharCollection {
        // Unwrapping is safe because we know `CharSet` iterates in order.
        CharCollection::from_sorted_chars(self.iter()).unwrap()
    }
}

impl From<CharCollection> for CharSet {
    fn from(value: CharCollection) -> CharSet {
        CharSet::from(&value)
    }
}

impl From<&CharCollection> for CharSet {
    fn from(value: &CharCollection) -> CharSet {
        CharSet::new(value.iter().map(|ch| ch as u32).collect::<Vec<u32>>())
    }
}

#[cfg(test)]
mod tests {
    use {super::*, char_collection::char_collect};

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

        assert_eq!(
            charset.iter().map(|ch| ch as u32).collect::<Vec<u32>>(),
            vec![1, 2, 3, 10, 500, 5000, 5001, 10000]
        );
    }

    #[test]
    fn test_charset_from_char_collection() {
        let collection = char_collect!(0..=0, 2..=2, 13..=13, 32..=126);
        let charset = CharSet::from(&collection);
        assert!([0, 2, 13, 32, 54, 126].iter().all(|c| charset.contains(*c)));
        assert!([1, 11, 19, 127, 10000].iter().all(|c| !charset.contains(*c)));
    }
}

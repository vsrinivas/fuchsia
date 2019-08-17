// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{format_err, Error},
    std::cmp::Ordering,
};

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

    pub fn from_string(s: String) -> Result<CharSet, Error> {
        let mut code_points: Vec<u32> = vec![];
        let mut prev: u32 = 0;
        for range in s.split(',').filter(|x| !x.is_empty()) {
            let mut split = range.split('+');

            let offset: u32 = match split.next() {
                Some(off) => off
                    .parse()
                    .or_else(|_| Err(format_err!("Failed to parse {:?} as u32.", off)))?,
                None => return Err(format_err!("Failed to parse {:?}: not a valid range.", range)),
            };

            let length: u32 = match split.next() {
                Some(len) => len
                    .parse()
                    .or_else(|_| Err(format_err!("Failed to parse {:?} as u32.", len)))?,
                None => 0, // We can treat "0,2,..." as "0+0,2+0,..."
            };

            if split.next().is_some() {
                return Err(format_err!("Failed to parse {:?}: not a valid range.", range));
            }

            let begin = prev + offset;
            prev = begin + length;
            code_points.extend(begin..=prev);
        }
        Ok(CharSet::new(code_points))
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
}

impl Default for CharSet {
    fn default() -> Self {
        CharSet::new(vec![])
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

    #[test]
    fn test_charset_from_string() -> Result<(), Error> {
        let s = "0,2,11,19+94".to_string();
        let charset = CharSet::from_string(s)?;
        assert!([0, 2, 13, 32, 54, 126].into_iter().all(|c| charset.contains(*c)));
        assert!([1, 11, 19, 127, 10000].into_iter().all(|c| !charset.contains(*c)));
        Ok(())
    }

    #[test]
    fn test_charset_from_string_not_a_number() {
        for s in &["0,p,11", "q+1", "3+r"] {
            assert!(CharSet::from_string(s.to_string()).is_err())
        }
    }
}

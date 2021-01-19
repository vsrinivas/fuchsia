// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use rand::distributions::{Distribution, Standard};
use rand::Rng;
use std::fmt;
use std::iter::Iterator;

const CORNER: char = '+';
const HORIZ: char = '-';
const VERT: char = '|';
const PIP: char = '*';
const BLANK: char = ' ';

#[derive(Debug)]
pub struct RollResult(u8);

impl Distribution<RollResult> for Standard {
    fn sample<R: Rng + ?Sized>(&self, rng: &mut R) -> RollResult {
        RollResult(rng.gen_range(1, 7))
    }
}

impl fmt::Display for RollResult {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let pips = [
            [self.0 >= 4, false, self.0 >= 2],
            [self.0 >= 6, self.0 % 2 == 1, self.0 >= 6],
            [self.0 >= 2, false, self.0 >= 4],
        ];

        writeln!(f, "{}{}{}{}{}", CORNER, HORIZ, HORIZ, HORIZ, CORNER)?;
        for row in &pips {
            write!(f, "{}", VERT)?;
            for pip in row {
                write!(f, "{}", if *pip { PIP } else { BLANK })?;
            }
            writeln!(f, "{}", VERT)?;
        }
        write!(f, "{}{}{}{}{}", CORNER, HORIZ, HORIZ, HORIZ, CORNER)?;

        Ok(())
    }
}

/// Iterator that yields N sized vectors of elements from N iterators.
///
/// All iterators must use the same item type. Iteration ends when the end of
/// any iterator is reached. If the number of iterators is known at compile
/// time, consider using itertools crate's izip! macro instead.
pub struct MultiZip<I: Iterator> {
    iters: Vec<I>,
}

pub fn multizip<I: Iterator>(iters: Vec<I>) -> MultiZip<I> {
    MultiZip { iters }
}

impl<I: Iterator> Iterator for MultiZip<I> {
    type Item = Vec<I::Item>;

    fn next(&mut self) -> Option<Self::Item> {
        self.iters.iter_mut().map(|iter| iter.next()).collect()
    }
}

/// Run tests on a target device with "fx run-test rolldice_lib_test"
#[cfg(test)]
mod tests {
    use super::*;
    use std::iter::*;

    #[test]
    fn format_die_one() {
        let expected = "\
+---+
|   |
| * |
|   |
+---+";
        assert_eq!(format!("{}", RollResult(1)), expected);
    }

    #[test]
    fn format_die_two() {
        let expected = "\
+---+
|  *|
|   |
|*  |
+---+";
        assert_eq!(format!("{}", RollResult(2)), expected);
    }
    #[test]
    fn format_die_three() {
        let expected = "\
+---+
|  *|
| * |
|*  |
+---+";
        assert_eq!(format!("{}", RollResult(3)), expected);
    }
    #[test]
    fn format_die_four() {
        let expected = "\
+---+
|* *|
|   |
|* *|
+---+";
        assert_eq!(format!("{}", RollResult(4)), expected);
    }
    #[test]
    fn format_die_five() {
        let expected = "\
+---+
|* *|
| * |
|* *|
+---+";
        assert_eq!(format!("{}", RollResult(5)), expected);
    }

    #[test]
    fn format_die_six() {
        let expected = "\
+---+
|* *|
|* *|
|* *|
+---+";
        assert_eq!(format!("{}", RollResult(6)), expected);
    }

    #[test]
    fn zip_empty_iterators() {
        let mut iter = multizip(vec![empty::<i32>(), empty(), empty()]);
        assert_eq!(iter.next(), None);
    }

    #[test]
    fn zip_single_element_iterators() {
        let mut iter = multizip(vec![once(1), once(2), once(3)]);
        assert_eq!(iter.next(), Some(vec![1, 2, 3]));
        assert_eq!(iter.next(), None);
    }

    #[test]
    fn zip_equal_ranges() {
        let mut iter = multizip(vec![0..2, 2..4, 4..6, 6..8]);
        assert_eq!(iter.next(), Some(vec![0, 2, 4, 6]));
        assert_eq!(iter.next(), Some(vec![1, 3, 5, 7]));
        assert_eq!(iter.next(), None);
    }

    #[test]
    fn zip_mismatched_ranges() {
        let mut iter = multizip(vec![0..3, 2..4, 4..6]);
        assert_eq!(iter.next(), Some(vec![0, 2, 4]));
        assert_eq!(iter.next(), Some(vec![1, 3, 5]));
        assert_eq!(iter.next(), None);

        let mut iter = multizip(vec![0..2, 2..5, 4..6]);
        assert_eq!(iter.next(), Some(vec![0, 2, 4]));
        assert_eq!(iter.next(), Some(vec![1, 3, 5]));
        assert_eq!(iter.next(), None);
    }

    #[test]
    fn zip_lines() {
        let strings = vec![
            String::from("A\nB\nC\n1"),
            String::from("D\nE\nF\n2"),
            String::from("G\nH\nI\n3"),
        ];
        let mut iter = multizip(strings.iter().map(|s| s.lines()).collect());
        assert_eq!(iter.next(), Some(vec!["A", "D", "G"]));
        assert_eq!(iter.next(), Some(vec!["B", "E", "H"]));
        assert_eq!(iter.next(), Some(vec!["C", "F", "I"]));
        assert_eq!(iter.next(), Some(vec!["1", "2", "3"]));
        assert_eq!(iter.next(), None);
    }
}

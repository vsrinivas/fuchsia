// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::ops::{Bound, RangeBounds};

/// Provides `char`-indexed operations on strings.
pub trait CharOps {
    /// Converts the given `char` range to an equivalent byte range for the current string, taking
    /// into account the number of bytes in the UTF-8 encoding of each `char`. If the given range is
    /// invalid for the current string, returns `None`.
    ///
    /// Note that the conversion may be lossy. For example, `"abcd".to_byte_range(..3)` will return
    /// `Some(0..3)`.
    fn to_byte_range<R>(&self, char_range: R) -> Option<(Bound<usize>, Bound<usize>)>
    where
        R: RangeBounds<usize>;
}

impl<S> CharOps for S
where
    S: AsRef<str>,
{
    fn to_byte_range<R>(&self, char_range: R) -> Option<(Bound<usize>, Bound<usize>)>
    where
        R: RangeBounds<usize>,
    {
        // Normalize the bounds to a half-open range.
        let start_char_index = match char_range.start_bound() {
            Bound::Included(start) => *start,
            Bound::Excluded(before_start) => *before_start + 1,
            Bound::Unbounded => 0,
        };

        let end_char_index = match char_range.end_bound() {
            Bound::Included(before_end) => Some(*before_end + 1),
            Bound::Excluded(end) => Some(*end),
            Bound::Unbounded => None,
        };

        let mut char_indices = self.as_ref().char_indices();

        let (before_byte_index, before_char_len) = if start_char_index > 0 {
            char_indices.nth(start_char_index - 1).map(|(i, c)| (i, c.len_utf8()))?
        } else {
            (0usize, 0usize)
        };

        // By first looking at the (n − 1)-st character and then attempting to increment by 1, we're
        // able to calculate the byte position _after the last character_ in the field. If we tried
        // to access the n-th character directly when the field has (n − 1) characters, we'd just
        // get `None`, which is wrong.
        let (start_byte_index, start_char) = match char_indices.next() {
            None => (before_byte_index + before_char_len, None),
            Some((i, c)) => (i, Some(c)),
        };

        match end_char_index {
            None => Some((Bound::Included(start_byte_index), Bound::Unbounded)),
            Some(range_end) => {
                let (mut last_byte_index, mut last_char) = (start_byte_index, start_char);
                let range_len = range_end.checked_sub(start_char_index).unwrap_or(0);
                let end_byte_index = if range_len > 0 {
                    let mut count = 0;
                    // range_len - 1 because we already got the starting character.
                    for (i, (byte_index, ch)) in char_indices.take(range_len - 1).enumerate() {
                        last_byte_index = byte_index;
                        last_char = Some(ch);
                        count = i + 1;
                    }
                    // Add 1 to account for the starting character.
                    if count + 1 < range_len {
                        return None;
                    }
                    last_byte_index + last_char.map_or(0, char::len_utf8)
                } else {
                    start_byte_index
                };
                Some((Bound::Included(start_byte_index), Bound::Excluded(end_byte_index)))
            }
        }
    }
}

pub trait GetChars {
    /// Returns a slice of the given string, where the range is specified in terms of *`char`
    /// indices* (or code point indices), rather than in terms of bytes. Returns `None` if either
    /// endpoint is out of bounds.
    ///
    /// This operation's time complexity is linear (`O(range.end)`).
    fn get_chars<R>(&self, range: R) -> Option<&str>
    where
        R: RangeBounds<usize>;
}

impl<S> GetChars for S
where
    S: AsRef<str>,
{
    fn get_chars<R>(&self, char_range: R) -> Option<&str>
    where
        R: RangeBounds<usize>,
    {
        let byte_range = self.to_byte_range(char_range)?;
        self.as_ref().as_bytes().get(byte_range).and_then(|x| std::str::from_utf8(x).ok())
    }
}

#[cfg(test)]
mod tests {
    use {
        super::{CharOps, GetChars},
        std::ops::Bound,
    };

    const STR: &str = "abcd😀efgh";

    #[test]
    fn to_byte_range_empty_range() {
        assert_eq!("".to_byte_range(0..0), Some((Bound::Included(0), Bound::Excluded(0))));
        assert_eq!("a".to_byte_range(1..1), Some((Bound::Included(1), Bound::Excluded(1))));
        assert_eq!(STR.to_byte_range(0..0), Some((Bound::Included(0), Bound::Excluded(0))));
        assert_eq!(STR.to_byte_range(5..5), Some((Bound::Included(8), Bound::Excluded(8))));
        assert_eq!(STR.to_byte_range(9..9), Some((Bound::Included(12), Bound::Excluded(12))));
        assert_eq!(STR.to_byte_range(10..10), None);
    }

    #[test]
    fn get_chars_range_basics() {
        assert_eq!(STR.get_chars(0..1), Some("a"));
        assert_eq!(STR.get_chars(3..4), Some("d"));
        assert_eq!(STR.get_chars(4..5), Some("😀"));
        assert_eq!(STR.get_chars(2..9), Some("cd😀efgh"));
        assert_eq!(STR.get_chars(8..9), Some("h"));
        assert_eq!(STR.get_chars(0..4), Some("abcd"));
        assert_eq!(STR.get_chars(2..6), Some("cd😀e"));
        assert_eq!(STR.get_chars(0..9), Some(STR));
    }

    #[test]
    fn get_chars_closed_range_basics() {
        assert_eq!(STR.get_chars(0..=0), Some("a"));
        assert_eq!(STR.get_chars(3..=3), Some("d"));
        assert_eq!(STR.get_chars(4..=4), Some("😀"));
        assert_eq!(STR.get_chars(2..=8), Some("cd😀efgh"));
        assert_eq!(STR.get_chars(8..=8), Some("h"));
        assert_eq!(STR.get_chars(0..=3), Some("abcd"));
        assert_eq!(STR.get_chars(2..=5), Some("cd😀e"));
        assert_eq!(STR.get_chars(0..=8), Some(STR));
    }

    #[test]
    fn get_chars_unbounded_range() {
        assert_eq!(STR.get_chars(..), Some(STR));
        assert_eq!(STR.get_chars(5..), Some("efgh"));
    }

    #[test]
    #[allow(clippy::reversed_empty_ranges)]
    fn get_chars_empty_range() {
        assert_eq!("".get(0..0), Some(""));
        assert_eq!(STR.get_chars(0..0), Some(""));
        assert_eq!(STR.get_chars(5..2), Some(""));
        assert_eq!(STR.get_chars(5..=2), Some(""));
        assert_eq!(STR.get_chars(..0), Some(""));
    }

    #[test]
    fn get_chars_out_of_range() {
        assert_eq!(STR.get_chars(30..30), None);
        assert_eq!(STR.get_chars(5..17), None);
        assert_eq!(STR.get_chars(30..35), None);
    }
}

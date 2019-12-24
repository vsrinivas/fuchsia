// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    serde_derive::{Deserialize, Serialize},
    std::{iter::Iterator, ops::RangeInclusive},
};

mod conversions;

pub use crate::conversions::*;

/// A compact representation of a set of unsigned integer ranges.
///
/// The primary use case is succinctly encoding a large set of Unicode code points in JSON.
///
/// If the set of ranges is
///
///     [1..=3, 8..=9, 13..=13, 18..=20]
///
/// the `OffsetString` will be
///
///     "1+2,5+1,4,4+3"
///
/// In each entry, the first number is the offset from the end of the previous range. If the current
/// range has length > 1, then there's a '+x' that shows how much to add to get the upper bound of
/// the range. Note that the range [13, 14) has length 1, so it doesn't get a plus suffix.
#[derive(Debug, Serialize, Deserialize, Eq, PartialEq)]
#[serde(try_from = "String")]
pub struct OffsetString(String);

impl OffsetString {
    /// Tries to construct a new `OffsetString` from a string.
    ///
    /// This method performs basic validation on whether the string is syntactically valid and does
    /// not contain any redundantly short ranges. Returns an `Error` if validation fails.
    pub fn new<T: AsRef<str>>(source: T) -> Result<OffsetString, Error> {
        let mut segment_index = 0;
        for segment in source.as_ref().split(',') {
            let mut endpoints = segment.split('+');

            // Not enough plus signs
            let low = endpoints.next().ok_or_else(|| format_err!("Empty segment"))?;
            let low_int = low.parse::<u32>()?;

            if segment_index > 0 && low_int <= 1 {
                return Err(format_err!("Adjacent ranges must be merged"));
            }

            if let Some(span) = endpoints.next() {
                let span_int = span.parse::<u32>()?;
                if span_int < 1 {
                    return Err(format_err!("Range is too small: {}", &segment));
                }

                // Too many plus signs
                if endpoints.next().is_some() {
                    return Err(format_err!("Invalid segment: {}", &segment));
                }
            }

            segment_index += 1;
        }
        Ok(OffsetString(source.as_ref().to_string()))
    }

    /// Iterate over the numeric ranges in the collection.
    pub fn iter_ranges<'a>(&'a self) -> impl Iterator<Item = RangeInclusive<u32>> + 'a {
        self.0
            .split(',')
            .map(|segment| {
                segment.split('+').map(|s| s.parse::<u32>().unwrap()).collect::<Vec<u32>>()
            })
            .scan(0u32, |offset, parsed_ints| {
                let low = *offset + parsed_ints[0];
                let high = if parsed_ints.len() == 1 { low } else { low + parsed_ints[1] };
                *offset = high;
                Some(low..=high)
            })
    }

    /// Iterate over the individual unsigned integers in the collection.
    pub fn iter<'a>(&'a self) -> impl Iterator<Item = u32> + 'a {
        self.iter_ranges().flat_map(|range| range)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_offset_string_new() {
        assert!(OffsetString::new("0+4,5,11+8").is_ok());
        assert!(OffsetString::new("1+3,5,11+8").is_ok());
    }

    #[test]
    fn test_offset_string_new_bad_string() {
        assert!(OffsetString::new("3+,5,11+8").is_err());
        assert!(OffsetString::new("-5+4,5,11+8").is_err());
        assert!(OffsetString::new("3+1,a,11+8").is_err());
        assert!(OffsetString::new("3+1,5,11+8,").is_err());
    }

    #[test]
    fn test_offset_string_new_bad_offset() {
        assert!(OffsetString::new("0+4,0,5,11+8").is_err());
        assert!(OffsetString::new("0+4,1,5,11+8").is_err());
        assert!(OffsetString::new("0+4,1+3,5,11+8").is_err());
    }

    #[test]
    fn test_offset_string_iter_ranges() -> Result<(), Error> {
        let offset_string = OffsetString::new("0+4,5,11+8")?;
        assert_eq!(
            offset_string.iter_ranges().collect::<Vec<RangeInclusive<u32>>>(),
            vec![0..=4, 9..=9, 20..=28]
        );
        Ok(())
    }

    #[test]
    fn test_offset_string_iter() -> Result<(), Error> {
        let offset_string = OffsetString::new("0+4,5,11+8")?;
        assert_eq!(
            offset_string.iter().collect::<Vec<u32>>(),
            vec![0, 1, 2, 3, 4, 9, 20, 21, 22, 23, 24, 25, 26, 27, 28]
        );
        Ok(())
    }
}

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    char_collection::{CharCollection, MultiCharRange},
    char_set::CharSet,
    failure::{format_err, Error},
    serde_derive::{Deserialize, Serialize},
    std::convert::TryFrom,
    unic_char_range::{chars, CharRange},
};

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
pub struct OffsetString(String);

impl From<OffsetString> for String {
    fn from(value: OffsetString) -> String {
        value.0
    }
}

impl From<CharCollection> for OffsetString {
    fn from(value: CharCollection) -> OffsetString {
        OffsetString::from(&value)
    }
}

impl From<&CharCollection> for OffsetString {
    fn from(value: &CharCollection) -> OffsetString {
        let mut prev_high: u32 = 0;
        let mut segments: Vec<String> = Vec::with_capacity(value.range_count());
        for range in value.iter_ranges() {
            let relative_low = range.low as u32 - prev_high;
            let range_size = range.len();
            let segment = if range_size == 1 {
                relative_low.to_string()
            } else {
                format!("{}+{}", relative_low, range_size - 1)
            };
            segments.push(segment);
            prev_high = range.high as u32;
        }
        OffsetString(segments.join(","))
    }
}

impl TryFrom<OffsetString> for CharCollection {
    type Error = failure::Error;

    fn try_from(value: OffsetString) -> Result<Self, Self::Error> {
        CharCollection::try_from(&value)
    }
}

impl TryFrom<&OffsetString> for CharCollection {
    type Error = failure::Error;

    fn try_from(value: &OffsetString) -> Result<Self, Self::Error> {
        let mut offset: u32 = 0;
        let ranges: Result<Vec<CharRange>, Error> = value
            .0
            .split(',')
            .map(|segment| {
                let parsed_ints: Result<Vec<u32>, _> =
                    segment.split('+').map(|s| s.parse::<u32>()).collect();
                parsed_ints
            })
            .map(|parsed_ints| {
                let parsed_ints = parsed_ints?;
                let low = offset + parsed_ints[0];
                let high = if parsed_ints.len() == 1 { low } else { low + parsed_ints[1] };
                offset = high;

                let low_char =
                    std::char::from_u32(low).ok_or_else(|| format_err!("Bad char: {}", low))?;
                let high_char =
                    std::char::from_u32(high).ok_or_else(|| format_err!("Bad char: {}", high))?;

                Ok(chars!(low_char..=high_char))
            })
            .collect();
        return CharCollection::from_sorted_ranges(ranges?);
    }
}

impl From<CharSet> for OffsetString {
    fn from(value: CharSet) -> OffsetString {
        OffsetString::from(&value)
    }
}

impl From<&CharSet> for OffsetString {
    fn from(char_set: &CharSet) -> OffsetString {
        let collection: CharCollection = char_set.into();
        collection.into()
    }
}

impl TryFrom<OffsetString> for CharSet {
    type Error = failure::Error;

    fn try_from(value: OffsetString) -> Result<Self, Self::Error> {
        CharSet::try_from(&value)
    }
}

impl TryFrom<&OffsetString> for CharSet {
    type Error = failure::Error;

    fn try_from(value: &OffsetString) -> Result<Self, Self::Error> {
        CharCollection::try_from(value).map(CharSet::from)
    }
}

#[cfg(test)]
mod tests {
    use {super::*, char_collection::char_collect};

    #[test]
    fn test_char_collection_to_offset_string() {
        let collection = char_collect!(0..=4, 9..=9, 20..=28);
        let offset_string: OffsetString = collection.into();
        assert_eq!(offset_string, OffsetString("0+4,5,11+8".to_string()));

        let collection = char_collect!(3..=4, 9..=9, 20..=28);
        let offset_string: OffsetString = collection.into();
        assert_eq!(offset_string, OffsetString("3+1,5,11+8".to_string()));
    }

    #[test]
    fn test_char_collection_from_offset_string() -> Result<(), Error> {
        assert_eq!(
            char_collect!(0..=4, 9..=9, 20..=28),
            CharCollection::try_from(OffsetString("0+4,5,11+8".to_string()))?
        );

        assert_eq!(
            char_collect!(3..=4, 9..=9, 20..=28),
            CharCollection::try_from(OffsetString("3+1,5,11+8".to_string()))?
        );

        Ok(())
    }

    #[test]
    fn test_char_collection_from_bad_offset_string() {
        assert!(CharCollection::try_from(OffsetString("3+,5,11+8".to_string())).is_err());
        assert!(CharCollection::try_from(OffsetString("-5+4,5,11+8".to_string())).is_err());
        assert!(CharCollection::try_from(OffsetString("3+1,a,11+8".to_string())).is_err());
        assert!(CharCollection::try_from(OffsetString("3+1,5,11+8,".to_string())).is_err());
    }
}

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::OffsetString,
    anyhow::{format_err, Error},
    char_collection::{CharCollection, MultiCharRange},
    char_set::CharSet,
    std::{convert::TryFrom, iter::Iterator},
    unic_char_range::{chars, CharRange},
};

impl TryFrom<String> for OffsetString {
    type Error = anyhow::Error;

    fn try_from(source: String) -> Result<OffsetString, Error> {
        OffsetString::new(source)
    }
}

impl TryFrom<&str> for OffsetString {
    type Error = anyhow::Error;

    fn try_from(source: &str) -> Result<OffsetString, Error> {
        OffsetString::new(source)
    }
}

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
    type Error = anyhow::Error;

    fn try_from(value: OffsetString) -> Result<Self, Self::Error> {
        CharCollection::try_from(&value)
    }
}

impl TryFrom<&OffsetString> for CharCollection {
    type Error = anyhow::Error;

    fn try_from(value: &OffsetString) -> Result<Self, Self::Error> {
        let ranges: Result<Vec<CharRange>, Error> = value
            .iter_ranges()
            .map(|range| {
                let low: u32 = *range.start();
                let high: u32 = *range.end();
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
    type Error = anyhow::Error;

    fn try_from(value: OffsetString) -> Result<Self, Self::Error> {
        CharSet::try_from(&value)
    }
}

impl TryFrom<&OffsetString> for CharSet {
    type Error = anyhow::Error;

    fn try_from(value: &OffsetString) -> Result<Self, Self::Error> {
        Ok(CharSet::new(value.iter().collect()))
    }
}

#[cfg(test)]
mod tests {
    use {super::*, char_collection::char_collect};

    #[test]
    fn test_char_collection_to_offset_string() -> Result<(), Error> {
        let collection = char_collect!(0..=4, 9..=9, 20..=28);
        let offset_string: OffsetString = collection.into();
        assert_eq!(offset_string, OffsetString::new("0+4,5,11+8")?);

        let collection = char_collect!(3..=4, 9..=9, 20..=28);
        let offset_string: OffsetString = collection.into();
        assert_eq!(offset_string, OffsetString("3+1,5,11+8".to_string()));

        Ok(())
    }

    #[test]
    fn test_char_collection_from_offset_string() -> Result<(), Error> {
        assert_eq!(
            char_collect!(0..=4, 9..=9, 20..=28),
            CharCollection::try_from(OffsetString::new("0+4,5,11+8")?)?
        );

        assert_eq!(
            char_collect!(3..=4, 9..=9, 20..=28),
            CharCollection::try_from(OffsetString::new("3+1,5,11+8")?)?
        );

        Ok(())
    }
}

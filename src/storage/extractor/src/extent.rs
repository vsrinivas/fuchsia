// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{error::Error, format::ExtentInfo, properties::ExtentProperties, utils::RangeOps},
    interval_tree::interval::Interval,
    std::convert::TryFrom,
    std::io::Write,
    std::ops::Range,
    zerocopy::AsBytes,
};

#[derive(Debug, Clone)]
pub struct Extent {
    /// storage_range is the offsets this extent maps to. This is the range the
    /// user has provided to us.
    pub storage_range: Range<u64>,

    /// Properties of this extent.
    pub properties: ExtentProperties,

    // When user adds an extent, extent may or may not be accompanied with data.
    // Data is sent if user has already read data from the disk or when user has
    // modified the data.
    data: Option<Box<[u8]>>,
}

impl PartialEq for Extent {
    fn eq(&self, other: &Self) -> bool {
        // We do not care about where this extent maps into image file. So we consider
        // only storage_range and properties and ignore image_range.
        self.storage_range == other.storage_range && self.properties == other.properties
    }
}

impl TryFrom<ExtentInfo> for Extent {
    type Error = Error;

    fn try_from(extent_c: ExtentInfo) -> Result<Self, Self::Error> {
        extent_c.check()?;

        Ok(Extent {
            storage_range: extent_c.start..extent_c.end,
            properties: ExtentProperties {
                extent_kind: extent_c.extent.to_kind()?,
                data_kind: extent_c.data.to_kind()?,
            },
            data: None,
        })
    }
}

impl Extent {
    pub fn new(
        storage_range: Range<u64>,
        properties: ExtentProperties,
        data: Option<Box<[u8]>>,
    ) -> Result<Self, Error> {
        if data.is_some() {
            // We allow contructing extent with invalid range. But invalid range should not
            // accompany with valid data.
            if !storage_range.is_valid() {
                return Err(Error::InvalidRange);
            }

            // If range is valid and data is not None, then data length should match range length.
            let data_length = data.as_ref().unwrap().len() as u64;
            if data_length != storage_range.length() {
                return Err(Error::InvalidDataLength);
            }
        }

        Ok(Extent { storage_range, properties, data })
    }

    // Sets storage_range start.
    // Test only helper routines that reduces boiler plate code.
    #[cfg(test)]
    pub(crate) fn set_start(&mut self, start: u64) {
        self.storage_range.start = start;
    }

    // Sets storage_range end.
    // This function is available only for tests. It reduces boilerplate.
    #[cfg(test)]
    pub(crate) fn set_end(&mut self, end: u64) {
        self.storage_range.end = end;
    }

    /// Returns storage range of this extent.
    pub fn storage_range(&self) -> &Range<u64> {
        &self.storage_range
    }

    /// Returns properties of this extent.
    pub fn properties(&self) -> &ExtentProperties {
        &self.properties
    }

    pub fn serialized_size(&self) -> u64 {
        let c_extent = ExtentInfo::from(self.clone());
        c_extent.as_bytes().len() as u64
    }

    pub fn write(&self, out_stream: &mut dyn Write) -> Result<u64, Error> {
        let c_extent = ExtentInfo::from(self.clone());
        out_stream.write_all(c_extent.as_bytes()).unwrap();
        Ok(c_extent.as_bytes().len() as u64)
    }

    /// Two extents are considered mergeable if they
    /// * either overlap or are is_adjacent, and
    /// * they have same set of properties
    fn is_mergeable(&self, other: &Self) -> bool {
        (self.storage_range.overlaps(&other.storage_range)
            || self.storage_range.is_adjacent(&other.storage_range))
            && self.properties() == other.properties()
            && self.data == other.data
    }

    /// Merge data of two mergable extents.
    /// TODO(auradkar).
    fn merge_data(&self, other: &Self) -> Option<Box<[u8]>> {
        debug_assert!(self.is_mergeable(&other));

        match &self.data {
            Some(_data) => {
                todo!("Adding data along with extent is not yet supported");
            }
            None => None,
        }
    }
}

impl AsRef<Range<u64>> for Extent {
    fn as_ref(&self) -> &Range<u64> {
        self.storage_range()
    }
}

impl Interval<u64> for Extent {
    fn clone_with(&self, new_range: &Range<u64>) -> Self {
        Self { storage_range: new_range.clone(), properties: self.properties.clone(), data: None }
    }

    fn has_mergeable_properties(&self, other: &Self) -> bool {
        self.properties() == other.properties() && self.data == other.data
    }

    /// Merge two mergeable extents.
    fn merge(&self, other: &Self) -> Self {
        assert!(self.is_mergeable(&other));
        let storage_range = std::cmp::min(self.storage_range.start, other.storage_range.start)
            ..std::cmp::max(self.storage_range.end, other.storage_range.end);
        Extent::new(storage_range, self.properties().clone(), self.merge_data(&other)).unwrap()
    }

    /// Returns `true` if self has higher priority that the other.
    ///
    /// Asserts that the two extents overlap.
    fn overrides(&self, other: &Self) -> bool {
        assert!(self.storage_range().overlaps(other.storage_range()));
        self.properties.overrides(&other.properties)
    }
}

#[cfg(test)]
mod test {
    use {
        crate::{
            extent::Extent,
            properties::{DataKind, ExtentKind, ExtentProperties},
            utils::RangeOps,
            utils::*,
        },
        interval_tree::interval::Interval,
        std::ops::Range,
    };

    fn get_same_properties() -> (ExtentProperties, ExtentProperties) {
        let p = ExtentProperties { extent_kind: ExtentKind::Data, data_kind: DataKind::Unmodified };
        (p, p)
    }
    fn get_different_properties() -> (ExtentProperties, ExtentProperties) {
        (
            ExtentProperties { extent_kind: ExtentKind::Data, data_kind: DataKind::Unmodified },
            ExtentProperties { extent_kind: ExtentKind::Pii, data_kind: DataKind::Unmodified },
        )
    }

    // First property's priority is lower.
    fn get_lower_priority_properties() -> (ExtentProperties, ExtentProperties) {
        let (a, b) = (
            ExtentProperties { extent_kind: ExtentKind::Data, data_kind: DataKind::Unmodified },
            ExtentProperties { extent_kind: ExtentKind::Pii, data_kind: DataKind::Unmodified },
        );
        assert!(a < b);
        (a, b)
    }

    fn get_extents(
        ranges: (Range<u64>, Range<u64>),
        properties: (ExtentProperties, ExtentProperties),
    ) -> (Extent, Extent) {
        (
            Extent::new(ranges.0, properties.0, None).unwrap(),
            Extent::new(ranges.1, properties.1, None).unwrap(),
        )
    }

    #[test]
    fn test_extent_info_partial_eq() {
        let (mut extent1, extent2) = get_extents((2..50, 30..80), get_different_properties());

        assert_eq!(extent1, extent1);

        // Different properties should make info unequal.
        extent1.storage_range = extent2.storage_range.clone();
        assert_ne!(extent1, extent2);

        // Different storage_range should make info unequal.
        extent1.storage_range.start = extent1.storage_range.start + 2;
        extent1.properties = extent2.properties;
        assert_ne!(extent1, extent2);
    }

    #[test]
    fn test_extent_is_mergeable() {
        let (extent1, extent2) = get_extents(get_overlapping_ranges(), get_same_properties());
        assert!(extent1.is_mergeable(&extent1));
        assert!(extent1.is_mergeable(&extent2));
        assert!(extent2.is_mergeable(&extent1));

        let (extent3, extent4) = get_extents(get_adjacent_ranges(), get_same_properties());
        assert!(extent3.is_mergeable(&extent4));
        assert!(extent4.is_mergeable(&extent3));

        let (extent5, extent6) = get_extents(get_containing_ranges(), get_same_properties());
        assert!(extent5.is_mergeable(&extent6));
        assert!(extent6.is_mergeable(&extent5));

        let (extent7, extent8) = get_extents(get_non_overlapping_ranges(), get_same_properties());
        assert!(!extent7.is_mergeable(&extent8));
        assert!(!extent8.is_mergeable(&extent7));
    }

    #[test]
    fn test_extent_non_is_mergeable() {
        let (extent1, extent2) = get_extents(get_overlapping_ranges(), get_different_properties());
        assert!(!extent1.is_mergeable(&extent2));
        assert!(!extent2.is_mergeable(&extent1));

        let (extent3, extent4) = get_extents(get_adjacent_ranges(), get_different_properties());
        assert!(!extent3.is_mergeable(&extent4));
        assert!(!extent4.is_mergeable(&extent3));

        let (extent5, extent6) = get_extents(get_containing_ranges(), get_different_properties());
        assert!(!extent5.is_mergeable(&extent6));
        assert!(!extent6.is_mergeable(&extent5));

        let (extent7, extent8) =
            get_extents(get_non_overlapping_ranges(), get_different_properties());
        assert!(!extent7.is_mergeable(&extent8));
        assert!(!extent8.is_mergeable(&extent7));
    }

    fn split_merge_verify(
        case_number: u32,
        extent1: &Extent,
        extent2: &Extent,
        return_extent: Option<Extent>,
        expected: &Vec<Extent>,
    ) {
        let mut found = vec![];
        let ret = extent1.split_or_merge(extent2, &mut found);
        match return_extent {
            Some(x) => {
                assert!(
                    ret.is_some(),
                    "Case Number:{}\nExpected: {:?}\nFound: None",
                    case_number,
                    x,
                );
                assert_eq!(x, ret.unwrap(), "Case number: {}", case_number);
            }
            None => assert!(
                ret.is_none(),
                "Case number: {}\nExpected: None\nFound: {:?}",
                case_number,
                ret,
            ),
        }
        assert_eq!(
            expected.len(),
            found.len(),
            "Case Number:{}\nExpected: {:?}\nFound: {:?}",
            case_number,
            expected,
            found
        );
        for (i, e) in expected.iter().enumerate() {
            assert_eq!(
                e.clone(),
                found[i],
                "Case number:{} Expected: {:?}\nFound: {:?}",
                case_number,
                expected,
                found
            );
        }
    }

    struct MergeSplitCase {
        extent1: Extent,
        extent2: Extent,
        expected_result: Option<Extent>,
        result: Vec<Extent>,
    }

    fn merge_split_case(
        ext1: (Range<u64>, ExtentProperties),
        ext2: (Range<u64>, ExtentProperties),
        return_ext: Option<(Range<u64>, ExtentProperties)>,
        result: Vec<(Range<u64>, ExtentProperties)>,
    ) -> MergeSplitCase {
        let mut res = vec![];
        for e in result {
            res.push(Extent::new(e.0, e.1, None).unwrap());
        }

        MergeSplitCase {
            extent1: Extent::new(ext1.0, ext1.1, None).unwrap(),
            extent2: Extent::new(ext2.0, ext2.1, None).unwrap(),
            expected_result: match return_ext {
                Some(x) => Some(Extent::new(x.0, x.1, None).unwrap()),
                None => None,
            },
            result: res,
        }
    }

    fn verify_merge_split_case(case_number: usize, case: &MergeSplitCase) {
        assert!(case.extent1.storage_range().is_valid());
        assert!(case.extent2.storage_range().is_valid());
        if case.expected_result.is_some() {
            assert!(case.expected_result.clone().unwrap().storage_range().is_valid());
        }
        for e in &case.result {
            assert!(e.storage_range().is_valid());
        }

        split_merge_verify(
            case_number as u32,
            &case.extent1,
            &case.extent2,
            case.expected_result.clone(),
            &case.result,
        );
    }

    #[test]
    fn test_extent_split_merge() {
        use merge_split_case as case;
        let (low, high) = get_lower_priority_properties();
        let test_cases: Vec<MergeSplitCase> = vec![
            // Merge with same range and same properties.
            case((1..5, low), (1..5, low), Some((1..5, low)), vec![]),
            // Merge with same range and different properties.
            case((1..5, low), (1..5, high), None, vec![(1..5, high)]),
            case((1..5, high), (1..5, low), Some((1..5, high)), vec![]),
            // Merge is_adjacent.
            case((1..5, low), (5..8, low), Some((1..8, low)), vec![]),
            case((5..8, low), (1..5, low), Some((1..8, low)), vec![]),
            // Non-mergeable is_adjacent and mixed properties.
            case((1..5, low), (5..8, high), Some((1..5, low)), vec![(5..8, high)]),
            case((1..5, high), (5..8, low), Some((1..5, high)), vec![(5..8, low)]),
            case((5..8, low), (1..5, high), Some((5..8, low)), vec![(1..5, high)]),
            case((5..8, high), (1..5, low), Some((5..8, high)), vec![(1..5, low)]),
            // Merge with overlapping ranges. Different combination of properties.
            case((1..5, low), (4..8, low), Some((1..8, low)), vec![]),
            case((4..8, low), (1..5, low), Some((1..8, low)), vec![]),
            case((1..5, low), (4..8, high), None, vec![(1..4, low), (4..8, high)]),
            case((1..5, high), (4..8, low), Some((1..5, high)), vec![(5..8, low)]),
            case((4..8, low), (1..5, high), Some((5..8, low)), vec![(1..5, high)]),
            case((4..8, high), (1..5, low), Some((4..8, high)), vec![((1..4, low))]),
            // One has the other.
            case((1..8, low), (4..6, low), Some((1..8, low)), vec![]),
            case((4..6, low), (1..8, low), Some((1..8, low)), vec![]),
            case((1..8, high), (4..6, low), Some((1..8, high)), vec![]),
            case((4..6, low), (1..8, high), None, vec![(1..8, high)]),
            case((1..8, low), (4..6, high), Some((6..8, low)), vec![(1..4, low), (4..6, high)]),
            case((4..6, high), (1..8, low), Some((4..6, high)), vec![(1..4, low), (6..8, low)]),
        ];

        for (case_number, case) in test_cases.iter().enumerate() {
            verify_merge_split_case(case_number, case);
        }
    }
}

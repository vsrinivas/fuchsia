// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::utils::RangeOps,
    std::{
        cmp::Ord,
        ops::{Range, Sub},
    },
};

pub trait Interval<T: 'static + Copy + Clone + Ord + Sub<Output = T>>: AsRef<Range<T>> {
    // Creates a clone of self with given range.
    fn clone_with(&self, new_range: &Range<T>) -> Self;

    /// Returns range start offset for the interval.
    fn start(&self) -> T {
        self.as_ref().start
    }

    /// Returns range end offset for the interval.
    fn end(&self) -> T {
        self.as_ref().end
    }

    /// Two intervals are is_adjacent if one ends where another starts.
    /// Interval properties don't matter.
    fn is_adjacent(&self, other: &Self) -> bool {
        self.as_ref().is_adjacent(&other.as_ref())
    }

    // Returns true if properties other than range allow merging.
    fn has_mergeable_properties(&self, #[allow(unused)] other: &Self) -> bool {
        true
    }

    /// Two intervals are considered mergeable if they
    /// * either overlap or are is_adjacent, and
    /// * they have same set of properties
    fn is_mergeable(&self, other: &Self) -> bool {
        (self.as_ref().overlaps(&other.as_ref()) || self.as_ref().is_adjacent(&other.as_ref()))
            && self.has_mergeable_properties(&other)
    }

    /// Merge two mergeable intervals.
    fn merge(&self, other: &Self) -> Self;

    /// Two intervals overlap if their ranges overlap. Interval
    /// properties don't matter.
    fn overlaps(&self, other: &Self) -> bool {
        self.as_ref().overlaps(other.as_ref())
    }

    /// Returns `true` if self has higher priority that the other.
    ///
    /// Asserts that the two intervals overlap.
    fn overrides(&self, #[allow(unused)] other: &Self) -> bool {
        true
    }

    /// Splits or merges two intervals based on the range() and properties of
    /// the intervals.
    ///
    /// Function returns remaining part of self whose range().start is greater
    /// than or equal to other.range().end. Any other intervals that should be
    /// kept from either `self` or `other` are appended to `result`.
    /// If self is entirely consumed, it returns None.
    ///
    /// Function asserts that the two intervals overlap.
    ///
    /// | self          | other         | return        | result                         |
    /// |---------------|---------------|---------------|--------------------------------|
    /// | // Merge with same range and same properties. |  |  |                          |
    /// | (1..5, &low)  | (1..5, &low)  | (1..5, &low)  | []                             |
    /// | // Merge with same range and different properties. |  |  |                     |
    /// | (1..5, &low)  | (1..5, &high) | None          | [(1..5, &high)]                |
    /// | (1..5, &high) | (1..5, &low)  | (1..5, &high) | []                             |
    /// | // Merge is_adjacent. |       |               |                                |
    /// | (1..5, &low)  | (5..8, &low)  | (1..8, &low)  | []                             |
    /// | (5..8, &low)  | (1..5, &low)  | (1..8, &low)  | []                             |
    /// | // Non-mergeable is_adjacent and mixed properties. | | |                       |
    /// | (1..5, &low)  | (5..8, &high) | (1..5, &low)  | [(5..8, &high)]                |
    /// | (1..5, &high) | (5..8, &low)  | (1..5, &high) | [(5..8, &low)]                 |
    /// | (5..8, &low)  | (1..5, &high) | (5..8, &low)  | [(1..5, &high)]                |
    /// | (5..8, &high) | (1..5, &low)  | (5..8, &high) | [(1..5, &low)]                 |
    /// | // Merge with overlapping ranges. Different combination of properties. |||     |
    /// | (1..5, &low)  | (4..8, &low)  | (1..8, &low)  | []                             |
    /// | (4..8, &low)  | (1..5, &low)  | (1..8, &low)  | []                             |
    /// | (1..5, &low)  | (4..8, &high) | None,         | [(1..4, &low), (4..8, &high)]  |
    /// | (1..5, &high) | (4..8, &low)  | (1..5, &high) | [(5..8, &low)]                 |
    /// | (4..8, &low)  | (1..5, &high) | (5..8, &low)  | [(1..5, &high)]                |
    /// | (4..8, &high) | (1..5, &low)  | (4..8, &high) | [((1..4, &low)]                |
    /// | // One has the other. |       |               |                                |
    /// | (1..8, &low)  | (4..6, &low)  | (1..8, &low)  | []                             |
    /// | (4..6, &low)  | (1..8, &low)  | (1..8, &low)  | []                             |
    /// | (1..8, &high) | (4..6, &low)  | (1..8, &high) | []                             |
    /// | (4..6, &low)  | (1..8, &high) | None          | [(1..8, &high)]                |
    /// | (1..8, &low)  | (4..6, &high) | (6..8, &low)  | [(1..4, &low), (4..6, &high)], |
    /// | (4..6, &high) | (1..8, &low)  | (4..6, &high) | [(1..4, &low), (6..8, &low)]   |
    ///
    fn split_or_merge(&self, other: &Self, result: &mut Vec<Self>) -> Option<Self>
    where
        Self: Sized + Clone,
    {
        assert!(self.overlaps(other) || self.is_adjacent(other));

        if self.is_mergeable(&other) {
            return Some(self.merge(other));
        }

        // These are adjacent that cannot be merged. Just retain both of them.
        if self.is_adjacent(&other) {
            result.push(other.clone());
            return Some(self.clone());
        }

        // If properties are not the same then self can split the other based on
        // whether properties of self are stronger than other's.
        //
        // If self has stronger properties, then it overrides other for the ranges that overlaps.
        if self.overrides(other) {
            // self swallows all of other.
            if self.as_ref().contains_range(&other.as_ref()) {
                return Some(self.clone());
            }

            // Other starts before self. So a slice from other's start to self's start is
            // retained.
            if self.start() > other.start() {
                result.push(other.clone_with(&(other.start()..self.start())));
            }

            // Other ends after self. So a slice from self's end to other's start is
            // retained.
            if self.end() < other.end() {
                result.push(other.clone_with(&(self.end()..other.end())));
            }

            // The whole of self is retained.
            return Some(self.clone());
        } else {
            // other swallows all of self.
            if other.as_ref().contains_range(self.as_ref()) || self.as_ref() == other.as_ref() {
                result.push(other.clone());
                return None;
            }

            // self start before the other. Some part of self at the beginning is retained.
            if self.start() < other.start() {
                result.push(self.clone_with(&(self.start()..other.start())));
            }
            result.push(other.clone());

            // self ends after the other ends. Some part of self towards the self's end is retained.
            if self.end() > other.end() {
                return Some(self.clone_with(&(other.end()..self.end())));
            }

            // All relevant pieces of self are broken down. None remains.
            None
        }
    }

    /// Calculates the difference between `self.as_ref()` and `other` and returns
    /// (remaining and removed). What is left of self after difference is returned in `remaining`
    /// and what was taken out of self is in `removed`.
    ///
    /// There are three different ways `difference` may affect a other. difference() may
    /// - consume all of self.
    /// - consume a part of self, leaving beginning or end or both ranges from self.
    /// - not affect self at all.
    /// | self          | other         | remaining     | removed           |
    /// |---------------|---------------|---------------|-------------------|
    /// | 1..9          | 1..9          | []            | 1..9              |
    /// | 1..9          | 0..5          | [5..9]        | 1..5              |
    /// | 1..9          | 4..13         | [1..4]        | 4..9              |
    /// | 1..9          | 4..7          | [1..4, 7..9]  | 4..7              |
    /// | 1..9          | 9..17         | [1..9]        | None              |
    fn difference(&self, other: &Range<T>) -> (Vec<Self>, Option<Self>)
    where
        Self: Sized + Clone,
    {
        let range = self.as_ref();
        if !range.overlaps(other) {
            return (vec![self.clone()], None);
        }

        if other.contains_range(range) {
            return (vec![], Some(self.clone()));
        }

        if range.contains_range(other) {
            let removed = self.clone_with(other);
            let mut remaining = vec![];
            if range.start < other.start {
                remaining.push(self.clone_with(&(range.start..other.start)));
            }
            if range.end > other.end {
                remaining.push(self.clone_with(&(other.end..range.end)));
            }
            return (remaining, Some(removed));
        }

        // The ranges overlap such that a part of self's end gets removed.
        if other.start > range.start {
            return (
                vec![self.clone_with(&(range.start..other.start))],
                Some(self.clone_with(&(other.start..range.end))),
            );
        }

        // The ranges overlap such that a part of self's beginning gets removed.
        assert!(other.start < range.start);
        (
            vec![self.clone_with(&(other.end..range.end))],
            Some(self.clone_with(&(range.start..other.end))),
        )
    }
}

#[cfg(test)]
mod test {
    use {
        crate::{interval::Interval, utils::*},
        std::ops::Range,
    };

    #[derive(Debug, Clone)]
    struct TestInterval {
        range: Range<u64>,
        state: bool,
    }
    impl PartialEq for TestInterval {
        fn eq(&self, other: &Self) -> bool {
            self.range == other.range && self.state == other.state
        }
    }
    impl AsRef<Range<u64>> for TestInterval {
        fn as_ref(&self) -> &Range<u64> {
            &self.range
        }
    }

    impl TestInterval {
        fn new(range: Range<u64>, state: bool) -> Self {
            Self { range, state }
        }
    }

    impl Interval<u64> for TestInterval {
        fn clone_with(&self, new_range: &Range<u64>) -> Self {
            Self { range: new_range.clone(), state: self.state }
        }

        fn has_mergeable_properties(&self, other: &Self) -> bool {
            self.state == other.state
        }

        fn merge(&self, other: &Self) -> Self {
            Self { range: self.range.merge(&other.range), state: self.state }
        }

        fn overrides(&self, other: &Self) -> bool {
            self.state == true || self.state == other.state
        }
    }

    fn get_same_properties() -> (bool, bool) {
        (true, true)
    }

    fn get_different_properties() -> (bool, bool) {
        (true, false)
    }

    // First property's priority is lower.
    fn get_lower_priority_properties() -> (bool, bool) {
        (false, true)
    }

    fn get_intervals(
        ranges: (Range<u64>, Range<u64>),
        properties: (bool, bool),
    ) -> (TestInterval, TestInterval) {
        (TestInterval::new(ranges.0, properties.0), TestInterval::new(ranges.1, properties.1))
    }

    #[test]
    fn test_interval_info_partial_eq() {
        let (interval1, interval2) = get_intervals((2..50, 30..80), get_same_properties());

        assert_eq!(interval1, interval1);
        assert_ne!(interval1, interval2);

        // Different properties should make info unequal.
        let (interval1, interval2) = get_intervals((2..50, 2..50), get_different_properties());
        assert_ne!(interval1, interval2);

        // Different properties should make info unequal.
        let (interval1, interval2) = get_intervals((2..50, 2..50), get_lower_priority_properties());
        assert_ne!(interval1, interval2);
    }

    #[test]
    fn test_interval_is_mergeable() {
        let (interval1, interval2) = get_intervals(get_overlapping_ranges(), get_same_properties());
        assert!(interval1.is_mergeable(&interval1));
        assert!(interval1.is_mergeable(&interval2));
        assert!(interval2.is_mergeable(&interval1));

        let (interval3, interval4) = get_intervals(get_adjacent_ranges(), get_same_properties());
        assert!(interval3.is_mergeable(&interval4));
        assert!(interval4.is_mergeable(&interval3));

        let (interval5, interval6) = get_intervals(get_containing_ranges(), get_same_properties());
        assert!(interval5.is_mergeable(&interval6));
        assert!(interval6.is_mergeable(&interval5));

        let (interval7, interval8) =
            get_intervals(get_non_overlapping_ranges(), get_same_properties());
        assert!(!interval7.is_mergeable(&interval8));
        assert!(!interval8.is_mergeable(&interval7));
    }

    #[test]
    fn test_interval_non_is_mergeable() {
        let (interval1, interval2) =
            get_intervals(get_overlapping_ranges(), get_different_properties());
        assert!(!interval1.is_mergeable(&interval2));
        assert!(!interval2.is_mergeable(&interval1));

        let (interval3, interval4) =
            get_intervals(get_adjacent_ranges(), get_different_properties());
        assert!(!interval3.is_mergeable(&interval4));
        assert!(!interval4.is_mergeable(&interval3));

        let (interval5, interval6) =
            get_intervals(get_containing_ranges(), get_different_properties());
        assert!(!interval5.is_mergeable(&interval6));
        assert!(!interval6.is_mergeable(&interval5));

        let (interval7, interval8) =
            get_intervals(get_non_overlapping_ranges(), get_different_properties());
        assert!(!interval7.is_mergeable(&interval8));
        assert!(!interval8.is_mergeable(&interval7));
    }

    fn split_merge_verify(
        case_number: u32,
        interval1: &TestInterval,
        interval2: &TestInterval,
        return_interval: Option<TestInterval>,
        expected: &Vec<TestInterval>,
    ) {
        let mut found = vec![];
        let ret = interval1.split_or_merge(interval2, &mut found);
        match return_interval {
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
        interval1: TestInterval,
        interval2: TestInterval,
        expected_result: Option<TestInterval>,
        result: Vec<TestInterval>,
    }

    fn merge_split_case(
        interval1: (Range<u64>, bool),
        interval2: (Range<u64>, bool),
        return_interval: Option<(Range<u64>, bool)>,
        result: Vec<(Range<u64>, bool)>,
    ) -> MergeSplitCase {
        let mut res = vec![];
        for e in result {
            res.push(TestInterval::new(e.0, e.1));
        }

        MergeSplitCase {
            interval1: TestInterval::new(interval1.0, interval1.1),
            interval2: TestInterval::new(interval2.0, interval2.1),
            expected_result: match return_interval {
                Some(x) => Some(TestInterval::new(x.0, x.1)),
                None => None,
            },
            result: res,
        }
    }

    fn verify_merge_split_case(case_number: usize, case: &MergeSplitCase) {
        assert!(case.interval1.as_ref().is_valid());
        assert!(case.interval2.as_ref().is_valid());
        if case.expected_result.is_some() {
            assert!(case.expected_result.clone().unwrap().as_ref().is_valid());
        }
        for e in &case.result {
            assert!(e.as_ref().is_valid());
        }

        split_merge_verify(
            case_number as u32,
            &case.interval1,
            &case.interval2,
            case.expected_result.clone(),
            &case.result,
        );
    }

    #[test]
    fn test_interval_split_merge() {
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

    #[test]
    fn test_interval_difference() {
        let interval1 = TestInterval { range: 1..9, state: true };
        // Nothing gets removed if ranges don't overlap.
        assert_eq!(interval1.difference(&(9..19)), (vec![interval1.clone()], None));
        assert_eq!(interval1.difference(&(19..29)), (vec![interval1.clone()], None));

        // Everything gets removed if ranges contain each other.
        assert_eq!(interval1.difference(&(1..9)), (vec![], Some(interval1.clone())));
        assert_eq!(interval1.difference(&(0..19)), (vec![], Some(interval1.clone())));

        // Three different cases of overlap.
        let remaining = vec![TestInterval { range: 5..9, state: true }];
        let removed = Some(TestInterval { range: 1..5, state: true });
        assert_eq!(interval1.difference(&(0..5)), (remaining, removed));
        let remaining = vec![TestInterval { range: 1..4, state: true }];
        let removed = Some(TestInterval { range: 4..9, state: true });
        assert_eq!(interval1.difference(&(4..13)), (remaining, removed));
        let remaining = vec![
            TestInterval { range: 1..4, state: true },
            TestInterval { range: 7..9, state: true },
        ];
        let removed = Some(TestInterval { range: 4..7, state: true });
        assert_eq!(interval1.difference(&removed.as_ref().unwrap().range), (remaining, removed));
    }
}

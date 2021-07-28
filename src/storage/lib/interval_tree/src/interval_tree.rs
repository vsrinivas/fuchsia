// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{error::Error, interval::Interval, utils::RangeOps},
    std::{cmp::Ord, collections::BTreeMap, ops::Range, ops::Sub},
};

/// IntervalTree holds a set of intervals by coalescing, splitting, merging if
/// needed.
/// T is the type of interval and U is the type of Range<U> that interval is
/// for.
#[derive(Debug)]
pub struct IntervalTree<T, U>
where
    T: Interval<U> + Sized,
    U: 'static + Copy + Clone + Ord + Sub<Output = U>,
{
    pub(crate) interval_tree: BTreeMap<U, T>,
}

impl<T, U> IntervalTree<T, U>
where
    T: Interval<U> + Sized + Clone,
    U: 'static + Copy + Clone + Ord + Sub<Output = U>,
{
    pub fn new() -> Self {
        Self { interval_tree: Default::default() }
    }

    // Finds all affected intervals and removes them from the tree.
    fn remove_affected(&mut self, range: &Range<U>) -> Vec<T> {
        let mut affected_intervals = vec![];

        // Get all intervals that may be affected by this interval insertion.
        for interval in self.interval_tree.iter_mut() {
            if !(range.overlaps(interval.1.as_ref()) || range.is_adjacent(interval.1.as_ref())) {
                continue;
            }
            affected_intervals.push(interval.1.clone());
        }

        // Remove all the affected intervals.
        for interval in &affected_intervals {
            self.interval_tree.remove(&interval.start());
        }

        affected_intervals
    }

    // insert_interval's logic is as follows.
    //  * create a list of all affected intervals by this insertion.
    //  * for each interval in affected intervals, split/merge with current intervals.
    //  * replace affected and current interval with the new split/merged intervals list.
    fn insert_interval(&mut self, current_interval: T) -> Result<(), Error> {
        let affected_intervals = self.remove_affected(current_interval.as_ref());

        // Perform split/merge of current interval with one affected interval at a time.
        // Note:
        //   1. This is performed on intervals in the ascending order of interval.start.
        //   2. The existing intervals are assumed to be non-overlapping with all other
        //      existing intervals.
        //   3. Order of insertion changes in intermediate state of the tree.
        let mut new_intervals = vec![];
        let mut remaining = Some(current_interval.clone());
        for (i, interval) in affected_intervals.iter().enumerate() {
            assert!(
                current_interval.overlaps(&interval) || current_interval.is_adjacent(&interval)
            );
            // If remaining interval is completely consumed then iterate over the rest of affected
            // intervals and just add them.
            // Ex. say we have already inserted intervals [2, 5), [5, 8), and [8, 12) all having
            // different priorities. Now the current interval is [5, 8) with lower
            // priority than all the the intervals then we may end up consuming current
            // entirely before we parse all of affected_intervals.
            match remaining {
                None => new_intervals.push(interval.clone()),
                Some(remaining_interval) => {
                    remaining = remaining_interval.split_or_merge(&interval, &mut new_intervals);
                    assert!(
                        remaining.is_some() || (i >= affected_intervals.len().saturating_sub(2))
                    );
                }
            }
        }

        // Insert all the split/merged interval into the interval tree.
        for interval in new_intervals {
            self.interval_tree.insert(interval.start(), interval.clone());
        }

        // It may happen that the current interval maybe unaffected or partially affected.
        // If so insert it into the tree.
        match remaining {
            Some(e) => {
                self.interval_tree.insert(e.start(), e);
            }
            _ => {}
        }

        Ok(())
    }

    /// Adds an interval to interval tree.
    pub fn add_interval(&mut self, interval: &T) -> Result<(), Error> {
        if !interval.as_ref().is_valid() {
            return Err(Error::InvalidRange);
        }
        self.insert_interval(interval.clone())
    }

    /// Removes all the intervals that overlap with given `range`. Returns removed interval.
    pub fn remove_interval(&mut self, range: &Range<U>) -> Result<Vec<T>, Error> {
        self.remove_matching_interval(range, |_| true)
    }

    /// Removes all the intervals that overlap with given `range` and match `predicate`. Returns
    /// removed interval.
    pub fn remove_matching_interval<F>(
        &mut self,
        range: &Range<U>,
        predicate: F,
    ) -> Result<Vec<T>, Error>
    where
        F: Fn(&T) -> bool,
    {
        if !range.is_valid() {
            return Err(Error::InvalidRange);
        }
        let affected_intervals = self.remove_affected(range);
        let mut ret = vec![];
        for interval in &affected_intervals {
            if predicate(interval) {
                let (remainings, removed) = interval.difference(range);
                for remaining in remainings {
                    let _ = self.add_interval(&remaining)?;
                }
                if let Some(removed) = removed {
                    // remove_affected also removes adjactent (non-overlapping) ranges, so the
                    // difference might be empty.
                    ret.push(removed);
                }
            } else {
                self.add_interval(interval)?;
            }
        }
        Ok(ret)
    }

    /// Updates the range in interval tree with properties in `interval`.
    /// Existing intervals are updated irrespective of their properties.
    /// Returns old intervals, if any.
    pub fn update_interval(&mut self, interval: &T) -> Result<Vec<T>, Error> {
        let ret = self.remove_interval(interval.as_ref())?;
        let _ = self.add_interval(interval)?;
        Ok(ret)
    }

    /// Returns number of interval in interval tree.
    pub fn interval_count(&self) -> usize {
        self.interval_tree.len()
    }

    /// Returns a list of intervals whose ranges overlap with given `range`.
    /// The function may return an interval which starts/ends before/after given `range`.
    pub fn get_intervals(&self, range: &Range<U>) -> Result<Vec<T>, Error> {
        if !range.is_valid() {
            return Err(Error::InvalidRange);
        }
        let mut affected_intervals = vec![];
        for (_, interval) in self.interval_tree.iter() {
            if range.overlaps(interval.as_ref()) {
                affected_intervals.push(interval.clone());
            }
        }
        Ok(affected_intervals)
    }

    /// Iterate over the interval_tree and check that they are in ascending order
    /// of start offset and they do not overlap.
    pub fn check_interval_tree(&self) {
        let mut prev_or: Option<T> = None;

        for (_, interval) in &self.interval_tree {
            if prev_or.is_some() {
                let prev = prev_or.unwrap();
                assert!(interval.start() >= prev.end());
            }

            assert!(interval.as_ref().is_valid());
            prev_or = Some(interval.clone());
        }
    }

    pub fn get_iter(&self) -> &BTreeMap<U, T> {
        &self.interval_tree
    }
}

#[cfg(test)]
mod test {
    use {super::*, crate::interval_tree::IntervalTree, std::ops::Range};

    #[derive(Debug, Clone)]
    struct TestInterval {
        range: Range<u64>,
        state: u32,
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
        fn new(range: Range<u64>, state: u32) -> Self {
            Self { range, state }
        }

        // Sets storage_range start.
        pub fn set_start(&mut self, start: u64) {
            self.range.start = start;
        }

        // Sets storage_range end.
        pub fn set_end(&mut self, end: u64) {
            self.range.end = end;
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
            self.state >= other.state
        }
    }

    static INSERTED_ADDRESS_START: u64 = 20;
    static INSERTED_ADDRESS_END: u64 = 30;

    static LOW_PRIORITY_PROPERTIES: u32 = 0;
    static HIGH_PRIORITY_PROPERTIES: u32 = 10;
    static INSERTED_PROPERTIES: u32 = 5;

    static OVERLAPPING_RIGHT_ADDRESS: Range<u64> =
        INSERTED_ADDRESS_END - 5..INSERTED_ADDRESS_END + 6;

    fn inserted_range() -> Range<u64> {
        INSERTED_ADDRESS_START..INSERTED_ADDRESS_END
    }

    fn inserted_interval() -> TestInterval {
        TestInterval::new(inserted_range(), INSERTED_PROPERTIES)
    }

    // Non-overlapping interval to the right of inserted_interval().
    fn right_interval() -> TestInterval {
        TestInterval::new(INSERTED_ADDRESS_END + 5..INSERTED_ADDRESS_END + 15, INSERTED_PROPERTIES)
    }

    fn setup_interval_tree() -> (IntervalTree<TestInterval, u64>, Vec<TestInterval>) {
        let mut tree = IntervalTree::new();
        match tree.add_interval(&inserted_interval()) {
            Err(why) => println!("why: {:?}", why),
            Ok(_) => {}
        }
        return (tree, vec![inserted_interval().clone()]);
    }

    fn verify(
        file: &str,
        line: u32,
        tree: &IntervalTree<TestInterval, u64>,
        intervals: &Vec<TestInterval>,
    ) {
        if tree.interval_tree.len() != intervals.len() {
            println!(
                "{}:{} Expected: {:?}\nFound: {:?}",
                file, line, intervals, tree.interval_tree
            );
        }
        assert_eq!(tree.interval_tree.len(), intervals.len());

        for (_, interval) in tree.interval_tree.iter() {
            let mut found = false;
            for inserted in intervals.iter() {
                if inserted == interval {
                    found = true;
                    break;
                }
            }
            if !found {
                println!("Could not find {:?}", *interval);
            }
            assert!(found);
        }
    }

    #[test]
    fn test_setup() {
        let (tree, expected_intervals) = setup_interval_tree();
        assert!(INSERTED_PROPERTIES > LOW_PRIORITY_PROPERTIES);
        assert!(INSERTED_PROPERTIES < HIGH_PRIORITY_PROPERTIES);
        assert!(LOW_PRIORITY_PROPERTIES < HIGH_PRIORITY_PROPERTIES);
        verify(file!(), line!(), &tree, &expected_intervals);
    }

    #[test]
    fn test_add_non_overlapping() {
        let (mut tree, mut expected_intervals) = setup_interval_tree();
        let e = TestInterval::new(40..50, INSERTED_PROPERTIES);
        tree.add_interval(&e).unwrap();
        expected_intervals.push(e);
        verify(file!(), line!(), &tree, &expected_intervals);
    }

    #[test]
    fn test_add_is_adjacent_not_mergable() {
        let (mut tree, mut expected_intervals) = setup_interval_tree();
        let e = TestInterval::new(
            INSERTED_ADDRESS_END..INSERTED_ADDRESS_END + 5,
            LOW_PRIORITY_PROPERTIES,
        );
        tree.add_interval(&e).unwrap();
        expected_intervals.push(e);
        verify(file!(), line!(), &tree, &expected_intervals);
    }

    #[test]
    fn test_add_is_adjacent() {
        let (mut tree, mut expected_intervals) = setup_interval_tree();
        let e =
            TestInterval::new(INSERTED_ADDRESS_END..INSERTED_ADDRESS_END + 5, INSERTED_PROPERTIES);
        tree.add_interval(&e).unwrap();
        expected_intervals.clear();
        expected_intervals.push(TestInterval::new(
            INSERTED_ADDRESS_START..INSERTED_ADDRESS_END + 5,
            INSERTED_PROPERTIES,
        ));
        verify(file!(), line!(), &tree, &expected_intervals);
    }

    #[test]
    fn test_add_adjacent_in_the_middle() {
        let (mut tree, mut expected_intervals) = setup_interval_tree();
        tree.add_interval(&right_interval()).unwrap();
        expected_intervals.push(right_interval().clone());
        verify(file!(), line!(), &tree, &expected_intervals);
        let e =
            TestInterval::new(INSERTED_ADDRESS_END..INSERTED_ADDRESS_END + 5, INSERTED_PROPERTIES);
        tree.add_interval(&e).unwrap();
        expected_intervals.clear();
        expected_intervals.push(TestInterval::new(
            INSERTED_ADDRESS_START..INSERTED_ADDRESS_END + 15,
            INSERTED_PROPERTIES,
        ));
        verify(file!(), line!(), &tree, &expected_intervals);
    }

    #[test]
    fn test_add_adjacent_in_the_middle_not_mergable() {
        let (mut tree, mut expected_intervals) = setup_interval_tree();
        tree.add_interval(&right_interval()).unwrap();
        expected_intervals.push(right_interval().clone());
        verify(file!(), line!(), &tree, &expected_intervals);
        let m = TestInterval::new(
            INSERTED_ADDRESS_END..INSERTED_ADDRESS_END + 5,
            HIGH_PRIORITY_PROPERTIES,
        );
        tree.add_interval(&m).unwrap();
        expected_intervals.push(m);
        verify(file!(), line!(), &tree, &expected_intervals);
    }

    #[test]
    fn test_add_adjacent_on_both_sides_overlapping_in_middle() {
        let (mut tree, mut expected_intervals) = setup_interval_tree();
        // Adjacent to left
        let mut m = TestInterval::new(
            INSERTED_ADDRESS_START - 10..INSERTED_ADDRESS_START,
            LOW_PRIORITY_PROPERTIES,
        );
        tree.add_interval(&m).unwrap();
        expected_intervals.push(m.clone());
        m.set_start(INSERTED_ADDRESS_END);
        m.set_end(INSERTED_ADDRESS_END + 10);
        tree.add_interval(&m).unwrap();
        expected_intervals.push(m.clone());

        verify(file!(), line!(), &tree, &expected_intervals);

        let m = TestInterval::new(
            INSERTED_ADDRESS_START..INSERTED_ADDRESS_END,
            LOW_PRIORITY_PROPERTIES,
        );
        tree.add_interval(&m).unwrap();
        verify(file!(), line!(), &tree, &expected_intervals);
    }

    #[test]
    fn test_add_overlapping() {
        let (mut tree, mut expected_intervals) = setup_interval_tree();
        tree.add_interval(&TestInterval::new(
            OVERLAPPING_RIGHT_ADDRESS.clone(),
            INSERTED_PROPERTIES,
        ))
        .unwrap();
        expected_intervals[0].set_end(OVERLAPPING_RIGHT_ADDRESS.end);
        verify(file!(), line!(), &tree, &expected_intervals);
    }

    #[test]
    fn test_add_overlapping_low_priority() {
        let (mut tree, mut expected_intervals) = setup_interval_tree();
        let mut e = TestInterval::new(OVERLAPPING_RIGHT_ADDRESS.clone(), LOW_PRIORITY_PROPERTIES);
        tree.add_interval(&e).unwrap();
        e.set_start(INSERTED_ADDRESS_END);
        expected_intervals.push(e);
        verify(file!(), line!(), &tree, &expected_intervals);
    }

    #[test]
    fn test_add_overlapping_high_priority() {
        let (mut tree, mut expected_intervals) = setup_interval_tree();
        tree.add_interval(&TestInterval::new(
            OVERLAPPING_RIGHT_ADDRESS.clone(),
            HIGH_PRIORITY_PROPERTIES,
        ))
        .unwrap();
        expected_intervals[0].set_end(OVERLAPPING_RIGHT_ADDRESS.start);
        expected_intervals
            .push(TestInterval::new(OVERLAPPING_RIGHT_ADDRESS.clone(), HIGH_PRIORITY_PROPERTIES));
        verify(file!(), line!(), &tree, &expected_intervals);
    }

    #[test]
    fn test_add_overlapping_in_middlex() {
        let (mut tree, mut expected_intervals) = setup_interval_tree();
        tree.add_interval(&right_interval()).unwrap();
        expected_intervals.push(right_interval().clone());
        verify(file!(), line!(), &tree, &expected_intervals);

        let middle_interval =
            TestInterval::new(OVERLAPPING_RIGHT_ADDRESS.clone(), INSERTED_PROPERTIES);
        tree.add_interval(&middle_interval).unwrap();
        expected_intervals.clear();
        expected_intervals.push(TestInterval::new(
            INSERTED_ADDRESS_START..right_interval().end(),
            INSERTED_PROPERTIES,
        ));
        verify(file!(), line!(), &tree, &expected_intervals);
    }

    #[test]
    fn test_add_overlapping_in_middle_low_priority() {
        let (mut tree, mut expected_intervals) = setup_interval_tree();
        tree.add_interval(&right_interval()).unwrap();
        expected_intervals.push(right_interval().clone());
        verify(file!(), line!(), &tree, &expected_intervals);

        tree.add_interval(&TestInterval::new(
            OVERLAPPING_RIGHT_ADDRESS.clone(),
            LOW_PRIORITY_PROPERTIES,
        ))
        .unwrap();
        expected_intervals.push(TestInterval::new(
            INSERTED_ADDRESS_END..INSERTED_ADDRESS_END + 5,
            LOW_PRIORITY_PROPERTIES,
        ));
        verify(file!(), line!(), &tree, &expected_intervals);
    }

    #[test]
    fn test_add_overlapping_in_middle_high_priority() {
        let (mut tree, mut expected_intervals) = setup_interval_tree();
        tree.add_interval(&right_interval()).unwrap();
        expected_intervals.push(right_interval().clone());
        verify(file!(), line!(), &tree, &expected_intervals);

        let middle_interval =
            TestInterval::new(OVERLAPPING_RIGHT_ADDRESS.clone(), HIGH_PRIORITY_PROPERTIES);
        tree.add_interval(&middle_interval).unwrap();
        expected_intervals[0].set_end(OVERLAPPING_RIGHT_ADDRESS.start);
        expected_intervals[1].set_start(OVERLAPPING_RIGHT_ADDRESS.end);
        expected_intervals.push(middle_interval);
        verify(file!(), line!(), &tree, &expected_intervals);
    }

    #[test]
    fn test_add_overlapping_multiple() {
        let (mut tree, mut expected_intervals) = setup_interval_tree();
        tree.add_interval(&right_interval()).unwrap();
        expected_intervals.push(right_interval().clone());
        let extreme_right_interval = TestInterval::new(
            right_interval().end() + 10..right_interval().end() + 20,
            INSERTED_PROPERTIES,
        );
        tree.add_interval(&extreme_right_interval).unwrap();
        expected_intervals.push(extreme_right_interval.clone());
        verify(file!(), line!(), &tree, &expected_intervals);

        let overlapping_interval = TestInterval::new(
            INSERTED_ADDRESS_START - 5..extreme_right_interval.end() + 10,
            INSERTED_PROPERTIES,
        );
        tree.add_interval(&overlapping_interval).unwrap();
        expected_intervals.clear();
        expected_intervals.push(overlapping_interval);
        verify(file!(), line!(), &tree, &expected_intervals);
    }

    #[test]
    fn test_add_overlapping_multiple_not_mergeable() {
        let (mut tree, mut expected_intervals) = setup_interval_tree();
        tree.add_interval(&right_interval()).unwrap();
        expected_intervals.push(right_interval().clone());
        let extreme_right_interval = TestInterval::new(
            right_interval().end() + 10..right_interval().end() + 20,
            INSERTED_PROPERTIES,
        );
        tree.add_interval(&extreme_right_interval).unwrap();
        expected_intervals.push(extreme_right_interval.clone());
        verify(file!(), line!(), &tree, &expected_intervals);

        let overlapping_interval = TestInterval::new(
            INSERTED_ADDRESS_START - 5..extreme_right_interval.end() + 10,
            LOW_PRIORITY_PROPERTIES,
        );
        tree.add_interval(&overlapping_interval).unwrap();
        // The overlapping_interval gets divided into multiple intervals.
        let mut split_interval = overlapping_interval.clone();
        split_interval.set_end(inserted_interval().start());
        expected_intervals.push(split_interval.clone());
        split_interval.set_start(inserted_interval().end());
        split_interval.set_end(right_interval().start());
        expected_intervals.push(split_interval.clone());
        split_interval.set_start(right_interval().end());
        split_interval.set_end(extreme_right_interval.start());
        expected_intervals.push(split_interval.clone());
        split_interval.set_start(extreme_right_interval.end());
        split_interval.set_end(overlapping_interval.end());
        expected_intervals.push(split_interval.clone());
        verify(file!(), line!(), &tree, &expected_intervals);
    }

    #[test]
    fn test_add_splits_existing_interval() {
        let (mut tree, mut expected_intervals) = setup_interval_tree();
        let small_interval = TestInterval::new(
            inserted_interval().start() + 3..inserted_interval().end() - 3,
            HIGH_PRIORITY_PROPERTIES,
        );
        tree.add_interval(&small_interval).unwrap();
        expected_intervals.push(expected_intervals[0].clone());
        expected_intervals[0].set_end(small_interval.start());
        expected_intervals[1].set_start(small_interval.end());
        expected_intervals.push(small_interval.clone());
        verify(file!(), line!(), &tree, &expected_intervals);
    }
    #[test]
    fn test_add_splits_existing_interval_at_start() {
        let (mut tree, mut expected_intervals) = setup_interval_tree();
        let small_interval = TestInterval::new(
            inserted_interval().start()..inserted_interval().end() - 3,
            HIGH_PRIORITY_PROPERTIES,
        );
        tree.add_interval(&small_interval).unwrap();
        expected_intervals[0].set_start(small_interval.end());
        expected_intervals.push(small_interval.clone());
        verify(file!(), line!(), &tree, &expected_intervals);
    }
    #[test]
    fn test_add_splits_existing_interval_at_end() {
        let (mut tree, mut expected_intervals) = setup_interval_tree();
        let small_interval = TestInterval::new(
            inserted_interval().start() + 3..inserted_interval().end(),
            HIGH_PRIORITY_PROPERTIES,
        );
        tree.add_interval(&small_interval).unwrap();
        expected_intervals[0].set_end(small_interval.start());
        expected_intervals.push(small_interval.clone());
        verify(file!(), line!(), &tree, &expected_intervals);
    }

    #[test]
    fn test_remove_all() {
        let (mut tree, expected_intervals) = setup_interval_tree();
        for interval in &expected_intervals {
            assert_eq!(tree.remove_interval(interval.as_ref()), Ok(vec![interval.clone()]));
        }
        assert_eq!(0, tree.interval_count());
    }

    #[test]
    fn test_remove_at_start() {
        let (mut tree, _) = setup_interval_tree();

        let ret = tree
            .remove_interval(&(inserted_range().start - 4..inserted_range().start + 4))
            .unwrap();

        assert_eq!(ret.len(), 1);
        assert_eq!(ret[0].range, inserted_range().start..inserted_range().start + 4);
        assert_eq!(1, tree.interval_count());
        for (_, interval) in tree.get_iter() {
            assert_eq!(interval.range, inserted_range().start + 4..inserted_range().end);
        }
    }

    #[test]
    fn test_remove_at_end() {
        let (mut tree, _) = setup_interval_tree();

        let ret =
            tree.remove_interval(&(inserted_range().end - 4..inserted_range().end + 4)).unwrap();

        assert_eq!(ret.len(), 1);
        assert_eq!(ret[0].range, inserted_range().end - 4..inserted_range().end);
        assert_eq!(1, tree.interval_count());
        for (_, interval) in tree.get_iter() {
            assert_eq!(interval.range, inserted_range().start..inserted_range().end - 4);
        }
    }

    #[test]
    fn test_remove_from_middle() {
        let (mut tree, _) = setup_interval_tree();

        let ret =
            tree.remove_interval(&(inserted_range().start + 4..inserted_range().end - 4)).unwrap();

        assert_eq!(ret.len(), 1);
        assert_eq!(ret[0].range, inserted_range().start + 4..inserted_range().end - 4);
        assert_eq!(2, tree.interval_count());
        for (_, interval) in tree.get_iter() {
            assert!(
                (interval.range == (inserted_range().start..inserted_range().start + 4))
                    || (interval.range == (inserted_range().end - 4..inserted_range().end))
            );
        }
    }

    #[test]
    fn test_remove_adjacent_unaffected() {
        let mut tree = IntervalTree::new();
        tree.add_interval(&TestInterval::new(0..1000, 1)).unwrap();
        tree.add_interval(&TestInterval::new(1000..2000, 1)).unwrap();
        tree.add_interval(&TestInterval::new(2000..3000, 1)).unwrap();

        let ret = tree.remove_interval(&(1000..2000)).unwrap();
        assert_eq!(ret, vec![TestInterval::new(1000..2000, 1)]);
        let remaining = tree.get_iter().values().cloned().collect::<Vec<_>>();
        assert_eq!(
            remaining,
            vec![TestInterval::new(0..1000, 1), TestInterval::new(2000..3000, 1),]
        );
    }

    #[test]
    fn test_remove_matching() {
        let mut tree = IntervalTree::new();
        tree.add_interval(&TestInterval::new(0..1, 0)).unwrap();
        tree.add_interval(&TestInterval::new(1..2, 1)).unwrap();
        tree.add_interval(&TestInterval::new(2..3, 0)).unwrap();
        tree.add_interval(&TestInterval::new(3..4, 1)).unwrap();

        let ret = tree.remove_matching_interval(&(0..2), |i| i.state == 0).unwrap();
        assert_eq!(ret, vec![TestInterval::new(0..1, 0)]);
        let remaining = tree.get_iter().values().cloned().collect::<Vec<_>>();
        assert_eq!(
            remaining,
            vec![
                TestInterval::new(1..2, 1),
                TestInterval::new(2..3, 0),
                TestInterval::new(3..4, 1),
            ]
        );
    }

    #[test]
    fn test_update() {
        let (mut tree, _) = setup_interval_tree();
        let mut new_interval = inserted_interval().clone();
        new_interval.state += 1;

        assert_eq!(tree.update_interval(&new_interval), Ok(vec![inserted_interval().clone()]));
        assert_eq!(1, tree.interval_count());

        for (_, interval) in tree.get_iter() {
            assert_eq!(interval.clone(), new_interval);
        }
    }

    #[test]
    fn test_get_intervals() {
        let (mut tree, _) = setup_interval_tree();
        let mut removed = inserted_interval().clone();
        removed.range = inserted_range().start + 4..inserted_range().end - 4;

        let mut new_interval = removed.clone();
        new_interval.state += 1;

        assert_eq!(tree.update_interval(&new_interval), Ok(vec![removed]));
        assert_eq!(3, tree.interval_count());

        let mut start = inserted_interval().clone();
        start.range.end = inserted_range().start + 4;
        let mut end = inserted_interval().clone();
        end.range.start = inserted_range().end - 4;
        assert_eq!(tree.get_intervals(&inserted_range()), Ok(vec![start, new_interval, end]));
    }
}

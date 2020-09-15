// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Utilities for dealing with the parsing of HCI packets.

use std::{collections::VecDeque, ops::Range};

/// Copy bytes from `src` to `dst` until either is exhausted.
/// Returns `Ok(())` if `dst` was exhausted (filled).
/// Returns `Err(())` if `src` is exhausted before `dst`
pub(super) fn copy_until_full<'a, T: 'a + Clone>(
    mut src: impl ExactSizeIterator<Item = T>,
    dst: impl ExactSizeIterator<Item = &'a mut T>,
) -> Result<(), ()> {
    if src.len() < dst.len() {
        return Err(());
    }
    for b in dst {
        if let Some(next) = src.next() {
            *b = next;
        } else {
            return Err(());
        }
    }
    Ok(())
}

/// Return contiguous slices from `q` that fall within the specified `range`.
/// If any region of `q` falls within `range`, the first slice is guaranteed to
/// be non-empty.
/// If `range` does not specify a populated region of `q`, both slices will be empty.
pub(super) fn slices_from_range<T>(q: &VecDeque<T>, range: Range<usize>) -> (&[T], &[T]) {
    // range does not contain any values in q.
    if range.start >= q.len() || ExactSizeIterator::len(&range) == 0 {
        return (&[], &[]);
    }

    let (a, b) = q.as_slices();

    if range.end <= a.len() {
        // Entire range is covered by a.
        (&a[range], &[])
    } else if range.start >= a.len() {
        // Entire range is covered by b.
        let b_start = range.start - a.len();
        let b_end = (range.end - a.len()).min(b.len());
        (&b[b_start..b_end], &[])
    } else {
        // Range spans a and b.
        let a_start = range.start;
        let b_end = (range.end - a.len()).min(b.len());
        (&a[a_start..], &b[..b_end])
    }
}

#[cfg(test)]
mod tests {
    use {super::*, std::iter::FromIterator};

    #[test]
    fn copy_less_than_full_produces_error() {
        let src = vec![];
        let mut dst = vec![0; 5];
        let dst_orig = dst.clone();
        let result = copy_until_full(src.iter().cloned(), dst.iter_mut());
        assert!(result.is_err());
        assert_eq!(dst, dst_orig);

        let src = vec![1, 2];
        let mut dst = vec![0; 5];
        let dst_orig = dst.clone();
        let result = copy_until_full(src.iter().cloned(), dst.iter_mut());
        assert!(result.is_err());
        assert_eq!(dst, dst_orig);
    }

    #[test]
    fn copy_full_produces_success() {
        // exactly full
        let src = vec![1, 2, 3, 4, 5];
        let mut dst = vec![0; 5];
        let result = copy_until_full(src.iter().cloned(), dst.iter_mut());
        assert!(result.is_ok());
        assert_eq!(src, dst);

        // more than full
        let src = vec![1, 2, 3, 4, 5, 6];
        let mut dst = vec![0; 5];
        let result = copy_until_full(src.iter().cloned(), dst.iter_mut());
        assert!(result.is_ok());
        assert_eq!(&src[0..dst.len()], &*dst);
    }

    const START: usize = 0;
    const START_B: usize = 4;
    const END: usize = 7; // END is set to the minimum capacity of a VecDeque.

    // Make a VecDeque with a specific as_slices() return value.
    fn make_q() -> VecDeque<usize> {
        let mut q = VecDeque::with_capacity(END);
        assert_eq!(q.capacity(), END,
            "Tests depend on VecDeque having a capacity of `CAP` to get the expected .as_slices() return values");

        // Offset start of VecDeque to force wrapping
        for _ in 0..START_B {
            q.push_back(99);
            q.pop_front();
        }
        // Populate VecDeque
        for i in START..END {
            q.push_back(i);
        }
        let expected_a = Vec::from_iter(0..START_B);
        let expected_b = Vec::from_iter(START_B..END);
        assert_eq!(
            q.as_slices(),
            (expected_a.as_slice(), expected_b.as_slice()),
            "Tests depend on specific .as_slices() return value"
        );
        q
    }

    #[test]
    fn range_falls_outside_slice() {
        let q = make_q();

        let empty = START..START;
        let (a, b) = slices_from_range(&q, empty);
        assert!(a.is_empty());
        assert!(b.is_empty());

        let out_of_bounds = END..(END + 1);
        let (a, b) = slices_from_range(&q, out_of_bounds);
        assert!(a.is_empty());
        assert!(b.is_empty());
    }

    #[test]
    fn range_falls_in_a_only() {
        let q = make_q();

        let full_a = START..START_B;
        let (a, b) = slices_from_range(&q, full_a.clone());
        let expected_a = Vec::from_iter(full_a);
        assert_eq!(a, &*expected_a);
        assert!(b.is_empty());

        let partial_a = (START + 1)..(START_B - 1);
        let (a, b) = slices_from_range(&q, partial_a.clone());
        let expected_a = Vec::from_iter(partial_a);
        assert_eq!(a, &*expected_a);
        assert!(b.is_empty());
    }

    #[test]
    fn range_falls_in_b_only() {
        let q = make_q();

        let full_b = START_B..END;
        let (a, b) = slices_from_range(&q, full_b.clone());
        // Note slices_from_range will always ensure that the returned `a` has values
        // if any values are covered by the range.
        let expected_a = Vec::from_iter(full_b);
        assert_eq!(a, &*expected_a);
        assert!(b.is_empty());

        let partial_b = (START_B + 1)..(END - 1);
        let (a, b) = slices_from_range(&q, partial_b.clone());
        let expected_a = Vec::from_iter(partial_b);
        assert_eq!(a, &*expected_a);
        assert!(b.is_empty());

        // partial_b extends past the end of the vecdeque. That is OK.
        let partial_b = (START_B + 1)..(END + 1);
        let (a, b) = slices_from_range(&q, partial_b.clone());
        let expected_a = Vec::from_iter((START_B + 1)..END);
        assert_eq!(a, &*expected_a);
        assert!(b.is_empty());
    }

    #[test]
    fn range_falls_in_both_a_and_b() {
        let q = make_q();

        let full_q = START..END;
        let (a, b) = slices_from_range(&q, full_q.clone());
        let expected_a = Vec::from_iter(START..START_B);
        let expected_b = Vec::from_iter(START_B..END);
        assert_eq!(a, &*expected_a);
        assert_eq!(b, &*expected_b);

        let partial_q = (START + 1)..(END - 1);
        let (a, b) = slices_from_range(&q, partial_q.clone());
        let expected_a = Vec::from_iter((START + 1)..START_B);
        let expected_b = Vec::from_iter(START_B..(END - 1));
        assert_eq!(a, &*expected_a);
        assert_eq!(b, &*expected_b);

        // partial_q extends past the end of the vecdeque. That is OK.
        let partial_q = (START + 1)..(END + 2);
        let (a, b) = slices_from_range(&q, partial_q.clone());
        let expected_a = Vec::from_iter((START + 1)..START_B);
        let expected_b = Vec::from_iter(START_B..END);
        assert_eq!(a, &*expected_a);
        assert_eq!(b, &*expected_b);
    }
}

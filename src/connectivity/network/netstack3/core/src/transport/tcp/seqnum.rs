// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! TCP sequence numbers and operations on them.

use core::{convert::TryFrom as _, num::TryFromIntError, ops};

use explicit::ResultExt as _;

/// Sequence number of a transferred TCP segment.
///
/// Per https://tools.ietf.org/html/rfc793#section-3.3:
///   This space ranges from 0 to 2**32 - 1. Since the space is finite, all
///   arithmetic dealing with sequence numbers must be performed modulo 2**32.
///   This unsigned arithmetic preserves the relationship of sequence numbers
///   as they cycle from 2**32 - 1 to 0 again.  There are some subtleties to
///   computer modulo arithmetic, so great care should be taken in programming
///   the comparison of such values.
///
/// For any sequence number, there are 2**31 numbers after it and 2**31 - 1
/// numbers before it.
#[derive(Debug, PartialEq, Eq, Clone, Copy)]
pub(crate) struct SeqNum(u32);

impl ops::Add<i32> for SeqNum {
    type Output = SeqNum;

    fn add(self, rhs: i32) -> Self::Output {
        let Self(lhs) = self;
        Self(lhs.wrapping_add_signed(rhs))
    }
}

impl ops::Sub<i32> for SeqNum {
    type Output = SeqNum;

    fn sub(self, rhs: i32) -> Self::Output {
        let Self(lhs) = self;
        Self(lhs.wrapping_add_signed(rhs.wrapping_neg()))
    }
}

impl ops::Add<u32> for SeqNum {
    type Output = SeqNum;

    fn add(self, rhs: u32) -> Self::Output {
        let Self(lhs) = self;
        Self(lhs.wrapping_add(rhs))
    }
}

impl ops::Add<usize> for SeqNum {
    type Output = SeqNum;

    fn add(self, rhs: usize) -> Self::Output {
        // The following `as` coercion is sound because:
        // 1. if `u32` is wider than `usize`, the unsigned extension will
        //    result in the same number.
        // 2. if `usize` is wider than `u32`, then `rhs` can be written as
        //    `A * 2 ^ 32 + B`. Because of the wrapping nature of sequnce
        //    numbers, the effect of adding `rhs` is the same as adding `B`
        //    which is the number after the truncation, i.e., `rhs as u32`.
        self + (rhs as u32)
    }
}

impl ops::Sub for SeqNum {
    // `i32` is more intuitive than `u32`, since subtraction may yield negative
    // values.
    type Output = i32;

    fn sub(self, rhs: Self) -> Self::Output {
        let Self(lhs) = self;
        let Self(rhs) = rhs;
        // The following `as` coercion is sound because:
        // Rust uses 2's complement for signed integers [1], meaning when cast
        // to an `i32, an `u32` >= 1<<32 becomes negative and an `u32` < 1<<32
        // becomes positive. `wrapping_sub` ensures that if `rhs` is a `SeqNum`
        // after `lhs`, the result will wrap into the `u32` space > 1<<32.
        // Recall that `SeqNums` are only valid for a `WindowSize` < 1<<31; this
        // prevents the difference of `wrapping_sub` from being so large that it
        // wraps into the `u32` space < 1<<32.
        // [1]: https://doc.rust-lang.org/reference/types/numeric.html
        lhs.wrapping_sub(rhs) as i32
    }
}

impl From<u32> for SeqNum {
    fn from(x: u32) -> Self {
        Self::new(x)
    }
}

impl Into<u32> for SeqNum {
    fn into(self) -> u32 {
        let Self(x) = self;
        x
    }
}

impl SeqNum {
    pub(crate) const fn new(x: u32) -> Self {
        Self(x)
    }
}

impl SeqNum {
    /// A predicate for whether a sequence number is before the other.
    ///
    /// Please refer to [`SeqNum`] for the defined order.
    pub(crate) fn before(self, other: SeqNum) -> bool {
        self - other < 0
    }

    /// A predicate for whether a sequence number is after the other.
    ///
    /// Please refer to [`SeqNum`] for the defined order.
    pub(crate) fn after(self, other: SeqNum) -> bool {
        self - other > 0
    }
}

/// A witness type for TCP window size.
///
/// Per [RFC 7323 Section 2.3]:
/// > ..., the above constraints imply that two times the maximum window size
/// > must be less than 2^31, or
/// >                    max window < 2^30
///
/// [RFC 7323 Section 2.3]: https://tools.ietf.org/html/rfc7323#section-2.3
#[derive(Debug, PartialEq, Eq, Clone, Copy, PartialOrd, Ord)]
pub(crate) struct WindowSize(u32);

impl WindowSize {
    pub(super) const MAX: WindowSize = WindowSize(1 << 30 - 1);
    pub(super) const ZERO: WindowSize = WindowSize(0);

    // TODO(https://github.com/rust-lang/rust/issues/67441): put this constant
    // in the state module once `Option::unwrap` is stable.
    pub(super) const DEFAULT: WindowSize = WindowSize(65535);

    pub const fn from_u32(wnd: u32) -> Option<Self> {
        let WindowSize(max) = Self::MAX;
        if wnd > max {
            None
        } else {
            Some(Self(wnd))
        }
    }

    pub(crate) fn from_u16(wnd: u16) -> Self {
        Self(wnd.into())
    }

    pub(super) fn saturating_add(self, rhs: u32) -> Self {
        Self::from_u32(u32::from(self).saturating_add(rhs)).unwrap_or(Self::MAX)
    }

    pub(super) fn new(wnd: usize) -> Option<Self> {
        u32::try_from(wnd).ok_checked::<TryFromIntError>().and_then(WindowSize::from_u32)
    }

    pub(super) fn checked_sub(self, diff: usize) -> Option<Self> {
        usize::from(self).checked_sub(diff).and_then(Self::new)
    }
}

impl ops::Add<WindowSize> for SeqNum {
    type Output = SeqNum;

    fn add(self, WindowSize(wnd): WindowSize) -> Self::Output {
        self + wnd
    }
}

impl From<WindowSize> for u32 {
    fn from(WindowSize(wnd): WindowSize) -> Self {
        wnd
    }
}

#[cfg(any(target_pointer_width = "32", target_pointer_width = "64"))]
impl From<WindowSize> for usize {
    fn from(WindowSize(wnd): WindowSize) -> Self {
        wnd as usize
    }
}

#[cfg(test)]
mod tests {
    use proptest::{
        arbitrary::any,
        proptest,
        strategy::{Just, Strategy},
        test_runner::Config,
    };
    use proptest_support::failed_seeds;

    use super::{SeqNum, WindowSize};
    use crate::transport::tcp::segment::MAX_PAYLOAD_AND_CONTROL_LEN;

    fn arb_seqnum() -> impl Strategy<Value = SeqNum> {
        any::<u32>().prop_map(SeqNum::from)
    }

    // Generates a triple (a, b, c) s.t. a < b < a + 2^30 && b < c < a + 2^30.
    // This triple is used to verify that transitivity holds.
    fn arb_seqnum_trans_tripple() -> impl Strategy<Value = (SeqNum, SeqNum, SeqNum)> {
        arb_seqnum().prop_flat_map(|a| {
            (1..=MAX_PAYLOAD_AND_CONTROL_LEN).prop_flat_map(move |diff_a_b| {
                let b = a + diff_a_b;
                (1..=MAX_PAYLOAD_AND_CONTROL_LEN - diff_a_b).prop_flat_map(move |diff_b_c| {
                    let c = b + diff_b_c;
                    (Just(a), Just(b), Just(c))
                })
            })
        })
    }

    proptest! {
        #![proptest_config(Config {
            // Add all failed seeds here.
            failure_persistence: failed_seeds!(),
            ..Config::default()
        })]

        #[test]
        fn seqnum_ord_is_reflexive(a in arb_seqnum()) {
            assert_eq!(a, a)
        }

        #[test]
        fn seqnum_ord_is_total(a in arb_seqnum(), b in arb_seqnum()) {
            if a == b {
                assert!(!a.before(b) && !b.before(a))
            } else {
                assert!(a.before(b) ^ b.before(a))
            }
        }

        #[test]
        fn seqnum_ord_is_transitive((a, b, c) in arb_seqnum_trans_tripple()) {
            assert!(a.before(b) && b.before(c) && a.before(c));
        }

        #[test]
        fn seqnum_add_positive_greater(a in arb_seqnum(), b in 1..=i32::MAX) {
            assert!(a.before(a + b))
        }

        #[test]
        fn seqnum_add_negative_smaller(a in arb_seqnum(), b in i32::MIN..=-1) {
            assert!(a.after(a + b))
        }

        #[test]
        fn seqnum_sub_positive_smaller(a in arb_seqnum(), b in 1..=i32::MAX) {
            assert!(a.after(a - b))
        }

        #[test]
        fn seqnum_sub_negative_greater(a in arb_seqnum(), b in i32::MIN..=-1) {
            assert!(a.before(a - b))
        }

        #[test]
        fn seqnum_zero_identity(a in arb_seqnum()) {
            assert_eq!(a, a + 0)
        }

        #[test]
        fn seqnum_before_after_inverse(a in arb_seqnum(), b in arb_seqnum()) {
            assert_eq!(a.after(b), b.before(a))
        }

        #[test]
        fn seqnum_wraps_around_at_max_length(a in arb_seqnum()) {
            assert!(a.before(a + MAX_PAYLOAD_AND_CONTROL_LEN));
            assert!(a.after(a + MAX_PAYLOAD_AND_CONTROL_LEN + 1));
        }

        #[test]
        fn window_size_less_than_or_eq_to_max(wnd in 0..=WindowSize::MAX.0) {
            assert_eq!(WindowSize::from_u32(wnd), Some(WindowSize(wnd)));
        }

        #[test]
        fn window_size_greater_than_max(wnd in WindowSize::MAX.0+1..=u32::MAX) {
            assert_eq!(WindowSize::from_u32(wnd), None);
        }
    }
}

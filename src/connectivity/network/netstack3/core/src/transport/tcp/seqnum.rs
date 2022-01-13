// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! TCP sequence numbers and operations on them.

use core::{fmt, ops};

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
/// For any sequence number, there are 2**32 numbers after it and 2**32 - 1
/// numbers before it.
// TODO(https://github.com/rust-lang/rust/issues/87840): i32 is used here
// instead of the more natural u32 to minimize the usage of `as` casts. Because
// a signed integer should be used to represent the difference between sequence
// numbers, without `mixed_integer_ops`, using u32 will require `as` casts when
// implementing `Sub` or `Add` for `SeqNum`.
#[derive(PartialEq, Eq, Clone, Copy)]
pub(crate) struct SeqNum(i32);

impl fmt::Debug for SeqNum {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let Self(seq) = self;
        f.debug_tuple("SeqNum").field(&(*seq as u32)).finish()
    }
}

impl ops::Add<i32> for SeqNum {
    type Output = SeqNum;

    fn add(self, rhs: i32) -> Self::Output {
        let Self(lhs) = self;
        Self(lhs.wrapping_add(rhs))
    }
}

impl ops::Sub<i32> for SeqNum {
    type Output = SeqNum;

    fn sub(self, rhs: i32) -> Self::Output {
        let Self(lhs) = self;
        Self(lhs.wrapping_sub(rhs))
    }
}

impl ops::Sub for SeqNum {
    type Output = i32;

    fn sub(self, rhs: Self) -> Self::Output {
        let Self(lhs) = self;
        let Self(rhs) = rhs;
        lhs.wrapping_sub(rhs)
    }
}

impl From<u32> for SeqNum {
    fn from(x: u32) -> Self {
        Self::new(x)
    }
}

impl SeqNum {
    pub(crate) const fn new(x: u32) -> Self {
        Self(x as i32)
    }
}

// TODO(https://fxbug.dev/88814): The code below will trigger dead code lint
// because there is no user currently. Disallow when it is actually used.
#[cfg_attr(not(test), allow(dead_code))]
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
        other.before(self)
    }
}

#[cfg(test)]
mod tests {
    use super::SeqNum;
    use proptest::{
        arbitrary::any,
        proptest,
        strategy::{Just, Strategy},
        test_runner::Config,
    };
    use proptest_support::failed_seeds;

    // Per https://tools.ietf.org/html/rfc7323#section-2.3: max window < 2^30
    const MAX_TCP_WINDOW: i32 = i32::pow(2, 30) - 1;

    fn arb_seqnum() -> impl Strategy<Value = SeqNum> {
        any::<u32>().prop_map(SeqNum::from)
    }

    // Generates a triple (a, b, c) s.t. a < b < a + 2^30 && b < c < a + 2^30.
    // This triple is used to verify that transitivity holds.
    fn arb_seqnum_trans_tripple() -> impl Strategy<Value = (SeqNum, SeqNum, SeqNum)> {
        arb_seqnum().prop_flat_map(|a| {
            (1..=MAX_TCP_WINDOW).prop_flat_map(move |diff_a_b| {
                let b = a + diff_a_b;
                (1..=MAX_TCP_WINDOW - diff_a_b).prop_flat_map(move |diff_b_c| {
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
        fn seqnum_add_positive_greater(a in arb_seqnum(), b in 1..MAX_TCP_WINDOW) {
            assert!(a.before(a + b))
        }

        #[test]
        fn seqnum_add_negative_smaller(a in arb_seqnum(), b in -MAX_TCP_WINDOW..-1) {
            assert!(a.after(a + b))
        }

        #[test]
        fn seqnum_sub_positive_smaller(a in arb_seqnum(), b in 1..MAX_TCP_WINDOW) {
            assert!(a.after(a - b))
        }

        #[test]
        fn seqnum_sub_negative_greater(a in arb_seqnum(), b in -MAX_TCP_WINDOW..-1) {
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
    }
}

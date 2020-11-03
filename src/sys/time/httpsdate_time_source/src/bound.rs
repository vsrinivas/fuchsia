// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon as zx;
use std::cmp;

/// An estimate of the worst possible time drift in the device's oscillator.
const MAX_DRIFT_PPM: i64 = 200;
const ONE_MILLION: i64 = 1_000_000;

/// A representation of the range of possible UTC times at an instant of monotonic time.
#[derive(Debug, PartialEq)]
pub struct Bound {
    /// Monotonic time for which the bound is valid.
    pub monotonic: zx::Time,
    /// The minimum possible UTC time.
    pub utc_min: zx::Time,
    /// The maximum possible UTC time.
    pub utc_max: zx::Time,
}

impl Bound {
    /// Take the intersection of two bounds to produce a (possibly) tighter bound. Returns None if
    /// the bounds do not intersect.
    pub fn combine(&self, other: &Bound) -> Option<Bound> {
        let (earlier, later) =
            if self.monotonic < other.monotonic { (self, other) } else { (other, self) };

        let projected = earlier.project(later.monotonic);
        let combined = Bound {
            monotonic: later.monotonic,
            utc_min: cmp::max(later.utc_min, projected.utc_min),
            utc_max: cmp::min(later.utc_max, projected.utc_max),
        };
        if combined.utc_min > combined.utc_max {
            None
        } else {
            Some(combined)
        }
    }

    /// Project a bound to a different monotonic time. The bound is expanded by the time
    /// elapsed between the bound and the provided monotonic time multiplied by the drift.
    fn project(&self, later_monotonic: zx::Time) -> Bound {
        let time_delta = later_monotonic - self.monotonic;
        let max_drift = (time_delta * MAX_DRIFT_PPM) / ONE_MILLION;
        Bound {
            monotonic: later_monotonic,
            utc_min: self.utc_min + time_delta - max_drift,
            utc_max: self.utc_max + time_delta + max_drift,
        }
    }

    /// Returns the size of the possible range.
    pub fn size(&self) -> zx::Duration {
        self.utc_max - self.utc_min
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use lazy_static::lazy_static;

    const DURATION_MICROS_500: zx::Duration = zx::Duration::from_micros(500);
    lazy_static! {
        static ref DRIFT_IN_500_MICROS: zx::Duration =
            DURATION_MICROS_500 * MAX_DRIFT_PPM / ONE_MILLION;
        static ref MONOTONIC_TIME: zx::Time = zx::Time::from_nanos(1_000_000_000_000);
    }

    fn assert_combine_commutative(bound_1: &Bound, bound_2: &Bound) {
        assert_eq!(bound_1.combine(bound_2), bound_2.combine(bound_1))
    }

    #[test]
    fn combine_bounds_with_same_monotonic_times() {
        let earlier_utc_bound = Bound {
            monotonic: *MONOTONIC_TIME,
            utc_min: zx::Time::from_nanos(1_000_000),
            utc_max: zx::Time::from_nanos(2_000_000),
        };
        let later_utc_bound = Bound {
            monotonic: *MONOTONIC_TIME,
            utc_min: zx::Time::from_nanos(1_500_000),
            utc_max: zx::Time::from_nanos(2_500_000),
        };
        assert_eq!(
            earlier_utc_bound.combine(&later_utc_bound).unwrap(),
            Bound {
                monotonic: *MONOTONIC_TIME,
                utc_min: later_utc_bound.utc_min,
                utc_max: earlier_utc_bound.utc_max,
            }
        );
        assert_combine_commutative(&earlier_utc_bound, &later_utc_bound);

        let enclosing_utc_bound = Bound {
            monotonic: *MONOTONIC_TIME,
            utc_min: zx::Time::from_nanos(1_000_000),
            utc_max: zx::Time::from_nanos(2_000_000),
        };
        let enclosed_utc_bound = Bound {
            monotonic: *MONOTONIC_TIME,
            utc_min: zx::Time::from_nanos(1_200_000),
            utc_max: zx::Time::from_nanos(1_800_000),
        };
        assert_eq!(enclosed_utc_bound.combine(&enclosing_utc_bound).unwrap(), enclosed_utc_bound);
        assert_combine_commutative(&enclosed_utc_bound, &enclosing_utc_bound);
    }

    #[test]
    fn combine_bounds_with_different_monotonic_times() {
        let earlier = Bound {
            monotonic: *MONOTONIC_TIME,
            utc_min: zx::Time::from_nanos(1_000_000),
            utc_max: zx::Time::from_nanos(2_000_000),
        };
        let later = Bound {
            monotonic: *MONOTONIC_TIME + DURATION_MICROS_500,
            utc_min: zx::Time::from_nanos(2_000_000),
            utc_max: zx::Time::from_nanos(3_000_000),
        };
        assert_eq!(
            earlier.combine(&later).unwrap(),
            Bound {
                monotonic: later.monotonic,
                utc_min: later.utc_min,
                utc_max: earlier.utc_max + DURATION_MICROS_500 + *DRIFT_IN_500_MICROS
            }
        );
        assert_combine_commutative(&earlier, &later);

        let earlier_enclosing = Bound {
            monotonic: *MONOTONIC_TIME,
            utc_min: zx::Time::from_nanos(1_000_000),
            utc_max: zx::Time::from_nanos(2_000_000),
        };
        let later_enclosed = Bound {
            monotonic: *MONOTONIC_TIME + DURATION_MICROS_500,
            utc_min: zx::Time::from_nanos(1_700_000),
            utc_max: zx::Time::from_nanos(2_300_000),
        };
        assert_eq!(earlier_enclosing.combine(&later_enclosed).unwrap(), later_enclosed);
        assert_combine_commutative(&earlier_enclosing, &later_enclosed);

        let earlier_enclosed = Bound {
            monotonic: *MONOTONIC_TIME,
            utc_min: zx::Time::from_nanos(1_200_000),
            utc_max: zx::Time::from_nanos(1_800_000),
        };
        let later_enclosing = Bound {
            monotonic: *MONOTONIC_TIME + DURATION_MICROS_500,
            utc_min: zx::Time::from_nanos(1_500_000),
            utc_max: zx::Time::from_nanos(2_500_000),
        };
        assert_eq!(
            earlier_enclosed.combine(&later_enclosing).unwrap(),
            Bound {
                monotonic: later_enclosing.monotonic,
                utc_min: earlier_enclosed.utc_min + DURATION_MICROS_500 - *DRIFT_IN_500_MICROS,
                utc_max: earlier_enclosed.utc_max + DURATION_MICROS_500 + *DRIFT_IN_500_MICROS,
            }
        );
        assert_combine_commutative(&earlier_enclosed, &later_enclosing);
    }

    #[test]
    fn combine_bounds_no_overlap() {
        let earlier_utc = Bound {
            monotonic: *MONOTONIC_TIME,
            utc_min: zx::Time::from_nanos(1_000_000),
            utc_max: zx::Time::from_nanos(2_000_000),
        };
        let later_utc = Bound {
            monotonic: *MONOTONIC_TIME,
            utc_min: zx::Time::from_nanos(2_500_000),
            utc_max: zx::Time::from_nanos(3_500_000),
        };
        assert!(earlier_utc.combine(&later_utc).is_none());
        assert_combine_commutative(&earlier_utc, &later_utc);

        let earlier = Bound {
            monotonic: *MONOTONIC_TIME,
            utc_min: zx::Time::from_nanos(1_000_000),
            utc_max: zx::Time::from_nanos(2_000_000),
        };
        let later = Bound {
            monotonic: *MONOTONIC_TIME + DURATION_MICROS_500,
            utc_min: zx::Time::from_nanos(2_600_000),
            utc_max: zx::Time::from_nanos(3_600_000),
        };
        assert!(earlier.combine(&later).is_none());
        assert_combine_commutative(&earlier, &later);
    }
}

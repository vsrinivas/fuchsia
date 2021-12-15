// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon as zx;

/// One million for PPM calculations
const MILLION: u64 = 1_000_000;

/// A transformation from monotonic time to synthetic time, including an error bound on this
/// synthetic time.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Transform {
    /// An offset on the monotonic timeline in nanoseconds.
    pub monotonic_offset: i64,
    /// An offset on the synthetic timeline in nanoseconds.
    pub synthetic_offset: i64,
    /// An adjustment to the standard 1 monotonic tick:1 synthetic tick rate in parts per million.
    /// Positive values indicate the synthetic clock is moving faster than the monotonic clock.
    pub rate_adjust_ppm: i32,
    /// The error bound on synthetic clock at monotonic = monotonic_offset.
    pub error_bound_at_offset: u64,
    /// The growth in error bound per monotonic tick in parts per million.
    pub error_bound_growth_ppm: u32,
}

impl Transform {
    /// Returns the synthetic time at the supplied monotonic time.
    pub fn synthetic(&self, monotonic: zx::Time) -> zx::Time {
        // Cast to i128 to avoid overflows in multiplication.
        let monotonic_difference = (monotonic.into_nanos() - self.monotonic_offset) as i128;
        let synthetic_offset = self.synthetic_offset as i128;
        let synthetic_ticks = self.rate_adjust_ppm as i128 + MILLION as i128;
        let reference_ticks = MILLION as i128;

        let time_nanos =
            (monotonic_difference * synthetic_ticks / reference_ticks) + synthetic_offset;
        zx::Time::from_nanos(time_nanos as i64)
    }

    /// Returns the error bound at the supplied monotonic time.
    pub fn error_bound(&self, monotonic: zx::Time) -> u64 {
        // Cast to i128 to avoid overflows in multiplication.
        let monotonic_difference = (monotonic.into_nanos() - self.monotonic_offset) as i128;
        if monotonic_difference <= 0 {
            // Assume the error bound was fixed at the supplied value before the reference time.
            self.error_bound_at_offset
        } else {
            // Error bound increases linearly after the reference time.
            let error_increase =
                (monotonic_difference * self.error_bound_growth_ppm as i128) / MILLION as i128;
            self.error_bound_at_offset + error_increase as u64
        }
    }

    /// Returns the synthetic time on this `Transform` minus the synthetic time on `other`,
    /// calculated at the supplied monotonic time.
    pub fn difference(&self, other: &Transform, monotonic: zx::Time) -> zx::Duration {
        self.synthetic(monotonic) - other.synthetic(monotonic)
    }

    /// Returns a `ClockUpdate` that will set a `Clock` onto this `Transform` using data
    /// from the supplied monotonic time.
    pub fn jump_to(&self, monotonic: zx::Time) -> zx::ClockUpdate {
        zx::ClockUpdate::builder()
            .absolute_value(monotonic, self.synthetic(monotonic))
            .rate_adjust(self.rate_adjust_ppm)
            .error_bounds(self.error_bound(monotonic))
            .build()
    }
}

impl From<&zx::Clock> for Transform {
    fn from(clock: &zx::Clock) -> Self {
        // Clock read failures should only be caused by an invalid clock object.
        let details = clock.get_details().expect("failed to get clock details");
        // Cast to i64 to avoid overflows in multiplication.
        let reference_ticks = details.mono_to_synthetic.rate.reference_ticks as i64;
        let synthetic_ticks = details.mono_to_synthetic.rate.synthetic_ticks as i64;
        let rate_adjust_ppm =
            ((synthetic_ticks * MILLION as i64) / reference_ticks) - MILLION as i64;

        Transform {
            monotonic_offset: details.mono_to_synthetic.reference_offset,
            synthetic_offset: details.mono_to_synthetic.synthetic_offset,
            rate_adjust_ppm: rate_adjust_ppm as i32,
            // Zircon clocks don't document the change in error over time. Assume a fixed error.
            error_bound_at_offset: details.error_bounds,
            error_bound_growth_ppm: 0,
        }
    }
}

/// Returns the time on the clock at a given monotonic reference time. This calculates the time
/// based on the clock transform definition, which only contains the most recent segment. This
/// is only useful for calculating the time for monotonic times close to the current time.
pub fn time_at_monotonic(clock: &zx::Clock, monotonic: zx::Time) -> zx::Time {
    let monotonic_reference = monotonic.into_nanos() as i128;
    // Clock read failures should only be caused by an invalid clock object.
    let details = clock.get_details().expect("failed to get clock details");
    // Calculate using the transform definition underlying a zircon clock.
    // Cast to i128 to avoid overflows in multiplication.
    let reference_offset = details.mono_to_synthetic.reference_offset as i128;
    let synthetic_offset = details.mono_to_synthetic.synthetic_offset as i128;
    let reference_ticks = details.mono_to_synthetic.rate.reference_ticks as i128;
    let synthetic_ticks = details.mono_to_synthetic.rate.synthetic_ticks as i128;

    let time_nanos = ((monotonic_reference - reference_offset) * synthetic_ticks / reference_ticks)
        + synthetic_offset;
    zx::Time::from_nanos(time_nanos as i64)
}

#[cfg(test)]
mod test {
    use {
        super::*,
        test_util::{assert_geq, assert_leq},
        zx::DurationNum,
    };

    const BACKSTOP: zx::Time = zx::Time::from_nanos(1234567890);
    const TIME_DIFF: zx::Duration = zx::Duration::from_seconds(5);
    const SLEW_RATE_PPM: i32 = 750;
    const ONE_MILLION: i32 = 1_000_000;

    const TEST_REFERENCE: zx::Time = zx::Time::from_nanos(70_000_000_000);
    const TEST_OFFSET: zx::Duration = zx::Duration::from_nanos(5_000_000_000);
    const TEST_ERROR_BOUND: u64 = 1234_000;
    const TEST_ERROR_BOUND_GROWTH: u32 = 100;

    const TOLERANCE: zx::Duration = zx::Duration::from_nanos(500_000_000);

    #[fuchsia::test]
    fn transform_properties_zero_rate_adjust() {
        let transform = Transform {
            monotonic_offset: TEST_REFERENCE.into_nanos(),
            synthetic_offset: (TEST_REFERENCE + TEST_OFFSET).into_nanos(),
            rate_adjust_ppm: 0,
            error_bound_at_offset: TEST_ERROR_BOUND,
            error_bound_growth_ppm: TEST_ERROR_BOUND_GROWTH,
        };

        assert_eq!(transform.synthetic(TEST_REFERENCE), TEST_REFERENCE + TEST_OFFSET);
        assert_eq!(
            transform.synthetic(TEST_REFERENCE + 200.millis()),
            TEST_REFERENCE + TEST_OFFSET + 200.millis()
        );
        assert_eq!(
            transform.synthetic(TEST_REFERENCE - 100.millis()),
            TEST_REFERENCE + TEST_OFFSET - 100.millis()
        );

        assert_eq!(transform.error_bound(TEST_REFERENCE), TEST_ERROR_BOUND);
        assert_eq!(
            transform.error_bound(TEST_REFERENCE + 1.milli()),
            TEST_ERROR_BOUND + TEST_ERROR_BOUND_GROWTH as u64
        );
        assert_eq!(transform.error_bound(TEST_REFERENCE - 1.milli()), TEST_ERROR_BOUND as u64);
    }

    #[fuchsia::test]
    fn transform_properties_positive_rate_adjust() {
        let transform = Transform {
            monotonic_offset: TEST_REFERENCE.into_nanos(),
            synthetic_offset: (TEST_REFERENCE + TEST_OFFSET).into_nanos(),
            rate_adjust_ppm: 25,
            error_bound_at_offset: TEST_ERROR_BOUND,
            error_bound_growth_ppm: 0,
        };

        assert_eq!(transform.synthetic(TEST_REFERENCE), TEST_REFERENCE + TEST_OFFSET);
        assert_eq!(
            transform.synthetic(TEST_REFERENCE + 200.millis()),
            TEST_REFERENCE + TEST_OFFSET + 200.millis() + (25 * 200).nanos()
        );
        assert_eq!(
            transform.synthetic(TEST_REFERENCE - 100.millis()),
            TEST_REFERENCE + TEST_OFFSET - 100.millis() - (25 * 100).nanos()
        );

        assert_eq!(transform.error_bound(TEST_REFERENCE), TEST_ERROR_BOUND);
        assert_eq!(transform.error_bound(TEST_REFERENCE + 1.milli()), TEST_ERROR_BOUND as u64);
        assert_eq!(transform.error_bound(TEST_REFERENCE - 1.milli()), TEST_ERROR_BOUND as u64);
    }

    #[fuchsia::test]
    fn transform_properties_negative_rate_adjust() {
        let transform = Transform {
            monotonic_offset: TEST_REFERENCE.into_nanos(),
            synthetic_offset: (TEST_REFERENCE + TEST_OFFSET).into_nanos(),
            rate_adjust_ppm: -50,
            error_bound_at_offset: TEST_ERROR_BOUND,
            error_bound_growth_ppm: TEST_ERROR_BOUND_GROWTH,
        };

        assert_eq!(transform.synthetic(TEST_REFERENCE), TEST_REFERENCE + TEST_OFFSET);
        assert_eq!(
            transform.synthetic(TEST_REFERENCE + 200.millis()),
            TEST_REFERENCE + TEST_OFFSET + 200.millis() - (50 * 200).nanos()
        );
        assert_eq!(
            transform.synthetic(TEST_REFERENCE - 100.millis()),
            TEST_REFERENCE + TEST_OFFSET - 100.millis() + (50 * 100).nanos()
        );

        assert_eq!(transform.error_bound(TEST_REFERENCE), TEST_ERROR_BOUND);
        assert_eq!(
            transform.error_bound(TEST_REFERENCE + 1.second()),
            TEST_ERROR_BOUND + (TEST_ERROR_BOUND_GROWTH * 1000) as u64
        );
        assert_eq!(transform.error_bound(TEST_REFERENCE - 1.second()), TEST_ERROR_BOUND as u64);
    }

    #[fuchsia::test]
    fn transform_difference() {
        let transform_1 = Transform {
            monotonic_offset: TEST_REFERENCE.into_nanos(),
            synthetic_offset: (TEST_REFERENCE + TEST_OFFSET).into_nanos(),
            rate_adjust_ppm: 25,
            error_bound_at_offset: TEST_ERROR_BOUND,
            error_bound_growth_ppm: TEST_ERROR_BOUND_GROWTH,
        };

        let transform_2 = Transform {
            monotonic_offset: TEST_REFERENCE.into_nanos(),
            synthetic_offset: TEST_REFERENCE.into_nanos(),
            rate_adjust_ppm: -50,
            error_bound_at_offset: TEST_ERROR_BOUND,
            error_bound_growth_ppm: 0,
        };

        assert_eq!(transform_1.difference(&transform_1, TEST_REFERENCE), 0.nanos());
        assert_eq!(transform_1.difference(&transform_2, TEST_REFERENCE), TEST_OFFSET);
        assert_eq!(
            transform_2.difference(&transform_1, TEST_REFERENCE),
            zx::Duration::from_nanos(-TEST_OFFSET.into_nanos())
        );
        assert_eq!(
            transform_1.difference(&transform_2, TEST_REFERENCE + 500.millis()),
            TEST_OFFSET + (75 * 500).nanos()
        );
        assert_eq!(
            transform_1.difference(&transform_2, TEST_REFERENCE - 300.millis()),
            TEST_OFFSET - (75 * 300).nanos()
        );
    }

    #[fuchsia::test]
    fn transform_conversion() {
        let transform = Transform {
            monotonic_offset: TEST_REFERENCE.into_nanos(),
            synthetic_offset: (TEST_REFERENCE + TEST_OFFSET).into_nanos(),
            rate_adjust_ppm: -15,
            error_bound_at_offset: TEST_ERROR_BOUND,
            error_bound_growth_ppm: 0,
        };

        let monotonic = zx::Time::get_monotonic();
        let clock_update = transform.jump_to(monotonic);
        assert_eq!(
            clock_update,
            zx::ClockUpdate::builder()
                .absolute_value(monotonic, transform.synthetic(monotonic))
                .rate_adjust(-15)
                .error_bounds(transform.error_bound(monotonic))
                .build()
        );

        let clock = zx::Clock::create(zx::ClockOpts::empty(), None).unwrap();
        clock.update(clock_update).unwrap();

        let double_converted = Transform::from(&clock);
        assert_eq!(double_converted.rate_adjust_ppm, transform.rate_adjust_ppm);
        assert_eq!(double_converted.error_bound_at_offset, transform.error_bound_at_offset);
        assert_eq!(double_converted.error_bound_growth_ppm, 0);
        assert_eq!(double_converted.rate_adjust_ppm, transform.rate_adjust_ppm);
        // Before RFC-0077 we accumulate some error in setting a clock, perform a coarse comparison.
        let synthetic_from_double_converted = double_converted.synthetic(TEST_REFERENCE);
        assert_geq!(synthetic_from_double_converted, TEST_REFERENCE + TEST_OFFSET - TOLERANCE);
        assert_leq!(synthetic_from_double_converted, TEST_REFERENCE + TEST_OFFSET + TOLERANCE);
    }

    #[fuchsia::test]
    fn time_at_monotonic_clock_not_started() {
        let clock = zx::Clock::create(zx::ClockOpts::empty(), Some(BACKSTOP)).unwrap();
        assert_eq!(time_at_monotonic(&clock, zx::Time::get_monotonic() + TIME_DIFF), BACKSTOP);
    }

    #[fuchsia::test]
    fn time_at_monotonic_clock_started() {
        let clock = zx::Clock::create(zx::ClockOpts::empty(), Some(BACKSTOP)).unwrap();

        let mono = zx::Time::get_monotonic();
        clock.update(zx::ClockUpdate::builder().absolute_value(mono, BACKSTOP)).unwrap();

        let clock_time = time_at_monotonic(&clock, mono + TIME_DIFF);
        assert_eq!(clock_time, BACKSTOP + TIME_DIFF);
    }

    #[fuchsia::test]
    fn time_at_monotonic_clock_slew_fast() {
        let clock = zx::Clock::create(zx::ClockOpts::empty(), Some(BACKSTOP)).unwrap();

        let mono = zx::Time::get_monotonic();
        clock
            .update(
                zx::ClockUpdate::builder()
                    .absolute_value(mono, BACKSTOP)
                    .rate_adjust(SLEW_RATE_PPM),
            )
            .unwrap();

        let clock_time = time_at_monotonic(&clock, mono + TIME_DIFF);
        assert_eq!(clock_time, BACKSTOP + TIME_DIFF * (ONE_MILLION + SLEW_RATE_PPM) / ONE_MILLION);
    }

    #[fuchsia::test]
    fn time_at_monotonic_clock_slew_slow() {
        let clock = zx::Clock::create(zx::ClockOpts::empty(), Some(BACKSTOP)).unwrap();

        let mono = zx::Time::get_monotonic();
        clock
            .update(
                zx::ClockUpdate::builder()
                    .absolute_value(mono, BACKSTOP)
                    .rate_adjust(-SLEW_RATE_PPM),
            )
            .unwrap();

        let clock_time = time_at_monotonic(&clock, mono + TIME_DIFF);
        assert_eq!(clock_time, BACKSTOP + TIME_DIFF * (ONE_MILLION - SLEW_RATE_PPM) / ONE_MILLION);
    }
}

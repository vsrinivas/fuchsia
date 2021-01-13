// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon as zx;

/// Returns the time on the clock at a given monotonic reference time. This calculates the time
/// based on the clock transform definition, which only contains the most recent segment. This
/// is only useful for calculating the time for monotonic times close to the current time.
pub fn time_at_monotonic(clock: &zx::Clock, monotonic: zx::Time) -> zx::Time {
    let monotonic_reference = monotonic.into_nanos() as i128;
    // Clock read failures should only be caused by an invalid clock object.
    let transform = clock.get_details().expect("failed to get clock details").mono_to_synthetic;
    // Calculate using the transform definition underlying a zircon clock.
    // Cast to i128 to avoid overflows in multiplication.
    let reference_offset = transform.reference_offset as i128;
    let synthetic_offset = transform.synthetic_offset as i128;
    let reference_ticks = transform.rate.reference_ticks as i128;
    let synthetic_ticks = transform.rate.synthetic_ticks as i128;

    let time_nanos = ((monotonic_reference - reference_offset) * synthetic_ticks / reference_ticks)
        + synthetic_offset;
    zx::Time::from_nanos(time_nanos as i64)
}

#[cfg(test)]
mod test {
    use {
        super::*,
        test_util::{assert_geq, assert_leq},
    };

    const BACKSTOP: zx::Time = zx::Time::from_nanos(1234567890);
    const TIME_DIFF: zx::Duration = zx::Duration::from_seconds(5);
    const SLEW_RATE_PPM: i32 = 750;
    const ONE_MILLION: i32 = 1_000_000;

    #[test]
    fn time_at_monotonic_clock_not_started() {
        let clock = zx::Clock::create(zx::ClockOpts::empty(), Some(BACKSTOP)).unwrap();
        assert_eq!(time_at_monotonic(&clock, zx::Time::get_monotonic() + TIME_DIFF), BACKSTOP);
    }

    #[test]
    fn time_at_monotonic_clock_started() {
        let clock = zx::Clock::create(zx::ClockOpts::empty(), Some(BACKSTOP)).unwrap();

        let mono_before = zx::Time::get_monotonic();
        clock.update(zx::ClockUpdate::new().value(BACKSTOP)).unwrap();
        let mono_after = zx::Time::get_monotonic();

        let mono_radius = (mono_after - mono_before) / 2;
        let mono_avg = mono_before + mono_radius;

        let clock_time = time_at_monotonic(&clock, mono_avg + TIME_DIFF);
        assert_geq!(clock_time, BACKSTOP + TIME_DIFF - mono_radius);
        assert_leq!(clock_time, BACKSTOP + TIME_DIFF + mono_radius);
    }

    #[test]
    fn time_at_monotonic_clock_slew_fast() {
        let clock = zx::Clock::create(zx::ClockOpts::empty(), Some(BACKSTOP)).unwrap();

        let mono_before = zx::Time::get_monotonic();
        clock.update(zx::ClockUpdate::new().value(BACKSTOP).rate_adjust(SLEW_RATE_PPM)).unwrap();
        let mono_after = zx::Time::get_monotonic();

        let mono_radius = (mono_after - mono_before) / 2;
        let mono_avg = mono_before + mono_radius;

        let clock_time = time_at_monotonic(&clock, mono_avg + TIME_DIFF);
        let expected_clock_time =
            BACKSTOP + TIME_DIFF * (ONE_MILLION + SLEW_RATE_PPM) / ONE_MILLION;
        assert_geq!(clock_time, expected_clock_time - mono_radius);
        assert_leq!(clock_time, expected_clock_time + mono_radius);
    }

    #[test]
    fn time_at_monotonic_clock_slew_slow() {
        let clock = zx::Clock::create(zx::ClockOpts::empty(), Some(BACKSTOP)).unwrap();

        let mono_before = zx::Time::get_monotonic();
        clock.update(zx::ClockUpdate::new().value(BACKSTOP).rate_adjust(-SLEW_RATE_PPM)).unwrap();
        let mono_after = zx::Time::get_monotonic();

        let mono_radius = (mono_after - mono_before) / 2;
        let mono_avg = mono_before + mono_radius;

        let clock_time = time_at_monotonic(&clock, mono_avg + TIME_DIFF);
        let expected_clock_time =
            BACKSTOP + TIME_DIFF * (ONE_MILLION - SLEW_RATE_PPM) / ONE_MILLION;
        assert_geq!(clock_time, expected_clock_time - mono_radius);
        assert_leq!(clock_time, expected_clock_time + mono_radius);
    }
}

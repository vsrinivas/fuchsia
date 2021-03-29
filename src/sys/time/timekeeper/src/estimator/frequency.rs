// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(unused)]

use {crate::time_source::Sample, fuchsia_zircon as zx};

/// The time period over which a set of time samples are collected to update the frequency estimate.
const FREQUENCY_ESTIMATION_WINDOW: zx::Duration = zx::Duration::from_hours(24);

/// The minimum number of samples that must be received in a frequency estimation window for it to
/// be eligible for a frequency estimate.
const FREQUENCY_ESTIMATION_MIN_SAMPLES: u32 = 12;

/// Failure modes that may occur while calling `EstimationWindow.add_sample()`.
#[derive(Clone, Debug, Eq, PartialEq)]
enum AddSampleError {
    BeforeWindow,
    AfterWindow,
}

/// Failure modes that may occur while calling `EstimationWindow.frequency()`.
#[derive(Clone, Debug, Eq, PartialEq)]
enum GetFrequencyError {
    InsufficientSamples,
    PotentialLeapSecond,
}

/// An accumulation of frequency data from a set of `Sample`s within a time window.
#[derive(Debug, PartialEq)]
struct EstimationWindow {
    /// The UTC time of the first sample in the window.
    initial_utc: zx::Time,
    /// The monotonic time of the first sample in the window.
    initial_monotonic: zx::Time,
    /// The number of samples accepted into this window.
    sample_count: u32,
    /// The sum of (UTC - initial UTC), in nanoseconds, over all samples.
    sum_utc: f64,
    /// The sum of (monotonic - initial monotonic), in nanoseconds, over all samples.
    sum_monotonic: f64,
    /// The sum of (monotonic - initial monotonic)^2, in nanoseconds squared, over all samples.
    sum_monotonic_squared: f64,
    /// The sum of (UTC - initial UTC)*(monotonic - initial monotonic), in nanoseconds squared,
    /// over all samples.
    sum_utc_monotonic: f64,
}

impl EstimationWindow {
    /// Construct a new `EstimationWindow` initialized to the supplied sample.
    fn new(sample: &Sample) -> Self {
        let utc = sample.utc.into_nanos() as f64;
        let monotonic = sample.monotonic.into_nanos() as f64;
        EstimationWindow {
            initial_utc: sample.utc,
            initial_monotonic: sample.monotonic,
            sample_count: 1,
            sum_utc: 0.0,
            sum_monotonic: 0.0,
            sum_monotonic_squared: 0.0,
            sum_utc_monotonic: 0.0,
        }
    }

    /// Attempts to add a new sample into this `EstimationWindow`. Returns an error if the sample
    /// is outside the allowable window defined by the first sample.
    fn add_sample(&mut self, sample: &Sample) -> Result<(), AddSampleError> {
        if sample.utc < self.initial_utc {
            return Err(AddSampleError::BeforeWindow);
        } else if sample.utc > self.initial_utc + FREQUENCY_ESTIMATION_WINDOW {
            return Err(AddSampleError::AfterWindow);
        }

        let utc = (sample.utc - self.initial_utc).into_nanos() as f64;
        let monotonic = (sample.monotonic - self.initial_monotonic).into_nanos() as f64;
        self.sample_count += 1;
        self.sum_utc += utc;
        self.sum_monotonic += monotonic;
        self.sum_monotonic_squared += monotonic * monotonic;
        self.sum_utc_monotonic += utc * monotonic;
        Ok(())
    }

    /// Returns the average frequency over the time window, or an error if the window is not
    /// eligible for some reason.
    fn frequency(&self) -> Result<f64, GetFrequencyError> {
        if self.sample_count < FREQUENCY_ESTIMATION_MIN_SAMPLES {
            return Err(GetFrequencyError::InsufficientSamples);
        } else if self.overlaps_leap_second() {
            return Err(GetFrequencyError::PotentialLeapSecond);
        }

        let sample_count = self.sample_count as f64;
        let denominator =
            self.sum_monotonic_squared - self.sum_monotonic * self.sum_monotonic / sample_count;
        let numerator = self.sum_utc_monotonic - self.sum_utc * self.sum_monotonic / sample_count;
        Ok(numerator / denominator)
    }

    /// Returns true if this `EstimationWindow` overlaps the 24hour smearing window centered on a
    /// potential leap second.
    fn overlaps_leap_second(&self) -> bool {
        // TODO(jsankey): Implement this method before we encounter a leap second. This will be
        // Dec 2021 at the earliest.
        false
    }
}

#[cfg(test)]
mod test {
    use {super::*, chrono::DateTime, test_util::assert_near, zx::DurationNum};

    const INITIAL_MONO: zx::Time = zx::Time::from_nanos(7_000_000_000);
    const STD_DEV: zx::Duration = zx::Duration::from_millis(88);

    // This time is nowhere near a leap second.
    const TEST_UTC_STR: &str = "2021-03-25T13:22:52-08:00";

    /// Creates a single sample with the supplied times and the standard standard deviation
    /// Initial UTC is specified as an RFC3339 string.
    fn create_sample(utc_string: &str, monotonic: zx::Time) -> Sample {
        let chrono_utc = DateTime::parse_from_rfc3339(utc_string).expect("Invalid UTC string");
        Sample::new(zx::Time::from_nanos(chrono_utc.timestamp_nanos()), monotonic, STD_DEV)
    }

    /// Create a vector of evenly spaced samples following the supplied reference sample with a
    /// frequency specified as (utc ticks)/(monotonic ticks).
    fn create_sample_set(
        reference_sample: &Sample,
        quantity: u32,
        utc_spacing: zx::Duration,
        frequency: f64,
    ) -> Vec<Sample> {
        let monotonic_spacing =
            zx::Duration::from_nanos((utc_spacing.into_nanos() as f64 / frequency) as i64);

        let mut vec = Vec::<Sample>::new();
        let mut utc = reference_sample.utc;
        let mut monotonic = reference_sample.monotonic;

        for _ in 0..quantity {
            utc += utc_spacing;
            monotonic += monotonic_spacing;
            vec.push(Sample::new(utc, monotonic, STD_DEV));
        }
        vec
    }

    #[fuchsia::test]
    fn estimation_window_valid() {
        for freq in &[0.999, 1.0, 1.001] {
            let initial_sample = create_sample(TEST_UTC_STR, INITIAL_MONO);
            let mut window = EstimationWindow::new(&initial_sample);
            for sample in create_sample_set(&initial_sample, 20, 1.hour(), *freq) {
                window.add_sample(&sample).unwrap();
            }
            assert_near!(window.frequency().unwrap(), *freq, 0.0000001);
        }
    }
    #[fuchsia::test]
    fn estimation_window_outside_window() {
        let initial = create_sample(TEST_UTC_STR, INITIAL_MONO);
        let earlier = Sample::new(initial.utc - 1.hour(), initial.monotonic - 1.hour(), STD_DEV);
        let later = Sample::new(initial.utc + 36.hours(), initial.monotonic + 36.hours(), STD_DEV);

        let mut window = EstimationWindow::new(&initial);
        assert_eq!(window.add_sample(&earlier), Err(AddSampleError::BeforeWindow));
        assert_eq!(window.add_sample(&later), Err(AddSampleError::AfterWindow));
    }
    #[fuchsia::test]
    fn estimation_window_insufficient_samples() {
        let initial_sample = create_sample(TEST_UTC_STR, INITIAL_MONO);
        let mut window = EstimationWindow::new(&initial_sample);
        for sample in create_sample_set(&initial_sample, 10, 1.hour(), 0.9876) {
            window.add_sample(&sample).unwrap();
        }
        assert_eq!(window.frequency(), Err(GetFrequencyError::InsufficientSamples));
    }
}

// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{enums::FrequencyDiscardReason, time_source::Sample},
    chrono::{Datelike, Duration, TimeZone, Utc},
    fuchsia_zircon as zx,
    std::mem,
};

/// The time period over which a set of time samples are collected to update the frequency estimate.
const FREQUENCY_ESTIMATION_WINDOW: zx::Duration = zx::Duration::from_hours(24);

/// The minimum number of samples that must be received in a frequency estimation window for it to
/// be eligible for a frequency estimate.
const FREQUENCY_ESTIMATION_MIN_SAMPLES: u32 = 12;

/// The factor applied to the current period during the exponentially weighted moving average
/// calculation of frequency.
const FREQUENCY_ESTIMATION_SMOOTHING: f64 = 0.25;

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

impl Into<FrequencyDiscardReason> for GetFrequencyError {
    fn into(self) -> FrequencyDiscardReason {
        match self {
            Self::InsufficientSamples => FrequencyDiscardReason::InsufficientSamples,
            Self::PotentialLeapSecond => FrequencyDiscardReason::PotentialLeapSecond,
        }
    }
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

    /// Returns true if this `EstimationWindow` overlaps the 24 hour smearing window centered on a
    /// potential leap second.
    fn overlaps_leap_second(&self) -> bool {
        let window_start_utc = Utc.timestamp_nanos(self.initial_utc.into_nanos());
        let window_end_utc =
            Utc.timestamp_nanos((self.initial_utc + FREQUENCY_ESTIMATION_WINDOW).into_nanos());
        // The month is sufficient to tell us which leap second insertion point we are potentially
        // close to.
        let leap_second = match window_start_utc.month() {
            1 => Utc.ymd(window_start_utc.year(), 1, 1),
            6 | 7 => Utc.ymd(window_start_utc.year(), 7, 1),
            12 => Utc.ymd(window_start_utc.year() + 1, 1, 1),
            _ => return false,
        }
        .and_hms(0, 0, 0);
        // If the start of the estimation window is less than 12 hours after the leap second and the
        // end of the estimation window is less than 12 hours before the leap second, the estimation
        // window will overlap the 24 hour smearing period centered on the leap second.
        window_end_utc > (leap_second - Duration::hours(12))
            && window_start_utc < (leap_second + Duration::hours(12))
    }
}

/// Maintains an estimate of the frequency at which UTC time moves with respect to monotonic time
/// on this device.
///
/// Note that this is the inverse of the local oscillator frequency: Local oscillator frequency
/// expresses the length of a monotonic nanosecond with respect to an actual nanosecond in the
/// physical universe and we assume that over long periods and excluding leap seconds UTC accurately
/// tracks the physical universe.
///
/// The frequency estimate is implemented as an exponentially weighted moving average of window
/// frequency estimates, each calculated using least squares regression over a 24 hour window.
#[derive(Debug)]
pub struct FrequencyEstimator {
    /// The estimated frequency.
    estimate: f64,
    /// The number of time windows that have been included in this estimate of frequency.
    window_count: u32,
    /// The currently active `EstimationWindow`.
    current_window: EstimationWindow,
}

impl FrequencyEstimator {
    /// Construct a new FrequencyEstimator initialized with the supplied sample.
    pub fn new(sample: &Sample) -> Self {
        FrequencyEstimator {
            estimate: 1.0,
            window_count: 0,
            current_window: EstimationWindow::new(sample),
        }
    }

    /// Update the estimate to include the supplied sample. Very occasionally this will lead to a
    /// change in the estimated frequency, in which case the new frequency and the total number of
    /// windows used in producing this new frequency are returned.
    pub fn update(
        &mut self,
        sample: &Sample,
    ) -> Result<Option<(f64, u32)>, FrequencyDiscardReason> {
        match self.current_window.add_sample(sample) {
            Ok(()) => {
                // This sample was accepted into the current window so didn't lead to a
                // frequency change.
                Ok(None)
            }
            Err(AddSampleError::BeforeWindow) => {
                // If the server had a large step back in time theoretically we might see a UTC
                // before the window we were accumulating into, in that case just start over.
                self.current_window = EstimationWindow::new(sample);
                Err(FrequencyDiscardReason::UtcBeforeWindow)
            }
            Err(AddSampleError::AfterWindow) => {
                // This sample was after the current window. Start a new window and if the previous
                // window was viable use its data.
                let previous_window =
                    mem::replace(&mut self.current_window, EstimationWindow::new(sample));
                match previous_window.frequency() {
                    Ok(window_estimate) => {
                        if self.window_count == 0 {
                            // If this is the first window use the estimated frequency directly...
                            self.estimate = window_estimate;
                        } else {
                            // ... otherwise combine it with the previous estimate.
                            self.estimate = window_estimate * FREQUENCY_ESTIMATION_SMOOTHING
                                + self.estimate * (1f64 - FREQUENCY_ESTIMATION_SMOOTHING);
                        }
                        self.window_count += 1;
                        Ok(Some((self.estimate, self.window_count)))
                    }
                    Err(err) => Err(err.into()),
                }
            }
        }
    }

    /// Update the estimate to include the supplied sample that is disjoint from previous samples.
    /// This will discard any current estimation window so never leads to a new frequency estimate.
    pub fn update_disjoint(&mut self, sample: &Sample) {
        self.current_window = EstimationWindow::new(sample);
    }

    /// Returns the number of completed windows incorporated in this estimator.
    #[cfg(test)]
    pub fn window_count(&self) -> u32 {
        self.window_count
    }

    /// Returns the number of samples in the current estimation window.
    #[cfg(test)]
    pub fn current_sample_count(&self) -> u32 {
        self.current_window.sample_count
    }
}

#[cfg(test)]
mod test {
    use {super::*, chrono::DateTime, test_util::assert_near, zx::DurationNum};

    const INITIAL_MONO: zx::Time = zx::Time::from_nanos(7_000_000_000);
    const STD_DEV: zx::Duration = zx::Duration::from_millis(88);

    // This time is nowhere near a leap second.
    const TEST_UTC_STR: &str = "2021-03-25T13:22:52-08:00";
    // This time overlaps a potential leap second.
    const LEAP_UTC_STR: &str = "2021-06-30T23:59:59+00:00";

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

    /// Append new evenly spaced samples to the supplied vector with a frequency specified as
    /// (utc ticks)/(monotonic ticks).
    fn extend_sample_set(
        samples: &mut Vec<Sample>,
        quantity: u32,
        utc_spacing: zx::Duration,
        frequency: f64,
    ) {
        let previous_sample = samples.last().expect("No existing samples to extend");
        let mut new_samples = create_sample_set(previous_sample, quantity, utc_spacing, frequency);
        samples.append(&mut new_samples);
    }

    fn assert_estimator_update(
        estimator: &mut FrequencyEstimator,
        sample: &Sample,
        expected_frequency: f64,
        expected_window_count: u32,
    ) {
        let (frequency, window_count) = estimator
            .update(sample)
            .expect("update returned an error")
            .expect("update did not lead to an updated frequency");
        assert_near!(frequency, expected_frequency, 0.0000001);
        assert_eq!(window_count, expected_window_count);
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

    #[fuchsia::test]
    fn estimation_window_overlaps_leap_second() {
        let times_and_overlaps = [
            ("2021-06-29T11:59:59+00:00", false),
            ("2021-06-29T12:00:01+00:00", true),
            ("2021-06-30T23:59:59+00:00", true),
            ("2021-07-01T11:59:59+00:00", true),
            ("2021-07-01T12:00:01+00:00", false),
            ("2021-07-01T04:59:59-07:00", true),
            ("2021-07-01T05:00:01-07:00", false),
            ("2021-09-01T00:00:01+00:00", false),
            ("2021-12-30T11:59:59+00:00", false),
            ("2021-12-30T12:00:01+00:00", true),
            ("2022-01-01T11:59:59+00:00", true),
            ("2022-01-01T12:00:01+00:00", false),
        ];
        for (time, overlap) in times_and_overlaps {
            let window = EstimationWindow::new(&create_sample(time, INITIAL_MONO));
            assert_eq!(
                window.overlaps_leap_second(),
                overlap,
                "Leap second overlap of {} should be {}",
                time,
                overlap
            );
        }
    }

    #[fuchsia::test]
    fn frequency_estimator_valid() {
        // Two days of data at an initial frequency.
        let mut samples = vec![create_sample(TEST_UTC_STR, INITIAL_MONO)];
        extend_sample_set(&mut samples, 47, 1.hour() + 1.second(), 0.999);
        // Two more days of data at an different frequency (plus one extra sample so we can close
        // the fourth day)
        extend_sample_set(&mut samples, 49, 1.hour() + 1.second(), 0.998);

        let mut estimator = FrequencyEstimator::new(&mut samples.remove(0));

        // We should receive a frequency of exactly 0.099 after each of the first two days.
        // In the second two days the frequency should converge towards the updated value.
        for (day, expected_freq) in vec![(1, 0.999), (2, 0.999), (3, 0.99875), (4, 0.9985625)] {
            for _ in 0..23 {
                assert_eq!(estimator.update(&mut samples.remove(0)), Ok(None));
            }
            assert_estimator_update(&mut estimator, &mut samples.remove(0), expected_freq, day);
        }

        assert_eq!(estimator.window_count(), 4);
        assert_eq!(estimator.current_sample_count(), 1);
    }

    #[fuchsia::test]
    fn frequency_estimator_disjoint_update() {
        let mut samples = vec![create_sample(TEST_UTC_STR, INITIAL_MONO)];
        extend_sample_set(&mut samples, 11, 1.hour() + 1.second(), 1.1);
        extend_sample_set(&mut samples, 25, 1.hour() + 1.second(), 0.999);

        let mut estimator = FrequencyEstimator::new(&mut samples.remove(0));

        // Feed the first 12 samples then mark the 13th as disjoint, causing the first samples to
        // be ignored.
        for _ in 0..11 {
            assert_eq!(estimator.update(&mut samples.remove(0)), Ok(None));
        }
        estimator.update_disjoint(&mut samples.remove(0));
        for _ in 0..23 {
            assert_eq!(estimator.update(&mut samples.remove(0)), Ok(None));
        }
        assert_estimator_update(&mut estimator, &mut samples.remove(0), 0.999, 1);
    }

    #[fuchsia::test]
    fn frequency_estimator_insufficient_samples() {
        let mut samples = vec![create_sample(TEST_UTC_STR, INITIAL_MONO)];
        // Note the first day only has 6 samples so should be ignored.
        extend_sample_set(&mut samples, 6, 4.hour() + 1.second(), 1.1);
        extend_sample_set(&mut samples, 25, 1.hour() + 1.second(), 0.999);

        let mut estimator = FrequencyEstimator::new(&mut samples.remove(0));

        for _ in 0..5 {
            assert_eq!(estimator.update(&mut samples.remove(0)), Ok(None));
        }
        assert_eq!(
            estimator.update(&mut samples.remove(0)),
            Err(FrequencyDiscardReason::InsufficientSamples)
        );
        for _ in 0..23 {
            assert_eq!(estimator.update(&mut samples.remove(0)), Ok(None));
        }
        assert_estimator_update(&mut estimator, &mut samples.remove(0), 0.999, 1);
    }

    #[fuchsia::test]
    fn frequency_estimator_overlaps_leap_second() {
        let mut samples = vec![create_sample(LEAP_UTC_STR, INITIAL_MONO)];
        extend_sample_set(&mut samples, 25, 1.hour() + 1.second(), 0.999);

        let mut estimator = FrequencyEstimator::new(&mut samples.remove(0));
        for _ in 0..23 {
            assert_eq!(estimator.update(&mut samples.remove(0)), Ok(None));
        }
        assert_eq!(
            estimator.update(&mut samples.remove(0)),
            Err(FrequencyDiscardReason::PotentialLeapSecond)
        );
    }
}

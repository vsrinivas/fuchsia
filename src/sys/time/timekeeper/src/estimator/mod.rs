// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        diagnostics::{Diagnostics, Event},
        enums::Track,
        time_source::Sample,
    },
    chrono::prelude::*,
    fuchsia_zircon as zx,
    lazy_static::lazy_static,
    log::{info, warn},
    std::{cmp, sync::Arc},
};

/// One million for PPM calculations
const MILLION: u64 = 1_000_000;

/// The standard deviation of the system oscillator frequency error in parts per million, used to
/// control the growth in error bound.
const OSCILLATOR_ERROR_STD_DEV_PPM: u64 = 15;

lazy_static! {
    /// The variance (i.e. standard deviation squared) of the system oscillator frequency error,
    /// used to control the growth in uncertainty during the prediction phase.
    static ref OSCILLATOR_ERROR_VARIANCE: f64 =
        (OSCILLATOR_ERROR_STD_DEV_PPM as f64 / MILLION as f64).powi(2);
}

/// The minimum covariance allowed for the UTC estimate in nanoseconds squared. This helps the
/// kalman filter not drink its own bathwater after receiving very low uncertainly updates from a
/// time source (i.e. become so confident in its internal estimate that it effectively stops
/// accepting new information).
const MIN_COVARIANCE: f64 = 1e12;

/// The factor to apply to standard deviations when producing an error bound. The current setting of
/// two sigma approximately corresponds to a 95% confidence interval.
const ERROR_BOUND_FACTOR: u64 = 2;

/// Converts a zx::Duration to a floating point number of nanoseconds.
fn duration_to_f64(duration: zx::Duration) -> f64 {
    duration.into_nanos() as f64
}

/// Converts a floating point number of nanoseconds to a zx::Duration.
fn f64_to_duration(float: f64) -> zx::Duration {
    zx::Duration::from_nanos(float as i64)
}

/// Maintains an estimate of the relationship between true UTC time and monotonic time on this
/// device, based on time samples received from one or more time sources.
///
/// The UTC estimate is implemented as a two dimensional Kalman filter where
///    state vector = [estimated_utc, estimated_frequency]
///
/// estimated_utc is maintained as f64 nanoseconds since a reference UTC (initialized as the first
/// UTC received by the filter). This keeps the absolute values and therefore the floating point
/// exponents lower than if we worked with time since UNIX epoch, so minimizes floating point
/// conversion errors. The filter can run for ~100 days from the reference point before a conversion
/// error of 1ns can occur.
///
/// estimated_frequency is considered a fixed value by the filter, i.e. has a covariance of zero
/// and an observation model term of zero.
#[derive(Debug)]
pub struct Estimator<D: Diagnostics> {
    /// A reference utc from which the estimate is maintained.
    reference_utc: zx::Time,
    /// The monotonic time at which the estimate applies.
    monotonic: zx::Time,
    /// Element 0 of the state vector, i.e. estimated utc after reference_utc, in nanoseconds.
    estimate_0: f64,
    /// Element 1 of the state vector, i.e. estimated oscillator frequency as a factor.
    estimate_1: f64,
    /// Element 0,0 of the covariance matrix, i.e. utc estimate covariance in nanoseconds squared.
    /// Note 0,0 is the only non-zero element in the matrix.
    covariance_00: f64,
    /// The track of the estimate being managed.
    track: Track,
    /// A diagnostics implementation for recording events of note.
    diagnostics: Arc<D>,
}

impl<D: Diagnostics> Estimator<D> {
    /// Construct a new estimator initialized to the supplied sample.
    pub fn new(track: Track, sample: Sample, diagnostics: Arc<D>) -> Self {
        let Sample { utc, monotonic, std_dev } = sample;
        let covariance_00 = duration_to_f64(std_dev).powf(2.0).max(MIN_COVARIANCE);
        diagnostics.record(Event::EstimateUpdated {
            track,
            offset: utc - monotonic,
            sqrt_covariance: f64_to_duration(covariance_00.sqrt()),
        });
        Estimator {
            reference_utc: utc,
            monotonic,
            estimate_0: 0f64,
            estimate_1: 1f64,
            covariance_00,
            track,
            diagnostics,
        }
    }

    /// Propagate the estimate forward to the requested monotonic time.
    fn predict(&mut self, monotonic: zx::Time) {
        let monotonic_step = duration_to_f64(monotonic - self.monotonic);
        self.monotonic = monotonic;
        // Estimated UTC increases by (change in monotonic time) * frequency.
        self.estimate_0 += self.estimate_1 * monotonic_step;
        // Estimated covariance increases as a function of the time step and oscillator error.
        self.covariance_00 += monotonic_step.powf(2.0) * *OSCILLATOR_ERROR_VARIANCE;
    }

    /// Correct the estimate by incorporating measurement data.
    fn correct(&mut self, utc: zx::Time, std_dev: zx::Duration) {
        let measurement_variance = duration_to_f64(std_dev).powf(2.0);
        let measurement_utc_offset = duration_to_f64(utc - self.reference_utc);
        // Gain is based on the relative variance of the apriori estimate and the new measurement...
        let k_0 = self.covariance_00 / (self.covariance_00 + measurement_variance);
        // ...and determines how much the measurement impacts the apriori estimate...
        self.estimate_0 += k_0 * (measurement_utc_offset - self.estimate_0);
        // ...and how much the covariance shrinks.
        self.covariance_00 = ((1f64 - k_0) * self.covariance_00).max(MIN_COVARIANCE);
    }

    /// Update the estimate to include the supplied sample.
    pub fn update(&mut self, Sample { utc, monotonic, std_dev }: Sample) {
        // Ignore any updates that are earlier than the current filter state. Samples from a single
        // time source should arrive in order due to the validation in time_source_manager, but its
        // not impossible that a backwards step occurs during a time source switch.
        if monotonic < self.monotonic {
            warn!(
                "Discarded update at monotonic={}, prior to current estimate monotonic={}",
                monotonic.into_nanos(),
                self.monotonic.into_nanos()
            );
            return;
        }

        // Calculate apriori by moving the estimate forward to the measurement's monotonic time.
        self.predict(monotonic);
        // Then correct to aposteriori by merging in the measurement.
        self.correct(utc, std_dev);

        let estimated_utc = self.reference_utc + f64_to_duration(self.estimate_0);
        self.diagnostics.record(Event::EstimateUpdated {
            track: self.track,
            offset: estimated_utc - self.monotonic,
            sqrt_covariance: f64_to_duration(self.covariance_00.sqrt()),
        });
        info!(
            "received {:?} update to {}. Estimated UTC offset={}, covariance={:e}",
            self.track,
            Utc.timestamp_nanos(utc.into_nanos()),
            (estimated_utc - monotonic).into_nanos(),
            self.covariance_00
        );
    }

    /// Returns the estimated utc at the supplied monotonic time.
    pub fn estimate(&self, monotonic: zx::Time) -> zx::Time {
        // TODO(jsankey): Accommodate an oscillator frequency error when implementing the frequency
        // correction algorithm.
        let utc_at_last_update = self.reference_utc + f64_to_duration(self.estimate_0);
        utc_at_last_update + (monotonic - self.monotonic)
    }

    /// Returns a confidence bound on the estimate error at the specified monotonic time,
    /// in nanoseconds.
    pub fn error_bound(&self, monotonic: zx::Time) -> u64 {
        // From central limit theorem assume the error tends to follow a normal distribution
        // with a standard deviation of sqrt(covariance) after many independent inputs. Error bound
        // at the time of the last update is therefore proportional to sqrt(covariance).
        // ERROR_BOUND_FACTOR defines the confidence bound we intend to deliver, with
        // ERROR_BOUND_FACTOR=2 mapping to 95% confidence.
        let bound_at_update = ERROR_BOUND_FACTOR * self.covariance_00.sqrt() as u64;
        // The error bound will grow the further we get from this time of last update.
        let time_since_update = cmp::max(zx::Duration::from_nanos(0), monotonic - self.monotonic);
        bound_at_update + error_bound_increase(time_since_update)
    }
}

/// Returns the increase in estimate error over a given duration, in nanoseconds.
pub fn error_bound_increase(duration: zx::Duration) -> u64 {
    ERROR_BOUND_FACTOR * (duration.into_nanos() as u64 * OSCILLATOR_ERROR_STD_DEV_PPM) / MILLION
}

#[cfg(test)]
mod test {
    use {super::*, crate::diagnostics::FakeDiagnostics, test_util::assert_near, zx::DurationNum};

    const TIME_1: zx::Time = zx::Time::from_nanos(10_000_000_000);
    const TIME_2: zx::Time = zx::Time::from_nanos(20_000_000_000);
    const TIME_3: zx::Time = zx::Time::from_nanos(30_000_000_000);
    const OFFSET_1: zx::Duration = zx::Duration::from_seconds(777);
    const OFFSET_2: zx::Duration = zx::Duration::from_seconds(999);
    const STD_DEV_1: zx::Duration = zx::Duration::from_millis(22);
    const ZERO_DURATION: zx::Duration = zx::Duration::from_nanos(0);
    const TEST_TRACK: Track = Track::Primary;
    const SQRT_COV_1: u64 = STD_DEV_1.into_nanos() as u64;

    fn create_estimate_event(offset: zx::Duration, sqrt_covariance: u64) -> Event {
        Event::EstimateUpdated {
            track: TEST_TRACK,
            offset,
            sqrt_covariance: zx::Duration::from_nanos(sqrt_covariance as i64),
        }
    }

    #[fuchsia::test]
    fn initialize() {
        let diagnostics = Arc::new(FakeDiagnostics::new());
        let estimator = Estimator::new(
            TEST_TRACK,
            Sample::new(TIME_1 + OFFSET_1, TIME_1, STD_DEV_1),
            Arc::clone(&diagnostics),
        );
        diagnostics.assert_events(&[create_estimate_event(OFFSET_1, SQRT_COV_1)]);
        assert_eq!(estimator.estimate(TIME_1), TIME_1 + OFFSET_1);
        assert_eq!(estimator.estimate(TIME_2), TIME_2 + OFFSET_1);
        assert_eq!(estimator.error_bound(TIME_1), 2 * SQRT_COV_1);
        // Earlier time should return same error bound.
        assert_eq!(estimator.error_bound(TIME_1 - 1.second()), 2 * SQRT_COV_1);
        // Later time should have a higher bound.
        assert_eq!(
            estimator.error_bound(TIME_1 + 1.second()),
            2 * SQRT_COV_1 + 2000 * OSCILLATOR_ERROR_STD_DEV_PPM
        );
    }

    #[fuchsia::test]
    fn apply_update() {
        let diagnostics = Arc::new(FakeDiagnostics::new());
        let mut estimator = Estimator::new(
            TEST_TRACK,
            Sample::new(TIME_1 + OFFSET_1, TIME_1, STD_DEV_1),
            Arc::clone(&diagnostics),
        );
        estimator.update(Sample::new(TIME_2 + OFFSET_2, TIME_2, STD_DEV_1));

        // Expected offset is biased slightly towards the second estimate.
        let expected_offset = 88_8002_580_002.nanos();
        let expected_sqrt_cov = 15_556_529u64;
        assert_eq!(estimator.estimate(TIME_3), TIME_3 + expected_offset);
        assert_eq!(
            estimator.error_bound(TIME_3),
            2 * expected_sqrt_cov
                + ((TIME_3 - TIME_2).into_nanos() as u64 * 2 * OSCILLATOR_ERROR_STD_DEV_PPM)
                    / MILLION
        );

        diagnostics.assert_events(&[
            create_estimate_event(OFFSET_1, SQRT_COV_1),
            create_estimate_event(expected_offset, expected_sqrt_cov),
        ]);
    }

    #[fuchsia::test]
    fn kalman_filter_performance() {
        // Note: The expected outputs for these test inputs have been validated using the time
        // synchronization simulator we created during algorithm development.
        let diagnostics = Arc::new(FakeDiagnostics::new());
        let mut estimator = Estimator::new(
            TEST_TRACK,
            Sample::new(
                zx::Time::from_nanos(10001_000000000),
                zx::Time::from_nanos(1_000000000),
                zx::Duration::from_millis(50),
            ),
            Arc::clone(&diagnostics),
        );
        assert_eq!(estimator.reference_utc, zx::Time::from_nanos(10001_000000000));
        assert_near!(estimator.estimate_0, 0f64, 1.0);
        assert_near!(estimator.covariance_00, 2.5e15, 1.0);

        estimator.update(Sample::new(
            zx::Time::from_nanos(10101_100000000),
            zx::Time::from_nanos(101_000000000),
            zx::Duration::from_millis(200),
        ));
        assert_near!(estimator.estimate_0, 100_005887335.0, 1.0);
        assert_near!(estimator.covariance_00, 2.3549341505449715e15, 1.0);

        estimator.update(Sample::new(
            zx::Time::from_nanos(10300_900000000),
            zx::Time::from_nanos(301_000000000),
            zx::Duration::from_millis(100),
        ));
        assert_near!(estimator.estimate_0, 299_985642106.0, 1.0);
        assert_near!(estimator.covariance_00, 1.9119595120463945e15, 1.0);
        diagnostics.assert_events(&[
            create_estimate_event(zx::Duration::from_nanos(10000000000000), 50000000),
            create_estimate_event(zx::Duration::from_nanos(10000005887335), 48527663),
            create_estimate_event(zx::Duration::from_nanos(9999985642105), 43725959),
        ]);
    }

    #[fuchsia::test]
    fn covariance_minimum() {
        let diagnostics = Arc::new(FakeDiagnostics::new());
        let mut estimator = Estimator::new(
            TEST_TRACK,
            Sample::new(TIME_1 + OFFSET_1, TIME_1, ZERO_DURATION),
            Arc::clone(&diagnostics),
        );
        assert_eq!(estimator.covariance_00, MIN_COVARIANCE);
        estimator.update(Sample::new(TIME_2 + OFFSET_2, TIME_2, ZERO_DURATION));
        assert_eq!(estimator.covariance_00, MIN_COVARIANCE);
        diagnostics.assert_events(&[
            create_estimate_event(OFFSET_1, MIN_COVARIANCE.sqrt() as u64),
            create_estimate_event(OFFSET_2, MIN_COVARIANCE.sqrt() as u64),
        ]);
    }

    #[fuchsia::test]
    fn earlier_monotonic_ignored() {
        let diagnostics = Arc::new(FakeDiagnostics::new());
        let mut estimator = Estimator::new(
            TEST_TRACK,
            Sample::new(TIME_2 + OFFSET_1, TIME_2, STD_DEV_1),
            Arc::clone(&diagnostics),
        );
        assert_near!(estimator.estimate_0, 0.0, 1.0);
        estimator.update(Sample::new(TIME_1 + OFFSET_1, TIME_1, STD_DEV_1));
        assert_near!(estimator.estimate_0, 0.0, 1.0);
        // Ignored event should not be logged.
        diagnostics.assert_events(&[create_estimate_event(OFFSET_1, SQRT_COV_1)]);
    }

    #[fuchsia::test]
    fn error_bound_increase_fn() {
        assert_eq!(error_bound_increase(1.minute()), 1800000);
        assert_eq!(error_bound_increase(1.hour()), 108000000);
    }
}

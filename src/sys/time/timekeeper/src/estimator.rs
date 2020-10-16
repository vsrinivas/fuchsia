// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{enums::Track, time_source::Sample},
    chrono::prelude::*,
    fuchsia_zircon as zx,
    log::{info, warn},
};

/// The variance (i.e. standard deviation squared) of the system oscillator frequency error, used
/// to control the growth in uncertainty during the prediction phase.
const OSCILLATOR_ERROR_VARIANCE: f64 = 2.25e-10; // 15ppm^2

/// The minimum covariance allowed for the UTC estimate in nanoseconds squared. This helps the
/// kalman filter not drink its own bathwater after receiving very low uncertainly updates from a
/// time source (i.e. become so confident in its internal estimate that it effectively stops
/// accepting new information).
const MIN_COVARIANCE: f64 = 1e12;

/// Converts a zx::Duration to a floating point number of nanoseconds.
fn duration_to_f64(duration: zx::Duration) -> f64 {
    duration.into_nanos() as f64
}

/// Converts a floating point number of nanoseconds to a zx::Duration.
#[allow(unused)]
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
pub struct Estimator {
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
    /// The utc offset calculated using the initial sample. This is a temporary variable that lets
    /// us retain a fixed output until we are more confident in the internal state.
    offset: zx::Duration,
}

impl Estimator {
    /// Construct a new estimator inititalized to the supplied sample.
    pub fn new(track: Track, Sample { utc, monotonic, std_dev }: Sample) -> Self {
        Estimator {
            reference_utc: utc,
            monotonic,
            estimate_0: 0f64,
            estimate_1: 1f64,
            covariance_00: duration_to_f64(std_dev).powf(2.0).max(MIN_COVARIANCE),
            offset: utc - monotonic,
            track,
        }
    }

    /// Propagate the estimate forward to the requested monotonic time.
    fn predict(&mut self, monotonic: zx::Time) {
        let monotonic_step = duration_to_f64(monotonic - self.monotonic);
        self.monotonic = monotonic;
        // Estimated UTC increases by (change in monotonic time) * frequency.
        self.estimate_0 += self.estimate_1 * monotonic_step;
        // Estimated covariance increases as a function of the time step and oscillator error.
        self.covariance_00 += monotonic_step.powf(2.0) * OSCILLATOR_ERROR_VARIANCE;
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

        // TODO(jsankey): Define and send a new diagnostic event to communicate an update.
        let input_utc_chrono = Utc.timestamp_nanos(utc.into_nanos());
        let estimated_utc = self.reference_utc + f64_to_duration(self.estimate_0);
        info!(
            "received {:?} update to {}. Estimated UTC offset={}, covariance={:e}",
            self.track,
            input_utc_chrono,
            (estimated_utc - monotonic).into_nanos(),
            self.covariance_00
        );
    }

    /// Returns the estimated utc at the supplied monotonic time.
    pub fn estimate(&self, monotonic: zx::Time) -> zx::Time {
        // Until we have enough confidence in the filter behavior and clients ability to handle
        // time corrections, we currently use only the first sample we receive. All others are used
        // to update the internal state of the estimator but not the time it outputs.
        self.offset + monotonic
    }
}

#[cfg(test)]
mod test {
    use {super::*, lazy_static::lazy_static, test_util::assert_near};

    const OFFSET_1: zx::Duration = zx::Duration::from_nanos(777);
    const OFFSET_2: zx::Duration = zx::Duration::from_nanos(999);
    const STD_DEV_1: zx::Duration = zx::Duration::from_nanos(2222);
    const ZERO_DURATION: zx::Duration = zx::Duration::from_nanos(0);

    lazy_static! {
        static ref TIME_1: zx::Time = zx::Time::from_nanos(10000);
        static ref TIME_2: zx::Time = zx::Time::from_nanos(20000);
        static ref TIME_3: zx::Time = zx::Time::from_nanos(30000);
    }

    #[test]
    fn initialize_and_estimate() {
        let estimator =
            Estimator::new(Track::Primary, Sample::new(*TIME_1 + OFFSET_1, *TIME_1, STD_DEV_1));
        assert_eq!(estimator.estimate(*TIME_1), *TIME_1 + OFFSET_1);
        assert_eq!(estimator.estimate(*TIME_2), *TIME_2 + OFFSET_1);
    }

    #[test]
    fn kalman_filter_performance() {
        // Note: The expected outputs for these test inputs have been validated using the time
        // synchronization simulator we created during algorithm development.
        let mut estimator = Estimator::new(
            Track::Primary,
            Sample::new(
                zx::Time::from_nanos(10001_000000000),
                zx::Time::from_nanos(1_000000000),
                zx::Duration::from_millis(50),
            ),
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
    }

    #[test]
    fn update_ignored() {
        let mut estimator =
            Estimator::new(Track::Primary, Sample::new(*TIME_1 + OFFSET_1, *TIME_1, STD_DEV_1));
        estimator.update(Sample::new(*TIME_2 + OFFSET_2, *TIME_2, STD_DEV_1));
        assert_eq!(estimator.estimate(*TIME_3), *TIME_3 + OFFSET_1);
    }

    #[test]
    fn covariance_minimum() {
        let mut estimator =
            Estimator::new(Track::Primary, Sample::new(*TIME_1 + OFFSET_1, *TIME_1, ZERO_DURATION));
        assert_eq!(estimator.covariance_00, MIN_COVARIANCE);
        estimator.update(Sample::new(*TIME_2 + OFFSET_2, *TIME_2, ZERO_DURATION));
        assert_eq!(estimator.covariance_00, MIN_COVARIANCE);
    }

    #[test]
    fn earlier_monotonic_ignored() {
        let mut estimator =
            Estimator::new(Track::Primary, Sample::new(*TIME_2 + OFFSET_1, *TIME_2, STD_DEV_1));
        assert_near!(estimator.estimate_0, 0.0, 1.0);
        estimator.update(Sample::new(*TIME_1 + OFFSET_1, *TIME_1, STD_DEV_1));
        assert_near!(estimator.estimate_0, 0.0, 1.0);
    }
}

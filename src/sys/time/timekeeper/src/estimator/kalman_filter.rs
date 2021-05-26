// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        estimator::{frequency_to_adjust_ppm, OSCILLATOR_ERROR_STD_DEV_PPM},
        time_source::Sample,
    },
    anyhow::{anyhow, Error},
    fuchsia_zircon as zx,
    lazy_static::lazy_static,
    time_util::Transform,
};

/// One million for PPM calculations
const MILLION: u64 = 1_000_000;

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
const ERROR_BOUND_FACTOR: u32 = 2;

/// Converts a zx::Duration to a floating point number of nanoseconds.
fn duration_to_f64(duration: zx::Duration) -> f64 {
    duration.into_nanos() as f64
}

/// Converts a floating point number of nanoseconds to a zx::Duration.
fn f64_to_duration(float: f64) -> zx::Duration {
    zx::Duration::from_nanos(float as i64)
}

/// Maintains an estimate of the offset between true UTC time and monotonic time on this
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
pub struct KalmanFilter {
    /// A reference utc from which the estimate is maintained.
    reference_utc: zx::Time,
    /// The monotonic time at which the estimate applies.
    monotonic: zx::Time,
    /// Element 0 of the state vector, i.e. estimated utc after reference_utc, in nanoseconds.
    estimate_0: f64,
    /// Element 1 of the state vector, i.e. utc nanoseconds per monotonic nanosecond.
    estimate_1: f64,
    /// Element 0,0 of the covariance matrix, i.e. utc estimate covariance in nanoseconds squared.
    /// Note 0,0 is the only non-zero element in the matrix.
    covariance_00: f64,
}

impl KalmanFilter {
    /// Construct a new KalmanFilter initialized to the supplied sample.
    pub fn new(Sample { utc, monotonic, std_dev }: &Sample) -> Self {
        let covariance_00 = duration_to_f64(*std_dev).powf(2.0).max(MIN_COVARIANCE);
        KalmanFilter {
            reference_utc: utc.clone(),
            monotonic: monotonic.clone(),
            estimate_0: 0f64,
            estimate_1: 1f64,
            covariance_00,
        }
    }

    /// Propagate the estimate forward to the requested monotonic time.
    fn predict(&mut self, monotonic: &zx::Time) {
        let monotonic_step = duration_to_f64(*monotonic - self.monotonic);
        self.monotonic = monotonic.clone();
        // Estimated UTC increases by (change in monotonic time) * frequency.
        self.estimate_0 += self.estimate_1 * monotonic_step;
        // Estimated covariance increases as a function of the time step and oscillator error.
        self.covariance_00 += monotonic_step.powf(2.0) * *OSCILLATOR_ERROR_VARIANCE;
    }

    /// Correct the estimate by incorporating measurement data.
    fn correct(&mut self, utc: &zx::Time, std_dev: &zx::Duration) {
        let measurement_variance = duration_to_f64(*std_dev).powf(2.0);
        let measurement_utc_offset = duration_to_f64(*utc - self.reference_utc);
        // Gain is based on the relative variance of the apriori estimate and the new measurement...
        let k_0 = self.covariance_00 / (self.covariance_00 + measurement_variance);
        // ...and determines how much the measurement impacts the apriori estimate...
        self.estimate_0 += k_0 * (measurement_utc_offset - self.estimate_0);
        // ...and how much the covariance shrinks.
        self.covariance_00 = ((1f64 - k_0) * self.covariance_00).max(MIN_COVARIANCE);
    }

    /// Update the estimate to include the supplied sample, returning the change in estimated UTC
    /// that this caused.
    pub fn update(
        &mut self,
        Sample { utc, monotonic, std_dev }: &Sample,
    ) -> Result<zx::Duration, Error> {
        // Ignore any updates that are earlier than the current filter state. Samples from a single
        // time source should arrive in order due to the validation in time_source_manager, but its
        // not impossible that a backwards step occurs during a time source switch.
        if *monotonic < self.monotonic {
            return Err(anyhow!(
                "sample monotonic={} prior to previous monotonic={}",
                monotonic.into_nanos(),
                self.monotonic.into_nanos()
            ));
        }

        // Calculate apriori by moving the estimate forward to the measurement's monotonic time.
        self.predict(monotonic);
        let apriori_utc = self.utc();
        // Then correct to aposteriori by merging in the measurement.
        self.correct(utc, std_dev);
        let aposteriori_utc = self.utc();

        Ok(aposteriori_utc - apriori_utc)
    }

    /// Updates the frequency used to estimate utc propagation.
    pub fn update_frequency(&mut self, frequency: f64) {
        // These infrequent updates are also a good opportunity to bring the reference utc forward
        // to the current internal state.
        self.reference_utc += f64_to_duration(self.estimate_0);
        self.estimate_0 = 0f64;
        self.estimate_1 = frequency;
    }

    /// Returns a `Transform` describing the estimated synthetic time and error as a function
    /// of the monotonic time.
    pub fn transform(&self) -> Transform {
        Transform {
            monotonic_offset: self.monotonic.into_nanos(),
            synthetic_offset: self.utc().into_nanos(),
            rate_adjust_ppm: frequency_to_adjust_ppm(self.estimate_1),
            error_bound_at_offset: ERROR_BOUND_FACTOR as u64 * self.covariance_00.sqrt() as u64,
            error_bound_growth_ppm: ERROR_BOUND_FACTOR * OSCILLATOR_ERROR_STD_DEV_PPM as u32,
        }
    }

    /// Returns the monotonic time of the last state update.
    pub fn monotonic(&self) -> zx::Time {
        self.monotonic
    }

    /// Returns the estimated utc at the last state update.
    pub fn utc(&self) -> zx::Time {
        self.reference_utc + f64_to_duration(self.estimate_0)
    }

    /// Returns the square root of the last updated filter covariance.
    pub fn sqrt_covariance(&self) -> zx::Duration {
        f64_to_duration(self.covariance_00.sqrt())
    }
}

#[cfg(test)]
mod test {
    use {super::*, test_util::assert_near, zx::DurationNum};

    const TIME_1: zx::Time = zx::Time::from_nanos(10_000_000_000);
    const TIME_2: zx::Time = zx::Time::from_nanos(20_000_000_000);
    const OFFSET_1: zx::Duration = zx::Duration::from_seconds(777);
    const OFFSET_2: zx::Duration = zx::Duration::from_seconds(999);
    const STD_DEV_1: zx::Duration = zx::Duration::from_millis(22);
    const ZERO_DURATION: zx::Duration = zx::Duration::from_nanos(0);
    const SQRT_COV_1: u64 = STD_DEV_1.into_nanos() as u64;

    #[fuchsia::test]
    fn initialize() {
        let filter = KalmanFilter::new(&Sample::new(TIME_1 + OFFSET_1, TIME_1, STD_DEV_1));
        let transform = filter.transform();
        assert_eq!(
            transform,
            Transform {
                monotonic_offset: TIME_1.into_nanos(),
                synthetic_offset: (TIME_1 + OFFSET_1).into_nanos(),
                rate_adjust_ppm: 0,
                error_bound_at_offset: 2 * SQRT_COV_1,
                error_bound_growth_ppm: 2 * OSCILLATOR_ERROR_STD_DEV_PPM as u32,
            }
        );
        assert_eq!(transform.synthetic(TIME_1), TIME_1 + OFFSET_1);
        assert_eq!(transform.synthetic(TIME_2), TIME_2 + OFFSET_1);
        assert_eq!(transform.error_bound(TIME_1), 2 * SQRT_COV_1);
        // Earlier time should return same error bound.
        assert_eq!(transform.error_bound(TIME_1 - 1.second()), 2 * SQRT_COV_1);
        // Later time should have a higher bound.
        assert_eq!(
            transform.error_bound(TIME_1 + 1.second()),
            2 * SQRT_COV_1 + 2000 * OSCILLATOR_ERROR_STD_DEV_PPM
        );
        assert_eq!(filter.monotonic(), TIME_1);
        assert_eq!(filter.utc(), TIME_1 + OFFSET_1);
        assert_eq!(filter.sqrt_covariance(), STD_DEV_1);
    }

    #[fuchsia::test]
    fn kalman_filter_performance() {
        // Note: The expected outputs for these test inputs have been validated using the time
        // synchronization simulator we created during algorithm development.
        let mut filter = KalmanFilter::new(&Sample::new(
            zx::Time::from_nanos(10001_000000000),
            zx::Time::from_nanos(1_000000000),
            zx::Duration::from_millis(50),
        ));
        assert_eq!(filter.reference_utc, zx::Time::from_nanos(10001_000000000));
        assert_near!(filter.estimate_0, 0f64, 1.0);
        assert_near!(filter.estimate_1, 1f64, 1e-9);
        assert_near!(filter.covariance_00, 2.5e15, 1.0);

        assert_eq!(
            filter
                .update(&Sample::new(
                    zx::Time::from_nanos(10101_100000000),
                    zx::Time::from_nanos(101_000000000),
                    zx::Duration::from_millis(200),
                ))
                .unwrap(),
            zx::Duration::from_nanos(100_005887335 - 0 - 100_000000000)
        );
        assert_near!(filter.estimate_0, 100_005887335.0, 1.0);
        assert_near!(filter.estimate_1, 1f64, 1e-9);
        assert_near!(filter.covariance_00, 2.3549341505449715e15, 1.0);

        assert_eq!(
            filter
                .update(&Sample::new(
                    zx::Time::from_nanos(10300_900000000),
                    zx::Time::from_nanos(301_000000000),
                    zx::Duration::from_millis(100),
                ))
                .unwrap(),
            zx::Duration::from_nanos(299_985642105 - 100_005887335 - 200_000000000)
        );
        assert_near!(filter.estimate_0, 299_985642105.0, 1.0);
        assert_near!(filter.estimate_1, 1f64, 1e-9);
        assert_near!(filter.covariance_00, 1.9119595120463945e15, 1.0);
    }

    #[fuchsia::test]
    fn frequency_change() {
        let mut filter = KalmanFilter::new(&Sample::new(
            zx::Time::from_nanos(10001_000000000),
            zx::Time::from_nanos(1_000000000),
            zx::Duration::from_millis(50),
        ));
        assert_eq!(
            filter
                .update(&Sample::new(
                    zx::Time::from_nanos(10201_000000000),
                    zx::Time::from_nanos(201_000000000),
                    zx::Duration::from_millis(50),
                ))
                .unwrap(),
            zx::Duration::from_nanos(0)
        );
        assert_eq!(filter.reference_utc, zx::Time::from_nanos(10001_000000000));
        assert_near!(filter.estimate_0, 200_000000000.0, 1.0);
        assert_near!(filter.estimate_1, 1f64, 1e-9);
        assert_near!(filter.covariance_00, 1252245957276901.8, 1.0);
        assert_eq!(
            filter.transform(),
            Transform {
                monotonic_offset: 201_000000000,
                synthetic_offset: 10201_000000000,
                rate_adjust_ppm: 0,
                error_bound_at_offset: 70774174,
                error_bound_growth_ppm: 2 * OSCILLATOR_ERROR_STD_DEV_PPM as u32,
            }
        );

        // Updating the frequency should move the internal reference forward to the last sample,
        // but otherwise doesn't change the offsets and errors reported externally.
        filter.update_frequency(0.9999);
        assert_eq!(filter.reference_utc, zx::Time::from_nanos(10201_000000000));
        assert_near!(filter.estimate_0, 0.0, 1.0);
        assert_near!(filter.estimate_1, 0.9999, 1e-9);
        assert_near!(filter.covariance_00, 1252245957276901.8, 1.0);
        assert_eq!(
            filter.transform(),
            Transform {
                monotonic_offset: 201_000000000,
                synthetic_offset: 10201_000000000,
                rate_adjust_ppm: -100,
                error_bound_at_offset: 70774174,
                error_bound_growth_ppm: 2 * OSCILLATOR_ERROR_STD_DEV_PPM as u32,
            }
        );

        // Even though this new sample has the same monotonic to UTC offset as before, based on the
        // updated frequency the new UTC is higher than the filter was expecting. It causes an
        // increase relative to the apriori estimate.
        assert_eq!(
            filter
                .update(&Sample::new(
                    zx::Time::from_nanos(10301_000000000),
                    zx::Time::from_nanos(301_000000000),
                    zx::Duration::from_millis(50),
                ))
                .unwrap(),
            zx::Duration::from_nanos(3341316)
        );
        assert_near!(filter.estimate_0, 99993341316.6, 1.0);
        assert_near!(filter.estimate_1, 0.9999, 1e-9);
        assert_near!(filter.covariance_00, 835329143746618.4, 1.0);
    }

    #[fuchsia::test]
    fn covariance_minimum() {
        let mut filter = KalmanFilter::new(&Sample::new(TIME_1 + OFFSET_1, TIME_1, ZERO_DURATION));
        assert_eq!(filter.covariance_00, MIN_COVARIANCE);
        assert!(filter.update(&Sample::new(TIME_2 + OFFSET_2, TIME_2, ZERO_DURATION)).is_ok());
        assert_eq!(filter.covariance_00, MIN_COVARIANCE);
    }

    #[fuchsia::test]
    fn earlier_monotonic_ignored() {
        let mut filter = KalmanFilter::new(&Sample::new(TIME_2 + OFFSET_1, TIME_2, STD_DEV_1));
        assert_near!(filter.estimate_0, 0.0, 1.0);
        assert!(filter.update(&Sample::new(TIME_1 + OFFSET_1, TIME_1, STD_DEV_1)).is_err());
        assert_near!(filter.estimate_0, 0.0, 1.0);
    }
}

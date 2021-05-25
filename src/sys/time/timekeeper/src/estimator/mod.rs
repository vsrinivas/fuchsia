// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod frequency;
mod kalman_filter;

use {
    crate::{
        diagnostics::{Diagnostics, Event},
        enums::{FrequencyDiscardReason, Track},
        time_source::Sample,
    },
    chrono::prelude::*,
    frequency::FrequencyEstimator,
    fuchsia_zircon as zx,
    kalman_filter::KalmanFilter,
    log::{info, warn},
    std::sync::Arc,
    time_util::Transform,
};

/// The standard deviation of the system oscillator frequency error in parts per million, used to
/// control the growth in error bound and bound the allowed frequency estimates.
const OSCILLATOR_ERROR_STD_DEV_PPM: u64 = 15;

/// The maximum allowed frequency error away from the nominal 1ns UTC == 1ns monotonic.
// TODO(jsankey): This should shortly be increased to a ppm of 2*OSCILLATOR_ERROR, but we start off
//                more conservative to limit potential impact of any frequency algorithm problems.
const MAX_FREQUENCY_ERROR: f64 = 10f64 /*value in ppm*/ / 1_000_000f64;

/// The maximum change in Kalman filter estimate that can occur before we discard any previous
/// samples being used as part of a long term frequency assessment. This is similar to the value
/// used by the ClockManager to determine when to step the externally visible clock but it need
/// not be identical. Since the steps are measured at different monotonic times there would always
/// be the possibility of an inconsistency.
const MAX_STEP_FOR_FREQUENCY_CONTINUITY: zx::Duration = zx::Duration::from_seconds(1);

/// Converts a floating point frequency to a rate adjustment in PPM.
fn frequency_to_adjust_ppm(frequency: f64) -> i32 {
    ((frequency - 1.0f64) * 1_000_000f64).round() as i32
}

/// Limits the supplied frequency to within +/- MAX_FREQUENCY_ERROR.
fn clamp_frequency(input: f64) -> f64 {
    input.clamp(1.0f64 - MAX_FREQUENCY_ERROR, 1.0f64 + MAX_FREQUENCY_ERROR)
}

/// Maintains an estimate of the relationship between true UTC time and monotonic time on this
/// device, based on time samples received from one or more time sources.
#[derive(Debug)]
pub struct Estimator<D: Diagnostics> {
    /// A Kalman Filter to track the UTC offset and uncertainty using a supplied frequency.
    filter: KalmanFilter,
    /// An estimator to produce long term estimates of the oscillator frequency.
    frequency_estimator: FrequencyEstimator,
    /// The track of the estimate being managed.
    track: Track,
    /// A diagnostics implementation for recording events of note.
    diagnostics: Arc<D>,
}

impl<D: Diagnostics> Estimator<D> {
    /// Construct a new estimator initialized to the supplied sample.
    pub fn new(track: Track, sample: Sample, diagnostics: Arc<D>) -> Self {
        let frequency_estimator = FrequencyEstimator::new(&sample);
        let filter = KalmanFilter::new(&sample);
        diagnostics.record(Event::KalmanFilterUpdated {
            track,
            monotonic: filter.monotonic(),
            utc: filter.utc(),
            sqrt_covariance: filter.sqrt_covariance(),
        });
        Estimator { filter, frequency_estimator, track, diagnostics }
    }

    /// Update the estimate to include the supplied sample.
    pub fn update(&mut self, sample: Sample) {
        // Begin by letting the Kalman filter consume the sample at its existing frequency...
        let utc = sample.utc;
        let change = match self.filter.update(&sample) {
            Ok(change) => change,
            Err(err) => {
                warn!("Rejected update: {}", err);
                return;
            }
        };
        let sqrt_covariance = self.filter.sqrt_covariance();
        self.diagnostics.record(Event::KalmanFilterUpdated {
            track: self.track,
            monotonic: self.filter.monotonic(),
            utc: self.filter.utc(),
            sqrt_covariance,
        });
        info!(
            "Received {:?} update to {}. estimate_change={}ns, sqrt_covariance={}ns",
            self.track,
            Utc.timestamp_nanos(utc.into_nanos()),
            change.into_nanos(),
            sqrt_covariance.into_nanos()
        );

        // If this was a big change just flush any long term frequency estimate ...
        if change.into_nanos().abs() > MAX_STEP_FOR_FREQUENCY_CONTINUITY.into_nanos() {
            self.frequency_estimator.update_disjoint(&sample);
            self.diagnostics.record(Event::FrequencyWindowDiscarded {
                track: self.track,
                reason: FrequencyDiscardReason::TimeStep,
            });
            info!("Discarding {:?} frequency window due to time step", self.track);
            return;
        }

        // ... otherwise see if the sample lets us calculate a new frequency we can give to
        // the Kalman filter for next time.
        match self.frequency_estimator.update(&sample) {
            Ok(Some((raw_frequency, window_count))) => {
                let clamped_frequency = clamp_frequency(raw_frequency);
                self.filter.update_frequency(clamped_frequency);
                self.diagnostics.record(Event::FrequencyUpdated {
                    track: self.track,
                    monotonic: sample.monotonic,
                    rate_adjust_ppm: frequency_to_adjust_ppm(clamped_frequency),
                    window_count,
                });
                info!(
                    "Received {:?} frequency update: raw={:.9}, clamped={:.9}",
                    self.track, raw_frequency, clamped_frequency
                );
            }
            Ok(None) => {}
            Err(reason) => {
                self.diagnostics
                    .record(Event::FrequencyWindowDiscarded { track: self.track, reason });
                warn!("Discarding {:?} frequency window: {:?}", self.track, reason);
            }
        }
    }

    /// Returns a `Transform` describing the estimated synthetic time and error as a function
    /// of the monotonic time.
    pub fn transform(&self) -> Transform {
        self.filter.transform()
    }
}

#[cfg(test)]
mod test {
    use {
        super::*,
        crate::diagnostics::FakeDiagnostics,
        fuchsia_zircon::{self as zx, DurationNum},
        test_util::assert_near,
    };

    // Note: we need to ensure the absolute times are not near the January 1st leap second.
    const TIME_1: zx::Time = zx::Time::from_nanos(100_010_000_000_000);
    const TIME_2: zx::Time = zx::Time::from_nanos(100_020_000_000_000);
    const TIME_3: zx::Time = zx::Time::from_nanos(100_030_000_000_000);
    const OFFSET_1: zx::Duration = zx::Duration::from_seconds(777);
    const OFFSET_2: zx::Duration = zx::Duration::from_seconds(999);
    const STD_DEV_1: zx::Duration = zx::Duration::from_millis(22);
    const TEST_TRACK: Track = Track::Primary;
    const SQRT_COV_1: u64 = STD_DEV_1.into_nanos() as u64;

    fn create_filter_event(
        monotonic: zx::Time,
        offset: zx::Duration,
        sqrt_covariance: u64,
    ) -> Event {
        Event::KalmanFilterUpdated {
            track: TEST_TRACK,
            monotonic: monotonic,
            utc: monotonic + offset,
            sqrt_covariance: zx::Duration::from_nanos(sqrt_covariance as i64),
        }
    }

    fn create_window_discard_event(reason: FrequencyDiscardReason) -> Event {
        Event::FrequencyWindowDiscarded { track: TEST_TRACK, reason }
    }

    #[fuchsia::test]
    fn frequency_to_adjust_ppm_test() {
        assert_eq!(frequency_to_adjust_ppm(0.999), -1000);
        assert_eq!(frequency_to_adjust_ppm(0.999999), -1);
        assert_eq!(frequency_to_adjust_ppm(1.0), 0);
        assert_eq!(frequency_to_adjust_ppm(1.000001), 1);
        assert_eq!(frequency_to_adjust_ppm(1.001), 1000);
    }

    #[fuchsia::test]
    fn clamp_frequency_test() {
        assert_near!(clamp_frequency(-452.0), 0.99999, 1e-9);
        assert_near!(clamp_frequency(0.99), 0.99999, 1e-9);
        assert_near!(clamp_frequency(1.0000001), 1.0000001, 1e-9);
        assert_near!(clamp_frequency(1.01), 1.00001, 1e-9);
    }

    #[fuchsia::test]
    fn initialize() {
        let diagnostics = Arc::new(FakeDiagnostics::new());
        let estimator = Estimator::new(
            TEST_TRACK,
            Sample::new(TIME_1 + OFFSET_1, TIME_1, STD_DEV_1),
            Arc::clone(&diagnostics),
        );
        diagnostics.assert_events(&[create_filter_event(TIME_1, OFFSET_1, SQRT_COV_1)]);
        let transform = estimator.transform();
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
    }

    #[fuchsia::test]
    fn apply_inconsistent_update() {
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
        assert_eq!(estimator.transform().synthetic(TIME_3), TIME_3 + expected_offset);

        // The frequency estimator should have discarded the first update.
        assert_eq!(estimator.frequency_estimator.window_count(), 0);
        assert_eq!(estimator.frequency_estimator.current_sample_count(), 1);

        diagnostics.assert_events(&[
            create_filter_event(TIME_1, OFFSET_1, SQRT_COV_1),
            create_filter_event(TIME_2, expected_offset, expected_sqrt_cov),
            create_window_discard_event(FrequencyDiscardReason::TimeStep),
        ]);
    }

    #[fuchsia::test]
    fn apply_consistent_update() {
        let diagnostics = Arc::new(FakeDiagnostics::new());
        let mut estimator = Estimator::new(
            TEST_TRACK,
            Sample::new(TIME_1 + OFFSET_1, TIME_1, STD_DEV_1),
            Arc::clone(&diagnostics),
        );
        estimator.update(Sample::new(TIME_2 + OFFSET_1, TIME_2, STD_DEV_1));

        let expected_sqrt_cov = 15_556_529u64;
        assert_eq!(estimator.transform().synthetic(TIME_3), TIME_3 + OFFSET_1);

        // The frequency estimator should have retained both samples.
        assert_eq!(estimator.frequency_estimator.window_count(), 0);
        assert_eq!(estimator.frequency_estimator.current_sample_count(), 2);

        diagnostics.assert_events(&[
            create_filter_event(TIME_1, OFFSET_1, SQRT_COV_1),
            create_filter_event(TIME_2, OFFSET_1, expected_sqrt_cov),
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
        assert_eq!(estimator.transform().synthetic(TIME_3), TIME_3 + OFFSET_1);
        estimator.update(Sample::new(TIME_1 + OFFSET_2, TIME_1, STD_DEV_1));
        assert_eq!(estimator.transform().synthetic(TIME_3), TIME_3 + OFFSET_1);
        // Ignored event should not be logged.
        diagnostics.assert_events(&[create_filter_event(TIME_2, OFFSET_1, SQRT_COV_1)]);
        // Nor included in the frequency estimator.
        assert_eq!(estimator.frequency_estimator.current_sample_count(), 1);
    }

    #[fuchsia::test]
    fn frequency_convergence() {
        // Generate two days of samples at a fixed, slightly erroneous, frequency.
        let reference_sample = Sample::new(TIME_1 + OFFSET_1, TIME_2, STD_DEV_1);
        let mut samples = Vec::<Sample>::new();
        {
            let test_frequency = 1.000003;
            let utc_spacing = 1.hour() + 1.millis();
            let monotonic_spacing =
                zx::Duration::from_nanos((utc_spacing.into_nanos() as f64 / test_frequency) as i64);
            for i in 1..48 {
                samples.push(Sample::new(
                    reference_sample.utc + utc_spacing * i,
                    reference_sample.monotonic + monotonic_spacing * i,
                    reference_sample.std_dev,
                ));
            }
        }

        let diagnostics = Arc::new(FakeDiagnostics::new());
        let mut estimator = Estimator::new(TEST_TRACK, reference_sample, Arc::clone(&diagnostics));

        // Run through these samples, asking the estimator to predict the utc of each sample based
        // on the sample's monotonic value, before feeding that sample into the estimator.
        for (i, sample) in samples.into_iter().enumerate() {
            let estimate = estimator.transform().synthetic(sample.monotonic);
            let error = zx::Duration::from_nanos((sample.utc - estimate).into_nanos().abs());

            // For the first day of samples the estimator will perform imperfectly until its been
            // able to estimate the long term frequency. After this it should get much better.
            // We calculated the new frequency after consuming the first window outside the first
            // day (i=23) and it begins to help on the next sample (i=24).
            let expected_error = match i {
                0 => 11.millis(),
                _ if i <= 23 => 12.millis(),
                24 => 2.millis(),
                _ => 0.millis(),
            };

            assert_near!(error, expected_error, 1.millis());
            estimator.update(sample);
        }
    }
}

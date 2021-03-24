// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod kalman_filter;

use {
    crate::{
        diagnostics::{Diagnostics, Event},
        enums::Track,
        time_source::Sample,
    },
    chrono::prelude::*,
    kalman_filter::KalmanFilter,
    log::{info, warn},
    std::sync::Arc,
    time_util::Transform,
};

/// The standard deviation of the system oscillator frequency error in parts per million, used to
/// control the growth in error bound and bound the allowed frequency estimates.
const OSCILLATOR_ERROR_STD_DEV_PPM: u64 = 15;

/// Maintains an estimate of the relationship between true UTC time and monotonic time on this
/// device, based on time samples received from one or more time sources.
#[derive(Debug)]
pub struct Estimator<D: Diagnostics> {
    /// A Kalman Filter to track the UTC offset and uncertainty using a supplied frequency.
    filter: KalmanFilter,
    /// The track of the estimate being managed.
    track: Track,
    /// A diagnostics implementation for recording events of note.
    diagnostics: Arc<D>,
}

impl<D: Diagnostics> Estimator<D> {
    /// Construct a new estimator initialized to the supplied sample.
    pub fn new(track: Track, sample: Sample, diagnostics: Arc<D>) -> Self {
        let filter = KalmanFilter::new(sample);
        diagnostics.record(Event::EstimateUpdated {
            track,
            offset: filter.offset(),
            sqrt_covariance: filter.sqrt_covariance(),
        });
        Estimator { filter, track, diagnostics }
    }

    /// Update the estimate to include the supplied sample.
    pub fn update(&mut self, sample: Sample) {
        let utc = sample.utc;
        if let Err(err) = self.filter.update(sample) {
            warn!("Rejected update: {}", err);
            return;
        }
        let offset = self.filter.offset();
        let sqrt_covariance = self.filter.sqrt_covariance();
        self.diagnostics.record(Event::EstimateUpdated {
            track: self.track,
            offset,
            sqrt_covariance,
        });
        info!(
            "received {:?} update to {}. Estimated UTC offset={}ns, sqrt_covariance={}ns",
            self.track,
            Utc.timestamp_nanos(utc.into_nanos()),
            offset.into_nanos(),
            sqrt_covariance.into_nanos()
        );
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
    };

    const TIME_1: zx::Time = zx::Time::from_nanos(10_000_000_000);
    const TIME_2: zx::Time = zx::Time::from_nanos(20_000_000_000);
    const TIME_3: zx::Time = zx::Time::from_nanos(30_000_000_000);
    const OFFSET_1: zx::Duration = zx::Duration::from_seconds(777);
    const OFFSET_2: zx::Duration = zx::Duration::from_seconds(999);
    const STD_DEV_1: zx::Duration = zx::Duration::from_millis(22);
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
        assert_eq!(estimator.transform().synthetic(TIME_3), TIME_3 + expected_offset);

        diagnostics.assert_events(&[
            create_estimate_event(OFFSET_1, SQRT_COV_1),
            create_estimate_event(expected_offset, expected_sqrt_cov),
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
        diagnostics.assert_events(&[create_estimate_event(OFFSET_1, SQRT_COV_1)]);
    }
}

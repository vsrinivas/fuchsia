// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_time_external::TimeSample;
use fuchsia_zircon as zx;
use push_source::Update;
use time_metrics_registry::HttpsdateBoundSizeMigratedMetricDimensionPhase as CobaltPhase;

/// An internal representation of a `fuchsia.time.external.TimeSample` that contains
/// additional metrics.
#[derive(Clone, Debug, PartialEq)]
pub struct HttpsSample {
    /// The utc time sample.
    pub utc: zx::Time,
    /// Monotonic time at which the `utc` sample was most valid.
    pub monotonic: zx::Time,
    /// Standard deviation of the error distribution of `utc`.
    pub standard_deviation: zx::Duration,
    /// The size of the final bound on utc time for the sample.
    pub final_bound_size: zx::Duration,
    /// Metrics for individual polls used to produce this sample.
    pub polls: Vec<Poll>,
}

impl Into<TimeSample> for HttpsSample {
    fn into(self) -> TimeSample {
        TimeSample {
            monotonic: Some(self.monotonic.into_nanos()),
            utc: Some(self.utc.into_nanos()),
            standard_deviation: Some(self.standard_deviation.into_nanos()),
            ..TimeSample::EMPTY
        }
    }
}

impl Into<Update> for HttpsSample {
    fn into(self) -> Update {
        Into::<TimeSample>::into(self).into()
    }
}

/// Metrics for an individual poll.
#[derive(Debug, Clone, PartialEq)]
pub struct Poll {
    /// The round trip latency observed during this poll.
    pub round_trip_time: zx::Duration,
    /// An estimate of the offset of the center of the poll from the system UTC time.
    /// TODO(fxbug.dev/67668): This value is collected for improving the error estimate produced by
    /// HTTPSDate but violates the time architecture principle where sources shouldn't use system
    /// time because that may introduce circular dependencies. This should be removed once the
    /// estimate is improved.
    pub center_offset: Option<zx::Duration>,
}

#[cfg(test)]
impl Poll {
    /// Construct a `Poll` with the given round trip time and no offset.
    pub fn with_round_trip_time(round_trip_time: zx::Duration) -> Self {
        Self { round_trip_time, center_offset: None }
    }
}

/// A phase in the HTTPS algorithm.
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum Phase {
    /// A phase comprised of the first sample only.
    Initial,
    /// A phase during which samples are produced relatively frequently to converge on an accurate
    /// time.
    Converge,
    /// A phase during which samples are produced relatively infrequently to maintain an accurate
    /// time.
    Maintain,
}

impl Into<CobaltPhase> for Phase {
    fn into(self) -> CobaltPhase {
        match self {
            Phase::Initial => CobaltPhase::Initial,
            Phase::Converge => CobaltPhase::Converge,
            Phase::Maintain => CobaltPhase::Maintain,
        }
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use std::sync::Arc;

    #[fuchsia::test]
    fn test_https_sample_into_update() {
        let utc_time = zx::Time::from_nanos(111_111_111_111);
        let monotonic_time = zx::Time::from_nanos(222_222_222_222);
        let standard_deviation = zx::Duration::from_nanos(333_333);

        let sample = HttpsSample {
            utc: utc_time,
            monotonic: monotonic_time,
            standard_deviation,
            final_bound_size: zx::Duration::from_nanos(9001),
            polls: vec![],
        };

        assert_eq!(
            <HttpsSample as Into<Update>>::into(sample),
            Update::Sample(Arc::new(TimeSample {
                monotonic: Some(monotonic_time.into_nanos()),
                utc: Some(utc_time.into_nanos()),
                standard_deviation: Some(standard_deviation.into_nanos()),
                ..TimeSample::EMPTY
            }))
        );
    }
}

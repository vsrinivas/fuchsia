// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_time_external::TimeSample;
use fuchsia_zircon as zx;
use push_source::Update;
use time_metrics_registry::HttpsdateBoundSizeMetricDimensionPhase as CobaltPhase;

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
    /// Round trip network latencies observed in polls used to produce this sample.
    pub round_trip_times: Vec<zx::Duration>,
}

impl Into<Update> for HttpsSample {
    fn into(self) -> Update {
        TimeSample {
            monotonic: Some(self.monotonic.into_nanos()),
            utc: Some(self.utc.into_nanos()),
            standard_deviation: Some(self.standard_deviation.into_nanos()),
            ..TimeSample::EMPTY
        }
        .into()
    }
}

/// A phase in the HTTPS algorithm.
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum Phase {
    /// A phase comprised of the first sample only.
    #[allow(unused)]
    Initial,
    /// A phase during which samples are produced relatively frequently to converge on an accurate
    /// time.
    #[allow(unused)]
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

    #[test]
    fn test_https_sample_into_update() {
        let utc_time = zx::Time::from_nanos(111_111_111_111);
        let monotonic_time = zx::Time::from_nanos(222_222_222_222);
        let standard_deviation = zx::Duration::from_nanos(333_333);

        let sample = HttpsSample {
            utc: utc_time,
            monotonic: monotonic_time,
            standard_deviation,
            final_bound_size: zx::Duration::from_nanos(9001),
            round_trip_times: vec![],
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

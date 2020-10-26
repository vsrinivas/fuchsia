// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_time_external::TimeSample;
use fuchsia_zircon as zx;
use push_source::Update;

/// An internal representation of a `fuchsia.time.external.TimeSample` that contains
/// additional metrics.
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
        }
        .into()
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
                standard_deviation: Some(standard_deviation.into_nanos())
            }))
        );
    }
}

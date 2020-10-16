// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{enums::Track, time_source::Sample},
    chrono::prelude::*,
    fuchsia_zircon as zx,
    log::info,
};

/// Maintains an estimate of the relationship between true UTC time and monotonic time on this
/// device, based on time samples received from one or more time sources.
pub struct Estimator {
    /// An estimated UTC time.
    utc: zx::Time,
    /// The monotonic time at which the UTC estimate applies.
    monotonic: zx::Time,
    /// The track of the estimate being managed.
    track: Track,
}

impl Estimator {
    /// Construct a new estimator inititalized to the supplied sample.
    pub fn new(track: Track, Sample { utc, monotonic, .. }: Sample) -> Self {
        Estimator { utc, monotonic, track }
    }

    /// Update the estimate to include the supplied sample.
    pub fn update(&mut self, Sample { utc, .. }: Sample) {
        // For consistency with the previous implementation, we currently use only the first
        // sample we receive. All others are discarded.
        let utc_chrono = Utc.timestamp_nanos(utc.into_nanos());
        info!("received {:?} time update to {}", self.track, utc_chrono);
    }

    /// Returns the estimated utc at the supplied monotonic time.
    pub fn estimate(&self, monotonic: zx::Time) -> zx::Time {
        (self.utc - self.monotonic) + monotonic
    }
}

#[cfg(test)]
mod test {
    use {super::*, lazy_static::lazy_static};

    const OFFSET_1: zx::Duration = zx::Duration::from_nanos(777);
    const OFFSET_2: zx::Duration = zx::Duration::from_nanos(999);
    const STD_DEV_1: zx::Duration = zx::Duration::from_nanos(2222);

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
    fn update_ignored() {
        let mut estimator =
            Estimator::new(Track::Primary, Sample::new(*TIME_1 + OFFSET_1, *TIME_1, STD_DEV_1));
        estimator.update(Sample::new(*TIME_2 + OFFSET_2, *TIME_2, STD_DEV_1));
        assert_eq!(estimator.estimate(*TIME_3), *TIME_3 + OFFSET_1);
    }
}

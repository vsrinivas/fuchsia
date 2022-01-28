// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::energy::*;

/// Struct for maintaining a signal strength exponentially weighted moving average. Differs from
/// SignalStrengthAverage, which is not exponentially weighted.
///
/// DecibelMilliWatt uses an i8 to represent the signal strength. However, due to integer rounding,
/// small updates to the average may never move an i8 value (e.g. avg(-50, -51) rounds to -50, so
/// updates of -51 will never result in an average of -51). This struct maintains the average signal
/// strength as an f64, so even small changes will affect the average.
#[derive(Clone, Debug, PartialEq)]
pub struct EwmaSignalStrength {
    current: f64,
    weighting_factor: f64,
}

impl EwmaSignalStrength {
    pub fn new(n: usize, inital_signal: DecibelMilliWatt) -> Self {
        Self { current: inital_signal.0.into(), weighting_factor: n as f64 }
    }

    pub fn dbm(&self) -> DecibelMilliWatt {
        DecibelMilliWatt(self.current.round() as i8)
    }

    pub fn femtowatt(&self) -> FemtoWatt {
        FemtoWatt::from(self.dbm())
    }
    // Using linear average, rather than logarithmic, since its more representative of performance.
    pub fn update_average(&mut self, dbm: DecibelMilliWatt) {
        let weight = 2.0 / (1.0 + self.weighting_factor);
        self.current = weight * (dbm.0 as f64) + (1.0 - weight) * self.current
    }
}
#[cfg(test)]
mod tests {
    use {super::*, test_util::assert_lt};

    #[test]
    fn test_simple_averaging_calculations() {
        let mut ewma_signal = EwmaSignalStrength::new(10, DecibelMilliWatt(-50));
        assert_eq!(ewma_signal.dbm(), DecibelMilliWatt(-50));

        // Validate average moves using exponential weighting
        ewma_signal.update_average(DecibelMilliWatt(-60));
        assert_eq!(ewma_signal.dbm(), DecibelMilliWatt(-52));

        // Validate average will eventually stabilize.
        for _ in 0..15 {
            ewma_signal.update_average(DecibelMilliWatt(-60))
        }
        assert_eq!(ewma_signal.dbm(), DecibelMilliWatt(-60));
    }

    #[test]
    fn test_small_variation_averaging() {
        let mut ewma_signal = EwmaSignalStrength::new(5, DecibelMilliWatt(-90));
        assert_eq!(ewma_signal.dbm(), DecibelMilliWatt(-90));

        // Validate that a small change that does not change the i8 dBm average still changes the
        // internal f64 average.
        ewma_signal.update_average(DecibelMilliWatt(-91));
        assert_eq!(ewma_signal.dbm(), DecibelMilliWatt(-90));
        assert_lt!(ewma_signal.current, DecibelMilliWatt(-90).0 as f64);

        // Validate that eventually the small changes are enough to change the i8 dbm average.
        for _ in 0..5 {
            ewma_signal.update_average(DecibelMilliWatt(-91));
        }
        assert_eq!(ewma_signal.dbm(), DecibelMilliWatt(-91));
    }
}

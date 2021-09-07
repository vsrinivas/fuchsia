// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Rounds the given volume level to the nearest 1%. Input should be a float between 0.0 and 1.0 and
/// the result will also be clamped to this range.
///
/// Rounding should be applied to audio levels handled by settings service that come from external
/// sources.
pub(crate) fn round_volume_level(volume: f32) -> f32 {
    ((volume * 100.0).round() / 100.0).max(0.0).min(1.0)
}

#[cfg(test)]
mod tests {
    use super::round_volume_level;

    // Various tests to verify rounding works as expected.
    #[test]
    // We're testing rounding so we want explicit float comparisons.
    #[allow(clippy::float_cmp)]
    fn test_round_volume() {
        assert_eq!(round_volume_level(1.0), 1.0);
        assert_eq!(round_volume_level(0.0), 0.0);
        assert_eq!(round_volume_level(0.222222), 0.22);
        assert_eq!(round_volume_level(0.349), 0.35);
        assert_eq!(round_volume_level(0.995), 1.0);
        assert_eq!(round_volume_level(0.994), 0.99);
    }

    // Verifies that values below 0.0 round to 0.0.
    #[test]
    // We're testing rounding so we want explicit float comparisons.
    #[allow(clippy::float_cmp)]
    fn test_round_volume_below_range() {
        assert_eq!(round_volume_level(-1.0), 0.0);
        assert_eq!(round_volume_level(-0.1), 0.0);
        assert_eq!(round_volume_level(-0.0), 0.0);
        assert_eq!(round_volume_level(std::f32::MIN), 0.0);
    }

    // Verifies that values above 1.0 round to 1.0.
    #[test]
    // We're testing rounding so we want explicit float comparisons.
    #[allow(clippy::float_cmp)]
    fn test_round_volume_above_range() {
        assert_eq!(round_volume_level(2.0), 1.0);
        assert_eq!(round_volume_level(1.1), 1.0);
        assert_eq!(round_volume_level(std::f32::MAX), 1.0);
    }
}

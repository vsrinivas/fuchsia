// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    std::ops,
    zerocopy::{AsBytes, FromBytes},
};

/// Representation of N IEEE 802.11 TimeUnits.
/// A TimeUnit is defined as 1024 micro seconds.
/// Note: Be careful with arithmetic operations on a TimeUnit. A TimeUnit is limited to 2 octets
/// and can easily overflow. However, there is usually no need to ever work with TUs > 0xFFFF.
#[repr(C)]
#[derive(AsBytes, FromBytes, Copy, Clone, Debug, Default, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct TimeUnit(pub u16);

#[cfg(target_os = "fuchsia")]
use fuchsia_zircon as zx;

#[cfg(target_os = "fuchsia")]
impl From<TimeUnit> for zx::Duration {
    fn from(tu: TimeUnit) -> zx::Duration {
        zx::Duration::from_micros(tu.into_micros())
    }
}

impl ops::Add<TimeUnit> for TimeUnit {
    type Output = Self;
    fn add(self, other_time_unit: TimeUnit) -> Self {
        Self(self.0.saturating_add(other_time_unit.0))
    }
}

impl<T> ops::Mul<T> for TimeUnit
where
    T: Into<u16>,
{
    type Output = Self;
    fn mul(self, k: T) -> Self {
        Self(self.0.saturating_mul(k.into()))
    }
}

impl TimeUnit {
    pub const DEFAULT_BEACON_INTERVAL: Self = Self(100);
    pub const MAX: Self = Self(std::u16::MAX);

    pub const fn into_micros(&self) -> i64 {
        (self.0 as i64) * 1024
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fuchsia_zircon::{self as zx, DurationNum};

    #[fuchsia::test]
    fn one_time_unit_conversion_to_microseconds() {
        assert_eq!(1024, TimeUnit(1).into_micros());
    }

    #[fuchsia::test]
    fn time_unit_conversion_to_microseconds_is_linear() {
        assert_eq!(0, TimeUnit(0).into_micros());
        assert_eq!(1024, TimeUnit(1).into_micros());
        assert_eq!(204800, TimeUnit(200).into_micros());
    }

    #[fuchsia::test]
    fn one_time_unit_conversion_to_duration() {
        assert_eq!(zx::Duration::from(TimeUnit(1)), 1024.micros());
    }

    #[fuchsia::test]
    fn time_unit_conversion_to_duration_is_linear() {
        assert_eq!(zx::Duration::from(TimeUnit(0)), 0.micros());
        assert_eq!(zx::Duration::from(TimeUnit(1)), 1024.micros());
        assert_eq!(zx::Duration::from(TimeUnit(200)), 204800.micros());
    }

    #[fuchsia::test]
    fn time_unit_multiplication_with_integer() {
        assert_eq!(TimeUnit(100) * 20_u8, TimeUnit(2000));
    }

    #[fuchsia::test]
    fn time_unit_addition_with_other_time_unit() {
        assert_eq!(TimeUnit(100) + TimeUnit(20), TimeUnit(120));
    }

    #[fuchsia::test]
    fn time_unit_addition_saturation() {
        assert_eq!(TimeUnit(1) + TimeUnit::MAX, TimeUnit::MAX);
    }

    #[fuchsia::test]
    fn time_unit_multiplication_saturation() {
        assert_eq!(TimeUnit::MAX * 2u16, TimeUnit::MAX);
    }
}

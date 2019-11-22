// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_zircon::{self as zx, DurationNum},
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

impl From<TimeUnit> for zx::Duration {
    fn from(tu: TimeUnit) -> zx::Duration {
        (tu.0 as i64 * 1024).micros()
    }
}

impl From<TimeUnit> for i64 {
    fn from(tu: TimeUnit) -> i64 {
        tu.0 as i64
    }
}

impl From<TimeUnit> for u16 {
    fn from(tu: TimeUnit) -> u16 {
        tu.0
    }
}

impl<T> ops::Add<T> for TimeUnit
where
    T: Into<u16>,
{
    type Output = Self;
    fn add(self, tus: T) -> Self {
        Self(self.0 + tus.into())
    }
}

impl<T> ops::Mul<T> for TimeUnit
where
    T: Into<u16>,
{
    type Output = Self;
    fn mul(self, tus: T) -> Self {
        Self(self.0 * tus.into())
    }
}

impl TimeUnit {
    pub const DEFAULT_BEACON_INTERVAL: Self = Self(100);
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn timeunit() {
        let mut duration: zx::Duration = TimeUnit(1).into();
        assert_eq!(duration, 1024.micros());

        duration = TimeUnit(100).into();
        assert_eq!(duration, (100 * 1024).micros());

        duration = TimeUnit::DEFAULT_BEACON_INTERVAL.into();
        assert_eq!(duration, (100 * 1024).micros());

        duration = (TimeUnit(100) * 20_u8).into();
        assert_eq!(duration, (100 * 20 * 1024).micros());

        duration = (TimeUnit(100) * TimeUnit(20)).into();
        assert_eq!(duration, (100 * 20 * 1024).micros());

        duration = (TimeUnit(100) + 20_u16).into();
        assert_eq!(duration, (120 * 1024).micros());

        duration = (TimeUnit(100) + TimeUnit(20)).into();
        assert_eq!(duration, (120 * 1024).micros());
    }
}

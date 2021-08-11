// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::common::EHandle;
use crate::runtime::DurationExt;
use fuchsia_zircon as zx;
use std::ops;

/// A time relative to the executor's clock.
#[derive(Debug, Copy, Clone, Eq, PartialEq, Ord, PartialOrd, Hash)]
#[repr(transparent)]
pub struct Time(zx::Time);

pub use zx::Duration;

impl Time {
    /// Return the current time according to the global executor.
    ///
    /// This function requires that an executor has been set up.
    pub fn now() -> Self {
        EHandle::local().inner.now()
    }

    /// Compute a deadline for the time in the future that is the
    /// given `Duration` away. Similarly to `zx::Time::after`,
    /// saturates on overflow instead of wrapping around.
    ///
    /// This function requires that an executor has been set up.
    pub fn after(duration: zx::Duration) -> Self {
        Self::now().saturating_add(duration)
    }

    /// Convert from `zx::Time`. This only makes sense if the time is
    /// taken from the same source (for the real clock, this is
    /// `zx::ClockId::Monotonic`).
    pub fn from_zx(t: zx::Time) -> Self {
        Time(t)
    }

    /// Convert into `zx::Time`. For the real clock, this will be a
    /// monotonic time.
    pub fn into_zx(self) -> zx::Time {
        self.0
    }

    /// Convert from nanoseconds.
    pub fn from_nanos(nanos: i64) -> Self {
        Self::from_zx(zx::Time::from_nanos(nanos))
    }

    /// Convert to nanoseconds.
    pub fn into_nanos(self) -> i64 {
        self.0.into_nanos()
    }

    /// Compute `zx::Duration` addition. Computes `self + `other`, saturating if overflow occurs.
    pub fn saturating_add(self, duration: zx::Duration) -> Self {
        Self(self.0.saturating_add(duration))
    }

    /// The maximum time.
    pub const INFINITE: Time = Time(zx::Time::INFINITE);

    /// The minimum time.
    pub const INFINITE_PAST: Time = Time(zx::Time::INFINITE_PAST);
}

impl From<zx::Time> for Time {
    fn from(t: zx::Time) -> Time {
        Time(t)
    }
}

impl From<Time> for zx::Time {
    fn from(t: Time) -> zx::Time {
        t.0
    }
}

impl ops::Add<zx::Duration> for Time {
    type Output = Time;
    fn add(self, d: zx::Duration) -> Time {
        Time(self.0 + d)
    }
}

impl ops::Add<Time> for zx::Duration {
    type Output = Time;
    fn add(self, t: Time) -> Time {
        Time(self + t.0)
    }
}

impl ops::Sub<zx::Duration> for Time {
    type Output = Time;
    fn sub(self, d: zx::Duration) -> Time {
        Time(self.0 - d)
    }
}

impl ops::Sub<Time> for Time {
    type Output = zx::Duration;
    fn sub(self, t: Time) -> zx::Duration {
        self.0 - t.0
    }
}

impl ops::AddAssign<zx::Duration> for Time {
    fn add_assign(&mut self, d: zx::Duration) {
        self.0.add_assign(d)
    }
}

impl ops::SubAssign<zx::Duration> for Time {
    fn sub_assign(&mut self, d: zx::Duration) {
        self.0.sub_assign(d)
    }
}

impl DurationExt for zx::Duration {
    fn after_now(self) -> Time {
        Time::after(self)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fuchsia_zircon::{self as zx, DurationNum};

    fn time_operations_param(zxt1: zx::Time, zxt2: zx::Time, d: zx::Duration) {
        let t1 = Time::from_zx(zxt1);
        let t2 = Time::from_zx(zxt2);
        assert_eq!(t1.into_zx(), zxt1);

        assert_eq!(Time::from_zx(zx::Time::INFINITE), Time::INFINITE);
        assert_eq!(Time::from_zx(zx::Time::INFINITE_PAST), Time::INFINITE_PAST);
        assert_eq!(zxt1 - zxt2, t1 - t2);
        assert_eq!(zxt1 + d, (t1 + d).into_zx());
        assert_eq!(d + zxt1, (d + t1).into_zx());
        assert_eq!(zxt1 - d, (t1 - d).into_zx());

        let mut zxt = zxt1;
        let mut t = t1;
        t += d;
        zxt += d;
        assert_eq!(zxt, t.into_zx());
        t -= d;
        zxt -= d;
        assert_eq!(zxt, t.into_zx());
    }

    #[test]
    fn time_operations() {
        time_operations_param(zx::Time::from_nanos(0), zx::Time::from_nanos(1000), 12.seconds());
        time_operations_param(
            zx::Time::from_nanos(-100000),
            zx::Time::from_nanos(65324),
            (-785).hours(),
        );
    }

    #[test]
    fn time_saturating_add() {
        assert_eq!(
            Time::from_nanos(10).saturating_add(zx::Duration::from_nanos(30)),
            Time::from_nanos(40)
        );
        assert_eq!(
            Time::from_nanos(10)
                .saturating_add(zx::Duration::from_nanos(Time::INFINITE.into_nanos())),
            Time::INFINITE
        );
        assert_eq!(
            Time::from_nanos(-10)
                .saturating_add(zx::Duration::from_nanos(Time::INFINITE_PAST.into_nanos())),
            Time::INFINITE_PAST
        );
    }
}

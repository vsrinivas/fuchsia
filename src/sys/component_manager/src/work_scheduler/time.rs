// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon as zx;

/// A `TimeSource` provides access to the current monotonic time.
pub trait TimeSource: Send + Sync {
    /// Get the current time according to the source's monotonic clock.
    fn get_monotonic(&self) -> i64;
}

/// A `TimeSource` based on canonical system monotonic clock.
pub struct RealTime {}

impl TimeSource for RealTime {
    fn get_monotonic(&self) -> i64 {
        zx::Time::get(zx::ClockId::Monotonic).into_nanos()
    }
}

impl RealTime {
    pub fn new() -> Self {
        RealTime {}
    }
}

#[cfg(test)]
pub mod test {
    use {super::TimeSource, std::ops};

    /// Time is measured in nanoseconds. This provides a constant symbol for one second.
    pub const SECOND: i64 = 1000000000;

    /// A `FakeTimeSource` stores a fixed point in time that can be incremented via `+=`.
    pub struct FakeTimeSource {
        monotonic_time: i64,
    }

    impl TimeSource for FakeTimeSource {
        fn get_monotonic(&self) -> i64 {
            self.monotonic_time
        }
    }

    impl ops::AddAssign<i64> for FakeTimeSource {
        fn add_assign(&mut self, duration: i64) {
            self.monotonic_time += duration;
        }
    }

    // Use arbitrary start monolithic time. This will surface bugs that, for example, are not
    // apparent when "time starts at 0".
    const FAKE_TIME_MONOTONIC_TIME: i64 = 374789234875;

    impl FakeTimeSource {
        pub fn new() -> Self {
            FakeTimeSource {
                monotonic_time: FAKE_TIME_MONOTONIC_TIME,
            }
        }
    }
}

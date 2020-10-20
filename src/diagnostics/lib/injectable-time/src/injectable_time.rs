// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::cell::RefCell;

/// TimeSource provides the current time in nanoseconds since the Unix epoch.
/// A `&'a dyn TimeSource` can be injected into a data structure.
/// TimeSource is implemented by UtcTime for wall-clock system time, and
/// FakeTime for a clock that is explicitly set by testing code.
pub trait TimeSource {
    fn now(&self) -> i64;
}

/// FakeTime instances return the last value that was `set()` by testing code.
/// Upon initialization, they return 0.
pub struct FakeTime {
    time: RefCell<i64>,
}

impl TimeSource for FakeTime {
    fn now(&self) -> i64 {
        *self.time.borrow()
    }
}

impl FakeTime {
    pub fn new() -> FakeTime {
        FakeTime { time: RefCell::new(0) }
    }
    pub fn set(&self, now: i64) {
        *self.time.borrow_mut() = now;
    }
}

/// UtcTime instances return the Rust system clock value each time now() is called.
pub struct UtcTime {}

impl UtcTime {
    pub fn new() -> UtcTime {
        UtcTime {}
    }
}

impl TimeSource for UtcTime {
    fn now(&self) -> i64 {
        let now_utc = chrono::prelude::Utc::now(); // Consider using SystemTime::now()?
        now_utc.timestamp() * 1_000_000_000 + now_utc.timestamp_subsec_nanos() as i64
    }
}

#[cfg(test)]
mod test {

    use super::*;

    struct TimeHolder<'a> {
        time_source: &'a dyn TimeSource,
    }

    impl<'a> TimeHolder<'a> {
        fn new(time_source: &'a dyn TimeSource) -> TimeHolder<'_> {
            TimeHolder { time_source }
        }

        fn now(&self) -> i64 {
            self.time_source.now()
        }
    }

    #[test]
    fn test_system_time() {
        let time_source = UtcTime::new();
        let time_holder = TimeHolder::new(&time_source);
        let first_time = time_holder.now();
        // Make sure the system time is ticking. If not, this will hang until the test times out.
        while time_holder.now() == first_time {}
    }

    #[test]
    fn test_fake_time() {
        let time_source = FakeTime::new();
        let time_holder = TimeHolder::new(&time_source);

        // Fake time is 0 on initialization.
        let first_time = time_holder.now();
        time_source.set(1000);
        let second_time = time_holder.now();
        // Fake time does not auto-increment.
        let third_time = time_holder.now();
        // Fake time can go backward.
        time_source.set(500);
        let fourth_time = time_holder.now();

        assert_eq!(first_time, 0);
        assert_eq!(second_time, 1000);
        assert_eq!(third_time, 1000);
        assert_eq!(fourth_time, 500);
    }
}

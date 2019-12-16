// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon::{ClockId, Time};

/// A trait for reporting the current time.
pub trait Clock {
    /// Retrieves the current time.
    fn current_time() -> Time;
}

/// An implementation of `Clock` that retrieves UTC time as reported by Zircon.
pub struct UtcClock;

impl Clock for UtcClock {
    fn current_time() -> Time {
        Time::get(ClockId::UTC)
    }
}

#[cfg(test)]
pub mod mock {
    use super::*;
    use lazy_static::lazy_static;

    lazy_static! {
        /// The fake time always reported by `FixedClock`
        pub static ref TEST_CURRENT_TIME: Time = Time::from_nanos(0x1999ad);
    }

    /// An implementation of `Clock` that always reports `TEST_CURRENT_TIME` as
    /// the current time.
    pub struct FixedClock;

    impl Clock for FixedClock {
        fn current_time() -> Time {
            TEST_CURRENT_TIME.clone()
        }
    }
}

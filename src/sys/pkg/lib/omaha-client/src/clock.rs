// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::time::SystemTime;

#[cfg(not(test))]
pub fn now() -> SystemTime {
    SystemTime::now()
}

#[cfg(test)]
pub use mock::now;

#[cfg(test)]
pub mod mock {
    use super::*;
    use std::cell::RefCell;

    thread_local!(static MOCK_TIME: RefCell<SystemTime> = RefCell::new(SystemTime::now()));

    pub fn now() -> SystemTime {
        MOCK_TIME.with(|time| *time.borrow())
    }

    pub fn set(new_time: SystemTime) {
        MOCK_TIME.with(|time| *time.borrow_mut() = new_time);
    }
}

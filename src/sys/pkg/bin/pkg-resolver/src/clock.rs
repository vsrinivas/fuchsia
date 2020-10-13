// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon as zx;

#[cfg(not(test))]
pub(crate) fn now() -> zx::Time {
    zx::Time::get_monotonic()
}

#[cfg(test)]
pub(crate) use mock::now;

#[cfg(test)]
pub(crate) mod mock {
    use {super::*, std::cell::Cell};

    thread_local!(
        static MOCK_TIME: Cell<zx::Time> = Cell::new(zx::Time::get_monotonic())
    );

    pub fn now() -> zx::Time {
        MOCK_TIME.with(|time| time.get())
    }

    pub fn set(new_time: zx::Time) {
        MOCK_TIME.with(|time| time.set(new_time));
    }
}

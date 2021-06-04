// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon::Time;

const TIMESTAMP_DIVIDEND: i64 = 1_000_000_000;

#[cfg(not(test))]
pub(crate) fn now() -> Time {
    Time::get_monotonic()
}

#[cfg(not(test))]
pub(crate) fn inspect_format_now() -> String {
    // follows syslog timestamp format: [seconds.nanos]
    let timestamp = now().into_nanos();
    let seconds = timestamp / TIMESTAMP_DIVIDEND;
    let nanos = timestamp % TIMESTAMP_DIVIDEND;
    format!("{}.{:09}", seconds, nanos)
}

#[cfg(test)]
pub(crate) use mock::now;

#[cfg(test)]
pub(crate) use mock::inspect_format_now;

#[cfg(test)]
pub(crate) mod mock {
    use super::*;
    use std::cell::RefCell;

    thread_local!(static MOCK_TIME: RefCell<Time> = RefCell::new(Time::get_monotonic()));

    pub(crate) fn now() -> Time {
        MOCK_TIME.with(|time| *time.borrow())
    }

    pub(crate) fn set(new_time: Time) {
        MOCK_TIME.with(|time| *time.borrow_mut() = new_time);
    }

    pub(crate) fn inspect_format_now() -> String {
        let timestamp = now().into_nanos();
        let seconds = timestamp / TIMESTAMP_DIVIDEND;
        let nanos = timestamp % TIMESTAMP_DIVIDEND;
        format!("{}.{:09}", seconds, nanos)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_inspect_format() {
        mock::set(Time::from_nanos(0));
        assert_eq!(String::from("0.000000000"), mock::inspect_format_now());

        mock::set(Time::from_nanos(123));
        assert_eq!(String::from("0.000000123"), mock::inspect_format_now());

        mock::set(Time::from_nanos(123_000_000_000));
        assert_eq!(String::from("123.000000000"), mock::inspect_format_now());

        mock::set(Time::from_nanos(123_000_000_123));
        assert_eq!(String::from("123.000000123"), mock::inspect_format_now());

        mock::set(Time::from_nanos(123_001_230_000));
        assert_eq!(String::from("123.001230000"), mock::inspect_format_now());
    }
}

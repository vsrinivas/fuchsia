// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon as zx;

use crate::error;
use crate::types::*;

pub const NANOS_PER_SECOND: i64 = 1000 * 1000 * 1000;

pub fn timeval_from_time(time: zx::Time) -> timeval {
    let nanos = time.into_nanos();
    timeval { tv_sec: nanos / NANOS_PER_SECOND, tv_usec: (nanos % NANOS_PER_SECOND) / 1000 }
}

pub fn timespec_from_time(time: zx::Time) -> timespec {
    let nanos = time.into_nanos();
    timespec { tv_sec: nanos / NANOS_PER_SECOND, tv_nsec: nanos % NANOS_PER_SECOND }
}

pub fn timespec_from_duration(duration: zx::Duration) -> timespec {
    let nanos = duration.into_nanos();
    timespec { tv_sec: nanos / NANOS_PER_SECOND, tv_nsec: nanos % NANOS_PER_SECOND }
}

pub fn duration_from_timespec(ts: timespec) -> Result<zx::Duration, Errno> {
    if ts.tv_nsec >= NANOS_PER_SECOND {
        return error!(EINVAL);
    }
    return Ok(zx::Duration::from_seconds(ts.tv_sec) + zx::Duration::from_nanos(ts.tv_nsec));
}

/// Returns a `zx::Time` for the given `timespec`, treating the `timespec` as an absolute point in
/// time (i.e., not relative to "now").
pub fn time_from_timespec(ts: timespec) -> Result<zx::Time, Errno> {
    let duration = duration_from_timespec(ts)?;
    Ok(zx::Time::ZERO + duration)
}

/// Returns an `itimerspec` with `it_value` set to `deadline` and `it_interval` set to `interval`.
pub fn itimerspec_from_deadline_interval(deadline: zx::Time, interval: zx::Duration) -> itimerspec {
    itimerspec {
        it_interval: timespec_from_duration(interval),
        it_value: timespec_from_time(deadline),
    }
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn test_itimerspec() {
        let deadline = zx::Time::from_nanos(2 * NANOS_PER_SECOND + 50);
        let interval = zx::Duration::from_nanos(1000);
        let time_spec = itimerspec_from_deadline_interval(deadline, interval);
        assert_eq!(time_spec.it_value.tv_sec, 2);
        assert_eq!(time_spec.it_value.tv_nsec, 50);
        assert_eq!(time_spec.it_interval.tv_nsec, 1000);
    }

    #[test]
    fn test_time_from_timespec() {
        let time_spec = timespec { tv_sec: 100, tv_nsec: 100 };
        let time = time_from_timespec(time_spec).expect("failed to create time from time spec");
        assert_eq!(time.into_nanos(), 100 * NANOS_PER_SECOND + 100);
    }

    #[test]
    fn test_invalid_time_from_timespec() {
        let time_spec = timespec { tv_sec: 100, tv_nsec: NANOS_PER_SECOND * 2 };
        assert_eq!(time_from_timespec(time_spec), Err(EINVAL));
    }
}

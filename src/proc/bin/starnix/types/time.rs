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

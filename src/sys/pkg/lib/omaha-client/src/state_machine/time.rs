// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::time::{Duration, SystemTime};

/// Convert SystemTime to micro seconds after unix epoch, if the time is before that, return 0.
pub fn time_to_i64(time: SystemTime) -> i64 {
    time.duration_since(SystemTime::UNIX_EPOCH).map(|d| d.as_micros()).unwrap_or(0) as i64
}

/// Convert micro seconds after unix epoch to SystemTime.
pub fn i64_to_time(micros: i64) -> SystemTime {
    SystemTime::UNIX_EPOCH + Duration::from_micros(micros as u64)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_round_trip() {
        assert_eq!(123456789, time_to_i64(i64_to_time(123456789)));
    }
}

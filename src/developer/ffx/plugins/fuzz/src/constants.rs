// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::duration::Duration;

pub const BYTES_PER_KB: u64 = 1 << 10;
pub const BYTES_PER_MB: u64 = 1 << 20;
pub const BYTES_PER_GB: u64 = 1 << 30;

pub const NANOS_PER_MICRO: i64 = Duration::from_micros(1).into_nanos();
pub const NANOS_PER_MILLI: i64 = Duration::from_millis(1).into_nanos();
pub const NANOS_PER_SECOND: i64 = Duration::from_seconds(1).into_nanos();
pub const NANOS_PER_MINUTE: i64 = Duration::from_seconds(60).into_nanos();
pub const NANOS_PER_HOUR: i64 = Duration::from_seconds(60 * 60).into_nanos();
pub const NANOS_PER_DAY: i64 = Duration::from_seconds(24 * 60 * 60).into_nanos();

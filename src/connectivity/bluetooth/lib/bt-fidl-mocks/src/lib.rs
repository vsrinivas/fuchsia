// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon::{Duration, DurationNum};

/// Defines a generic utility to expect calls to a FIDL message and extract their parameters.
pub mod expect;

/// Mock utilities for fuchsia.bluetooth.sys.
pub mod sys;

/// Mock utilities for fuchsia.bluetooth.gatt.
pub mod gatt;

const TIMEOUT_SECONDS: i64 = 4 * 60;

pub fn timeout_duration() -> Duration {
    TIMEOUT_SECONDS.seconds()
}

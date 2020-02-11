// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon::{Duration, DurationNum};

/// Tests for Bonding procedures
pub mod bonding;

/// Tests for fuchsia.bluetooth.sys.bootstrap protocol
pub mod bootstrap;

/// Tests for the fuchsia.bluetooth.control.Control protocol
pub mod control;

/// Tests for the Bluetooth Host driver behavior
pub mod host_driver;

/// Tests for the fuchsia.bluetooth.le.Central protocol
pub mod low_energy_central;

/// Tests for the fuchsia.bluetooth.le.Peripheral protocol
pub mod low_energy_peripheral;

/// Tests for the fuchsia.bluetooth.bredr.Profile protocol
pub mod profile;

// Use a framework-wide timeout of 4 minutes.
//
// This time is expected to be:
//   a) sufficient to avoid flakes due to infra or resource contention, except in many standard
//      deviations of unlikeliness
//   b) short enough to still provide useful feedback in those cases where asynchronous operations
//      fail
//   c) short enough to fail before the overall infra-imposed test timeout (currently 5 minutes),
//      so that we can produce specific test-relevant information in the case of failure.
const TIMEOUT_SECONDS: i64 = 4 * 60;

pub fn timeout_duration() -> Duration {
    TIMEOUT_SECONDS.seconds()
}

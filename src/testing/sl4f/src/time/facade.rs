// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use fuchsia_zircon::{self as zx, AsHandleRef};
use std::convert::TryInto;
use std::time::SystemTime;

const NANOS_IN_MILLIS: u64 = 1000000;

/// Facade providing access to system time.
#[derive(Debug)]
pub struct TimeFacade {}

impl TimeFacade {
    pub fn new() -> Self {
        TimeFacade {}
    }

    /// Returns the system's reported UTC time in millis since the Unix epoch retrieved
    /// through standard language libraries.
    pub fn system_time_millis() -> Result<u64, Error> {
        let time_millis = SystemTime::now().duration_since(SystemTime::UNIX_EPOCH)?.as_millis();
        // Zircon stores time as 64 bits so truncating from u128 should not fail.
        Ok(time_millis.try_into()?)
    }

    /// Returns the system's reported UTC time in millis since the Unix epoch, retrieved by
    /// explicitly using the clock handle provided to the runtime.
    pub fn userspace_time_millis() -> Result<u64, Error> {
        let clock = fuchsia_runtime::duplicate_utc_clock_handle(zx::Rights::READ)?;
        Ok(clock.read()?.into_nanos() as u64 / NANOS_IN_MILLIS)
    }

    /// Returns the UTC time in millis since the Unix epoch, according to the kernel maintained
    /// UTC clock (ZX_CLOCK_UTC). This clock will soon be removed in favor of the userspace clock.
    pub fn kernel_time_millis() -> Result<u64, Error> {
        // This deprecated call is used to compare the kernel clock to the userspace clock.
        // Do not copy.
        #[allow(deprecated)]
        Ok(zx::Time::get(zx::ClockId::UTC).into_nanos() as u64 / NANOS_IN_MILLIS)
    }

    /// Returns true iff system time has been synchronized with some source.
    pub async fn is_synchronized() -> Result<bool, Error> {
        let clock = fuchsia_runtime::duplicate_utc_clock_handle(zx::Rights::WAIT)?;
        match clock.wait_handle(zx::Signals::CLOCK_STARTED, zx::Time::ZERO) {
            Ok(_) => Ok(true),
            Err(zx::Status::TIMED_OUT) => Ok(false),
            Err(e) => Err(e.into()),
        }
    }
}

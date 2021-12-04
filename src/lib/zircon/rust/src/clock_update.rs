// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Type-safe bindings for Zircon clock update objects.

use crate::Time;
use fuchsia_zircon_sys as sys;

/// Specifies which properties of a clock to update. See [`Clock::update`]
#[derive(Debug, Eq, PartialEq)]
pub struct ClockUpdate {
    value: Option<Time>,
    rate_adjust: Option<i32>,
    error_bounds: Option<u64>,
}

impl ClockUpdate {
    /// Create a new ClockUpdate.
    pub fn new() -> Self {
        ClockUpdate { value: None, rate_adjust: None, error_bounds: None }
    }

    /// Update a clock's absolute value.
    pub fn value(mut self, value: Time) -> Self {
        self.value = Some(value);
        self
    }

    /// Update a clock's rate adjustment, specified in PPM deviation from nominal.
    pub fn rate_adjust(mut self, rate_adjust: i32) -> Self {
        self.rate_adjust = Some(rate_adjust);
        self
    }

    /// Update a clock's estimated error bounds, specified in nanoseconds.
    pub fn error_bounds(mut self, error_bounds: u64) -> Self {
        self.error_bounds = Some(error_bounds);
        self
    }

    /// Returns a bitfield of options to pass to [`sys::zx_clock_update`] in conjunction with a
    /// `zx_clock_update_args_v1_t` generated from this `ClockUpdate`.
    pub fn options(&self) -> u64 {
        let mut opts = sys::ZX_CLOCK_ARGS_VERSION_1;
        if self.rate_adjust.is_some() {
            opts |= sys::ZX_CLOCK_UPDATE_OPTION_RATE_ADJUST_VALID;
        }
        if self.value.is_some() {
            opts |= sys::ZX_CLOCK_UPDATE_OPTION_VALUE_VALID;
        }
        if self.error_bounds.is_some() {
            opts |= sys::ZX_CLOCK_UPDATE_OPTION_ERROR_BOUND_VALID;
        }
        opts
    }
}

impl From<ClockUpdate> for sys::zx_clock_update_args_v1_t {
    fn from(cu: ClockUpdate) -> Self {
        sys::zx_clock_update_args_v1_t {
            rate_adjust: cu.rate_adjust.unwrap_or(0),
            padding1: Default::default(),
            value: cu.value.map(Time::into_nanos).unwrap_or(0),
            error_bound: cu.error_bounds.unwrap_or(0),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn empty_update() {
        let update = ClockUpdate::new();
        assert_eq!(update.options(), sys::ZX_CLOCK_ARGS_VERSION_1);
        assert_eq!(
            sys::zx_clock_update_args_v1_t::from(update),
            sys::zx_clock_update_args_v1_t {
                rate_adjust: 0,
                padding1: Default::default(),
                value: 0,
                error_bound: 0,
            }
        );
    }

    #[test]
    fn rate_only() {
        let update = ClockUpdate::new().rate_adjust(52);
        assert_eq!(
            update.options(),
            sys::ZX_CLOCK_ARGS_VERSION_1 | sys::ZX_CLOCK_UPDATE_OPTION_RATE_ADJUST_VALID
        );
        assert_eq!(
            sys::zx_clock_update_args_v1_t::from(update),
            sys::zx_clock_update_args_v1_t {
                rate_adjust: 52,
                padding1: Default::default(),
                value: 0,
                error_bound: 0,
            }
        );
    }

    #[test]
    fn fully_specified_update() {
        let update =
            ClockUpdate::new().value(Time::from_nanos(42)).rate_adjust(52).error_bounds(62);
        assert_eq!(
            update.options(),
            sys::ZX_CLOCK_ARGS_VERSION_1
                | sys::ZX_CLOCK_UPDATE_OPTION_VALUE_VALID
                | sys::ZX_CLOCK_UPDATE_OPTION_RATE_ADJUST_VALID
                | sys::ZX_CLOCK_UPDATE_OPTION_ERROR_BOUND_VALID
        );
        assert_eq!(
            sys::zx_clock_update_args_v1_t::from(update),
            sys::zx_clock_update_args_v1_t {
                rate_adjust: 52,
                padding1: Default::default(),
                value: 42,
                error_bound: 62,
            }
        );
    }
}

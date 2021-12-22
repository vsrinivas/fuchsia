// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Type-safe bindings for Zircon clock update objects.
//!
//! Example usage:
//! ```rust
//! clock.update(ClockUpdate::builder().approximate_value(updated_time)).expect("update failed");
//!
//! let update = ClockUpdate::builder().rate_adjust(42).error_bounds(1_000_000).build();
//! clock.update(update).expect("update failed");
//! ```

use crate::Time;
use fuchsia_zircon_sys as sys;
use std::fmt::Debug;

/// A trait implemented by all components of a ClockUpdateBuilder's state.
pub trait State {
    /// Records the contents of the internal state to the supplied `clock_update_args_v2_t` struct.
    fn add_args(&self, args: &mut sys::zx_clock_update_args_v2_t);

    /// Records the validity in the supplied bitfield.
    fn add_options(&self, options: &mut u64);
}

/// A trait implemented by states that describe how to set a clock value.
pub trait ValueState: State {}

/// A trait implemented by states that describe how to set a clock rate.
pub trait RateState: State {}

/// A trait implemented by states that describe how to set a clock error.
pub trait ErrorState: State {}

/// A `ClockUpdateBuilder` state indicating no change.
pub struct Null;

impl State for Null {
    fn add_args(&self, _: &mut sys::zx_clock_update_args_v2_t) {}
    fn add_options(&self, _: &mut u64) {}
}

impl ValueState for Null {}
impl RateState for Null {}
impl ErrorState for Null {}

/// A `ClockUpdateBuilder` state indicating value should be set using a
/// (reference time, synthetic time) tuple.
pub struct AbsoluteValue {
    reference_value: Time,
    synthetic_value: Time,
}

impl State for AbsoluteValue {
    #[inline]
    fn add_args(&self, args: &mut sys::zx_clock_update_args_v2_t) {
        args.reference_value = self.reference_value.into_nanos();
        args.synthetic_value = self.synthetic_value.into_nanos();
    }

    #[inline]
    fn add_options(&self, opts: &mut u64) {
        *opts |= sys::ZX_CLOCK_UPDATE_OPTION_REFERENCE_VALUE_VALID
            | sys::ZX_CLOCK_UPDATE_OPTION_SYNTHETIC_VALUE_VALID;
    }
}

impl ValueState for AbsoluteValue {}

/// A `ClockUpdateBuilder` state indicating value should be set using only a synthetic time.
pub struct ApproximateValue(Time);

impl State for ApproximateValue {
    #[inline]
    fn add_args(&self, args: &mut sys::zx_clock_update_args_v2_t) {
        args.synthetic_value = self.0.into_nanos();
    }

    #[inline]
    fn add_options(&self, opts: &mut u64) {
        *opts |= sys::ZX_CLOCK_UPDATE_OPTION_SYNTHETIC_VALUE_VALID;
    }
}

impl ValueState for ApproximateValue {}

/// A clock update state indicating the rate should be set using the contained ppm offset.
pub struct Rate(i32);

impl State for Rate {
    #[inline]
    fn add_args(&self, args: &mut sys::zx_clock_update_args_v2_t) {
        args.rate_adjust = self.0;
    }

    #[inline]
    fn add_options(&self, opts: &mut u64) {
        *opts |= sys::ZX_CLOCK_UPDATE_OPTION_RATE_ADJUST_VALID;
    }
}

impl RateState for Rate {}

/// A clock update state indicating the clock error should be set using the contained bound in
/// nanoseconds.
pub struct Error(u64);

impl State for Error {
    #[inline]
    fn add_args(&self, args: &mut sys::zx_clock_update_args_v2_t) {
        args.error_bound = self.0;
    }

    #[inline]
    fn add_options(&self, opts: &mut u64) {
        *opts |= sys::ZX_CLOCK_UPDATE_OPTION_ERROR_BOUND_VALID;
    }
}

impl ErrorState for Error {}

/// Builder to specify how zero or more properties of a clock should be updated.
/// See [`Clock::update`].
///
/// A `ClockUpdateBuilder` may be created using `ClockUpdate::builder()`.
#[derive(Debug, Eq, PartialEq)]
pub struct ClockUpdateBuilder<V: ValueState, R: RateState, E: ErrorState> {
    value_state: V,
    rate_state: R,
    error_state: E,
}

impl<V: ValueState, R: RateState, E: ErrorState> ClockUpdateBuilder<V, R, E> {
    /// Converts this `ClockUpdateBuilder` to a `ClockUpdate`.
    #[inline]
    pub fn build(self) -> ClockUpdate {
        ClockUpdate::from(self)
    }
}

impl ClockUpdateBuilder<Null, Null, Null> {
    /// Returns an empty `ClockUpdateBuilder`.
    #[inline]
    fn new() -> Self {
        Self { value_state: Null, rate_state: Null, error_state: Null }
    }
}

impl<R: RateState, E: ErrorState> ClockUpdateBuilder<Null, R, E> {
    /// Sets an absolute value for this `ClockUpdate` using a (reference time, synthetic time) pair.
    ///
    /// Reference time is typically monotonic and synthetic time is the time tracked by the clock.
    /// Adding an absolute value is only possible when no other value has been set.
    #[inline]
    pub fn absolute_value(
        self,
        reference_value: Time,
        synthetic_value: Time,
    ) -> ClockUpdateBuilder<AbsoluteValue, R, E> {
        ClockUpdateBuilder {
            value_state: AbsoluteValue { reference_value, synthetic_value },
            rate_state: self.rate_state,
            error_state: self.error_state,
        }
    }
}

impl<E: ErrorState> ClockUpdateBuilder<Null, Null, E> {
    /// Sets an approximate value for this `ClockUpdateBuilder` using a synthetic time only.
    ///
    /// Synthetic time is the time tracked by the clock. The reference time will be set to current
    /// monotonic time when the kernel applies this clock update, meaning any delay between
    /// calculating synthetic time and applying the update will result in a clock error. Adding an
    /// approximate value is only possible when no other value has been set and when no rate has
    /// been set.
    #[inline]
    pub fn approximate_value(
        self,
        synthetic_value: Time,
    ) -> ClockUpdateBuilder<ApproximateValue, Null, E> {
        ClockUpdateBuilder {
            value_state: ApproximateValue(synthetic_value),
            rate_state: self.rate_state,
            error_state: self.error_state,
        }
    }
}

impl<E: ErrorState> ClockUpdateBuilder<Null, Null, E> {
    /// Adds a rate change in parts per million to this `ClockUpdateBuilder`.
    ///
    /// Adding a rate is only possible when the value is either not set or set to an absolute value
    /// and when no rate has been set previously.
    #[inline]
    pub fn rate_adjust(self, rate_adjust_ppm: i32) -> ClockUpdateBuilder<Null, Rate, E> {
        ClockUpdateBuilder {
            value_state: self.value_state,
            rate_state: Rate(rate_adjust_ppm),
            error_state: self.error_state,
        }
    }
}

impl<E: ErrorState> ClockUpdateBuilder<AbsoluteValue, Null, E> {
    /// Adds a rate change in parts per million to this `ClockUpdateBuilder`.
    ///
    /// Adding a rate is only possible when the value is either not set or set to an absolute value
    /// and when no rate has been set previously.
    #[inline]
    pub fn rate_adjust(self, rate_adjust_ppm: i32) -> ClockUpdateBuilder<AbsoluteValue, Rate, E> {
        ClockUpdateBuilder {
            value_state: self.value_state,
            rate_state: Rate(rate_adjust_ppm),
            error_state: self.error_state,
        }
    }
}

impl<V: ValueState, R: RateState> ClockUpdateBuilder<V, R, Null> {
    /// Adds an error bound in nanoseconds to this `ClockUpdateBuilder`.
    #[inline]
    pub fn error_bounds(self, error_bound_ns: u64) -> ClockUpdateBuilder<V, R, Error> {
        ClockUpdateBuilder {
            value_state: self.value_state,
            rate_state: self.rate_state,
            error_state: Error(error_bound_ns),
        }
    }
}

/// Specifies an update to zero or more properties of a clock. See [`Clock::update`]
#[derive(Debug, Eq, PartialEq)]
pub struct ClockUpdate {
    options: u64,
    args: sys::zx_clock_update_args_v2_t,
}

impl ClockUpdate {
    /// Returns a new, empty, `ClockUpdateBuilder`.
    #[inline]
    pub fn builder() -> ClockUpdateBuilder<Null, Null, Null> {
        ClockUpdateBuilder::new()
    }

    /// Returns a bitfield of options to pass to [`sys::zx_clock_update`] in conjunction with a
    /// `zx_clock_update_args_v2_t` generated from this `ClockUpdate`.
    #[inline]
    pub fn options(&self) -> u64 {
        self.options
    }
}

impl<V: ValueState, R: RateState, E: ErrorState> From<ClockUpdateBuilder<V, R, E>> for ClockUpdate {
    fn from(builder: ClockUpdateBuilder<V, R, E>) -> Self {
        let mut args = sys::zx_clock_update_args_v2_t::default();
        builder.value_state.add_args(&mut args);
        builder.rate_state.add_args(&mut args);
        builder.error_state.add_args(&mut args);

        let mut options = sys::ZX_CLOCK_ARGS_VERSION_2;
        builder.value_state.add_options(&mut options);
        builder.rate_state.add_options(&mut options);
        builder.error_state.add_options(&mut options);

        Self { options, args }
    }
}

impl From<ClockUpdate> for sys::zx_clock_update_args_v2_t {
    fn from(clock_update: ClockUpdate) -> Self {
        clock_update.args
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn empty_update() {
        let update = ClockUpdateBuilder::new().build();
        assert_eq!(update.options(), sys::ZX_CLOCK_ARGS_VERSION_2);
        assert_eq!(
            sys::zx_clock_update_args_v2_t::from(update),
            sys::zx_clock_update_args_v2_t {
                rate_adjust: 0,
                padding1: Default::default(),
                reference_value: 0,
                synthetic_value: 0,
                error_bound: 0,
            }
        );
    }

    #[test]
    fn rate_only() {
        let update = ClockUpdate::from(ClockUpdateBuilder::new().rate_adjust(52));
        assert_eq!(
            update.options(),
            sys::ZX_CLOCK_ARGS_VERSION_2 | sys::ZX_CLOCK_UPDATE_OPTION_RATE_ADJUST_VALID
        );
        assert_eq!(
            sys::zx_clock_update_args_v2_t::from(update),
            sys::zx_clock_update_args_v2_t {
                rate_adjust: 52,
                padding1: Default::default(),
                reference_value: 0,
                synthetic_value: 0,
                error_bound: 0,
            }
        );
    }

    #[test]
    fn approximate_value() {
        let update = ClockUpdateBuilder::new()
            .approximate_value(Time::from_nanos(42))
            .error_bounds(62)
            .build();
        assert_eq!(
            update.options(),
            sys::ZX_CLOCK_ARGS_VERSION_2
                | sys::ZX_CLOCK_UPDATE_OPTION_SYNTHETIC_VALUE_VALID
                | sys::ZX_CLOCK_UPDATE_OPTION_ERROR_BOUND_VALID
        );
        assert_eq!(
            sys::zx_clock_update_args_v2_t::from(update),
            sys::zx_clock_update_args_v2_t {
                rate_adjust: 0,
                padding1: Default::default(),
                reference_value: 0,
                synthetic_value: 42,
                error_bound: 62,
            }
        );
    }

    #[test]
    fn absolute_value() {
        let update = ClockUpdateBuilder::new()
            .absolute_value(Time::from_nanos(1000), Time::from_nanos(42))
            .rate_adjust(52)
            .error_bounds(62)
            .build();
        assert_eq!(
            update.options(),
            sys::ZX_CLOCK_ARGS_VERSION_2
                | sys::ZX_CLOCK_UPDATE_OPTION_REFERENCE_VALUE_VALID
                | sys::ZX_CLOCK_UPDATE_OPTION_SYNTHETIC_VALUE_VALID
                | sys::ZX_CLOCK_UPDATE_OPTION_RATE_ADJUST_VALID
                | sys::ZX_CLOCK_UPDATE_OPTION_ERROR_BOUND_VALID
        );
        assert_eq!(
            sys::zx_clock_update_args_v2_t::from(update),
            sys::zx_clock_update_args_v2_t {
                rate_adjust: 52,
                padding1: Default::default(),
                reference_value: 1000,
                synthetic_value: 42,
                error_bound: 62,
            }
        );
    }
}

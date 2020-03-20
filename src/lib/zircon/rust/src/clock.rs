// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Type-safe bindings for Zircon clock objects.

use crate::ok;
use crate::{AsHandleRef, Handle, HandleBased, HandleRef, Time};
use bitflags::bitflags;
use fuchsia_zircon_status::Status;
use fuchsia_zircon_sys as sys;
use std::{mem::MaybeUninit, ptr};

/// An object representing a kernel [clock], used to track the progress of time. A clock is a
/// one-dimensional affine transformation of the [clock monotonic] reference timeline which may be
/// atomically adjusted by a maintainer and observed by clients.
///
/// As essentially a subtype of `Handle`, it can be freely interconverted.
///
/// [clock]: https://fuchsia.dev/fuchsia-src/reference/kernel_objects/clock
/// [clock monotonic]: https://fuchsia.dev/fuchsia-src/reference/syscalls/clock_get_monotonic.md
#[derive(Debug, Eq, PartialEq, Ord, PartialOrd, Hash)]
#[repr(transparent)]
pub struct Clock(Handle);
impl_handle_based!(Clock);

bitflags! {
    #[repr(transparent)]
    pub struct ClockOpts: u64 {
        /// When set, creates a clock object which is guaranteed to never run backwards. Monotonic
        /// clocks must always move forward.
        const MONOTONIC = sys::ZX_CLOCK_OPT_MONOTONIC;

        /// When set, creates a clock which is guaranteed to never jump either forwards or
        /// backwards. Continuous clocks may only be maintained using frequency adjustments and are,
        /// by definition, also monotonic.
        const CONTINUOUS = sys::ZX_CLOCK_OPT_CONTINUOUS | Self::MONOTONIC.bits;

        /// When set, creates a clock that is automatically started and is initially a clone of
        /// clock monotonic. Users may still update the clock within the limits defined by the
        /// other options, the handle rights, and the backstop time of the clock.
        const AUTO_START = sys::ZX_CLOCK_OPT_AUTO_START;
    }
}

/// Fine grained details of a [`Clock`] object.
#[non_exhaustive]
#[derive(Debug, Clone)]
pub struct ClockDetails {
    /// The minimum time the clock can ever be set to.
    pub backstop: Time,

    /// The current ticks to clock transformation.
    pub ticks_to_synthetic: ClockTransformation,

    /// The current clock monotonic to clock transformation.
    pub mono_to_synthetic: ClockTransformation,

    /// The current symmetric error estimate (if any) for the clock, measured in nanoseconds.
    pub error_bounds: u64,

    /// An observation of the system tick counter which was taken during the observation of the clock.
    pub query_ticks: sys::zx_ticks_t,

    /// The last time the clock's value was updated as defined by the clock monotonic reference timeline.
    pub last_value_update_ticks: sys::zx_ticks_t,

    /// The last time the clock's rate adjustment was updated as defined by the clock monotonic reference timeline.
    pub last_rate_adjust_update_ticks: sys::zx_ticks_t,

    /// The last time the clock's error bounds were updated as defined by the clock monotonic reference timeline.
    pub last_error_bounds_update_ticks: sys::zx_ticks_t,

    /// The generation nonce.
    pub generation_counter: u32,
}

impl From<sys::zx_clock_details_v1_t> for ClockDetails {
    fn from(details: sys::zx_clock_details_v1_t) -> Self {
        ClockDetails {
            backstop: Time::from_nanos(details.backstop_time),
            ticks_to_synthetic: details.ticks_to_synthetic.into(),
            mono_to_synthetic: details.mono_to_synthetic.into(),
            error_bounds: details.error_bound,
            query_ticks: details.query_ticks,
            last_value_update_ticks: details.last_value_update_ticks,
            last_rate_adjust_update_ticks: details.last_rate_adjust_update_ticks,
            last_error_bounds_update_ticks: details.last_error_bounds_update_ticks,
            generation_counter: details.generation_counter,
        }
    }
}

/// A one-dimensional affine transformation that maps points from the reference timeline to the
/// clock timeline. See [clock transformations].
///
/// [clock transformations]: https://fuchsia.dev/fuchsia-src/concepts/kernel/clock_transformations
#[derive(Debug, Clone)]
pub struct ClockTransformation {
    /// The offset on the reference timeline, measured in reference clock ticks.
    pub reference_offset: i64,
    /// The offset on the clock timeline, measured in clock ticks (typically normalized to nanoseconds).
    pub synthetic_offset: i64,
    /// The ratio of the reference to clock rate.
    pub rate: sys::zx_clock_rate_t,
}

impl From<sys::zx_clock_transformation_t> for ClockTransformation {
    fn from(ct: sys::zx_clock_transformation_t) -> Self {
        ClockTransformation {
            reference_offset: ct.reference_offset,
            synthetic_offset: ct.synthetic_offset,
            rate: ct.rate,
        }
    }
}

impl Clock {
    /// Create a new clock object with the provided arguments. Wraps the [zx_clock_create] syscall.
    ///
    /// [zx_clock_create]: https://fuchsia.dev/fuchsia-src/reference/syscalls/clock_create
    pub fn create(opts: ClockOpts, backstop: Option<Time>) -> Result<Self, Status> {
        let mut out = 0;
        let status = match backstop {
            Some(backstop) => {
                // When using backstop time, use the API v1 args struct.
                let args = sys::zx_clock_create_args_v1_t { backstop_time: backstop.into_nanos() };
                unsafe {
                    sys::zx_clock_create(
                        sys::ZX_CLOCK_ARGS_VERSION_1 | opts.bits,
                        &args as *const _ as *const u8,
                        &mut out,
                    )
                }
            }
            None => unsafe { sys::zx_clock_create(opts.bits, ptr::null(), &mut out) },
        };
        ok(status)?;
        unsafe { Ok(Self::from(Handle::from_raw(out))) }
    }

    /// Perform a basic read of this clock. Wraps the [zx_clock_read] syscall. Requires
    /// `ZX_RIGHT_READ` and that the clock has had an initial time established.
    ///
    /// [zx_clock_read]: https://fuchsia.dev/fuchsia-src/reference/syscalls/clock_read
    pub fn read(&self) -> Result<Time, Status> {
        let mut now = 0;
        let status = unsafe { sys::zx_clock_read(self.raw_handle(), &mut now) };
        ok(status)?;
        Ok(Time::from_nanos(now))
    }

    /// Get low level details of this clock's current status. Wraps the
    /// [zx_clock_get_details] syscall. Requires `ZX_RIGHT_READ`.
    ///
    /// [zx_clock_get_details]: https://fuchsia.dev/fuchsia-src/reference/syscalls/clock_get_details
    pub fn get_details(&self) -> Result<ClockDetails, Status> {
        let mut out_details = MaybeUninit::<sys::zx_clock_details_v1_t>::uninit();
        let status = unsafe {
            sys::zx_clock_get_details(
                self.raw_handle(),
                sys::ZX_CLOCK_ARGS_VERSION_1,
                out_details.as_mut_ptr() as *mut u8,
            )
        };
        ok(status)?;
        let out_details = unsafe { out_details.assume_init() };
        Ok(out_details.into())
    }

    /// Make adjustments to this clock. Wraps the [zx_clock_update] syscall. Requires
    /// `ZX_RIGHT_WRITE`.
    ///
    /// [zx_clock_update]: https://fuchsia.dev/fuchsia-src/reference/syscalls/clock_update
    pub fn update(&self, update: ClockUpdate) -> Result<(), Status> {
        let opts = sys::ZX_CLOCK_ARGS_VERSION_1 | update.raw_opts();
        let args = sys::zx_clock_update_args_v1_t::from(update);
        let status = unsafe {
            sys::zx_clock_update(self.raw_handle(), opts, &args as *const _ as *const u8)
        };
        ok(status)?;
        Ok(())
    }
}

/// Specifies which properties of a clock to update. See [`Clock::update`]
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

    /// Returns a bitmask of options for use with [`sys::zx_clock_update`].
    fn raw_opts(&self) -> u64 {
        let mut opts = 0;
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
            padding1: [0, 0, 0, 0],
            value: cu.value.map(Time::into_nanos).unwrap_or(0),
            error_bound: cu.error_bounds.unwrap_or(0),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use matches::assert_matches;

    #[test]
    fn create_clocks() {
        assert_matches!(Clock::create(ClockOpts::empty(), None), Ok(_));
        assert_matches!(Clock::create(ClockOpts::MONOTONIC, None), Ok(_));
        assert_matches!(Clock::create(ClockOpts::CONTINUOUS, None), Ok(_));
        assert_matches!(Clock::create(ClockOpts::AUTO_START | ClockOpts::MONOTONIC, None), Ok(_));
        assert_matches!(Clock::create(ClockOpts::AUTO_START | ClockOpts::CONTINUOUS, None), Ok(_));

        // Now with backstop.
        let backstop = Some(Time::from_nanos(5500));
        assert_matches!(Clock::create(ClockOpts::MONOTONIC, backstop), Ok(_));
        assert_matches!(Clock::create(ClockOpts::CONTINUOUS, backstop), Ok(_));
        assert_matches!(
            Clock::create(ClockOpts::AUTO_START | ClockOpts::MONOTONIC, backstop),
            Ok(_)
        );
        assert_matches!(
            Clock::create(ClockOpts::AUTO_START | ClockOpts::CONTINUOUS, backstop),
            Ok(_)
        );
    }

    #[test]
    fn read_time() {
        let clock = Clock::create(ClockOpts::MONOTONIC, None).expect("failed to create clock");
        assert_matches!(clock.read(), Ok(_));
    }

    #[test]
    fn get_clock_details() {
        // No backstop.
        let clock = Clock::create(ClockOpts::MONOTONIC, None).expect("failed to create clock");
        let details = clock.get_details().expect("failed to get details");
        assert_eq!(details.backstop, Time::from_nanos(0));

        // With backstop.
        let clock = Clock::create(ClockOpts::MONOTONIC, Some(Time::from_nanos(5500)))
            .expect("failed to create clock");
        let details = clock.get_details().expect("failed to get details");
        assert_eq!(details.backstop, Time::from_nanos(5500));
    }

    #[test]
    fn update_clock() {
        let clock = Clock::create(ClockOpts::MONOTONIC, None).expect("failed to create clock");
        let before_details = clock.get_details().expect("failed to get details");
        assert_eq!(before_details.last_value_update_ticks, 0);
        assert_eq!(before_details.last_rate_adjust_update_ticks, 0);
        assert_eq!(before_details.last_error_bounds_update_ticks, 0);

        // Update all values.
        clock
            .update(ClockUpdate::new().value(Time::from_nanos(42)).rate_adjust(52).error_bounds(52))
            .expect("failed to update clock");
        let after_details = clock.get_details().expect("failed to get details");
        assert!(before_details.generation_counter < after_details.generation_counter);
        assert!(after_details.last_value_update_ticks > before_details.last_value_update_ticks);
        assert_eq!(
            after_details.last_value_update_ticks,
            after_details.last_rate_adjust_update_ticks
        );
        assert_eq!(
            after_details.last_value_update_ticks,
            after_details.last_error_bounds_update_ticks
        );
        assert_eq!(after_details.error_bounds, 52);
        assert_eq!(after_details.ticks_to_synthetic.synthetic_offset, 42);
        assert_eq!(after_details.mono_to_synthetic.synthetic_offset, 42);

        let before_details = after_details;

        // Update only one value.
        clock.update(ClockUpdate::new().error_bounds(100)).expect("failed to update clock");
        let after_details = clock.get_details().expect("failed to get details");
        assert!(before_details.generation_counter < after_details.generation_counter);
        assert!(
            after_details.last_error_bounds_update_ticks > before_details.last_value_update_ticks
        );
        assert!(
            after_details.last_error_bounds_update_ticks
                > after_details.last_rate_adjust_update_ticks
        );
        assert_eq!(
            after_details.last_rate_adjust_update_ticks,
            after_details.last_value_update_ticks
        );
        assert_eq!(after_details.error_bounds, 100);
        assert_eq!(after_details.ticks_to_synthetic.synthetic_offset, 42);
        assert_eq!(after_details.mono_to_synthetic.synthetic_offset, 42);
    }
}

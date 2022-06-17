// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Type-safe bindings for Zircon clock objects.

use crate::ok;
use crate::{AsHandleRef, ClockUpdate, Handle, HandleBased, HandleRef, Time};
use bitflags::bitflags;
use fuchsia_zircon_status::Status;
use fuchsia_zircon_sys as sys;
use std::{convert::TryInto, mem::MaybeUninit, ptr};

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
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ClockDetails {
    /// The minimum time the clock can ever be set to.
    pub backstop: Time,

    /// The current ticks to clock transformation.
    pub ticks_to_synthetic: ClockTransformation,

    /// The current clock monotonic to clock transformation.
    pub mono_to_synthetic: ClockTransformation,

    /// The current symmetric error estimate (if any) for the clock, measured in nanoseconds.
    pub error_bounds: u64,

    /// An observation of the system tick counter which was taken during the observation of the
    /// clock.
    pub query_ticks: sys::zx_ticks_t,

    /// The last time the clock's value was updated as defined by the clock monotonic reference
    /// timeline.
    pub last_value_update_ticks: sys::zx_ticks_t,

    /// The last time the clock's rate adjustment was updated as defined by the clock monotonic
    /// reference timeline.
    pub last_rate_adjust_update_ticks: sys::zx_ticks_t,

    /// The last time the clock's error bounds were updated as defined by the clock monotonic
    /// reference timeline.
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
#[derive(Debug, Clone, Eq, PartialEq)]
pub struct ClockTransformation {
    /// The offset on the reference timeline, measured in reference clock ticks.
    pub reference_offset: i64,
    /// The offset on the clock timeline, measured in clock ticks (typically normalized to
    /// nanoseconds).
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

/// Apply affine transformation to convert the reference time "r" to the synthetic time
/// "c". All values are widened to i128 before calculations and the end result is converted back to
/// a i64. If "c" is a larger number than would fit in an i64, the result saturates when cast to
/// i64.
fn transform_clock(r: i64, r_offset: i64, c_offset: i64, r_rate: u32, c_rate: u32) -> i64 {
    let r = r as i128;
    let r_offset = r_offset as i128;
    let c_offset = c_offset as i128;
    let r_rate = r_rate as i128;
    let c_rate = c_rate as i128;
    let c = (((r - r_offset) * c_rate) / r_rate) + c_offset;
    c.try_into().unwrap_or_else(|_| if c.is_positive() { i64::MAX } else { i64::MIN })
}

/// [Clock transformations](https://fuchsia.dev/fuchsia-src/concepts/kernel/clock_transformations)
/// can be applied to convert a time from a reference time to a synthetic time. The inverse
/// transformation can be applied to convert a synthetic time back to the reference time.
impl ClockTransformation {
    pub fn apply(&self, time: Time) -> Time {
        let c = transform_clock(
            time.into_nanos(),
            self.reference_offset,
            self.synthetic_offset,
            self.rate.reference_ticks,
            self.rate.synthetic_ticks,
        );

        Time::from_nanos(c)
    }

    pub fn apply_inverse(&self, time: Time) -> Time {
        let r = transform_clock(
            time.into_nanos(),
            self.synthetic_offset,
            self.reference_offset,
            self.rate.synthetic_ticks,
            self.rate.reference_ticks,
        );

        Time::from_nanos(r as i64)
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
    pub fn update(&self, update: impl Into<ClockUpdate>) -> Result<(), Status> {
        let update = update.into();
        let options = update.options();
        let args = sys::zx_clock_update_args_v2_t::from(update);
        let status = unsafe {
            sys::zx_clock_update(self.raw_handle(), options, &args as *const _ as *const u8)
        };
        ok(status)?;
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use assert_matches::assert_matches;

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

        // Update all properties.
        clock
            .update(
                ClockUpdate::builder()
                    .absolute_value(Time::from_nanos(999), Time::from_nanos(42))
                    .rate_adjust(52)
                    .error_bounds(52),
            )
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
        assert_eq!(after_details.mono_to_synthetic.reference_offset, 999);
        assert_eq!(after_details.mono_to_synthetic.synthetic_offset, 42);

        let before_details = after_details;

        // Update only one property.
        clock.update(ClockUpdate::builder().error_bounds(100)).expect("failed to update clock");
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

    #[test]
    fn clock_identity_transformation_roundtrip() {
        let t_0 = Time::ZERO;
        // Identity clock transformation
        let xform = ClockTransformation {
            reference_offset: 0,
            synthetic_offset: 0,
            rate: sys::zx_clock_rate_t { synthetic_ticks: 1, reference_ticks: 1 },
        };

        // Transformation roundtrip should be equivalent with the identity transformation.
        let transformed_time = xform.apply(t_0);
        let original_time = xform.apply_inverse(transformed_time);
        assert_eq!(t_0, original_time);
    }

    #[test]
    fn clock_transformation_roundtrip() {
        let t_0 = Time::ZERO;
        // Arbitrary clock transformation
        let xform = ClockTransformation {
            reference_offset: 196980085208,
            synthetic_offset: 1616900096031887801,
            rate: sys::zx_clock_rate_t { synthetic_ticks: 999980, reference_ticks: 1000000 },
        };

        // Transformation roundtrip should be equivalent modulo rounding error.
        let transformed_time = xform.apply(t_0);
        let original_time = xform.apply_inverse(transformed_time);
        let roundtrip_diff = t_0 - original_time;
        assert!(roundtrip_diff.into_nanos().abs() <= 1);
    }

    #[test]
    fn clock_trailing_transformation_roundtrip() {
        let t_0 = Time::ZERO;
        // Arbitrary clock transformation where the synthetic clock is trailing behind the
        // reference clock.
        let xform = ClockTransformation {
            reference_offset: 1616900096031887801,
            synthetic_offset: 196980085208,
            rate: sys::zx_clock_rate_t { synthetic_ticks: 1000000, reference_ticks: 999980 },
        };

        // Transformation roundtrip should be equivalent modulo rounding error.
        let transformed_time = xform.apply(t_0);
        let original_time = xform.apply_inverse(transformed_time);
        let roundtrip_diff = t_0 - original_time;
        assert!(roundtrip_diff.into_nanos().abs() <= 1);
    }

    #[test]
    fn clock_saturating_transformations() {
        let t_0 = Time::from_nanos(i64::MAX);
        // Clock transformation which will positively overflow t_0
        let xform = ClockTransformation {
            reference_offset: 0,
            synthetic_offset: 1,
            rate: sys::zx_clock_rate_t { synthetic_ticks: 1, reference_ticks: 1 },
        };

        // Applying the transformation will lead to saturation
        let time = xform.apply(t_0).into_nanos();
        assert_eq!(time, i64::MAX);

        let t_0 = Time::from_nanos(i64::MIN);
        // Clock transformation which will negatively overflow t_0
        let xform = ClockTransformation {
            reference_offset: 1,
            synthetic_offset: 0,
            rate: sys::zx_clock_rate_t { synthetic_ticks: 1, reference_ticks: 1 },
        };

        // Applying the transformation will lead to saturation
        let time = xform.apply(t_0).into_nanos();
        assert_eq!(time, i64::MIN);
    }
}

// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The `omaha_client::time` module provides a set of types and traits to allow for the expressing
//! of time using both a wall-time clock, and a monotonic clock.
//!
//! The motivation for this is that wall-time is subject to being artibtrarily moved (forwards or
//! backwards as the system's timekeeper decides is necessary), to keep it in sync with external
//! systems.
//!
//! The monotonic clock, on the other hand, provides a consistently moving forward notion of time,
//! but one that is not anchored at any particular external point in time.  It's epoch is therefore
//! somewhat opaque by nature.  Rust's std::time::Instant takes this to the extreme of making the
//! underlying value completely hidden from callers.
//!
//! One aspect of the `TimeSource` trait and the `ComplexTime` type is that they provide a way to
//! pair the two timelines, and construct a time that is given in terms of each of these dimensions.
//!
//! What it doesn't try to do, is so that these pairings are the _same_ time.  They can be
//! observations made concurrently (as in the case of `TimeSource::now() -> ComplexTime`),
//! or they can be bounds for when an event in the future can happen:
//!
//! ```
//! let past_event_wall_time: SystemTime = read_some_system_time();
//! let duration_to_next = Duration::from_secs(1*60*60); // one hour
//! let rough_next_event_time = ComplexTime{
//!                               wall: past_event_wall_time + duration_to_next,
//!                               mono: TimeSource::now_in_monotonic() + duration_to_next
//!                             };
//! timer.wait_until_either(rough_next_event_time).await;
//! ```
//!
//! The above setups up a `ComplexTime` as a bound that based on an expected wall time, and
//! monotonic time relative to `now`, such that the event can be described as "at time X, or within
//! an hour" when used with `Timer::wait_until`, or "after time X, at least an hour from now", if
//! used with `Timer::wait_until_all`.
//!
//! # Usage Guidelines
//!
//! The `ComplexTime` and `PartialComplexTime` structs give the ability to represent a number of
//! states of knowledge about a time.
//!
//! When modeling the known time for something:
//!
//! * `ComplexTime` - When both wall and monotonic times are always known
//! * `PartialComplexTime` - When some time (wall, monotonic, or both) is always known
//! * `Option<ComplexTime> - When time is either known for both timelines, or not at all.
//! * `Option<PartialComplexTime> - Situations where time can be in any of 4 states:
//!   * None whatsoever
//!   * Only wall time
//!   * Only monotonic time
//!   * Both are known
//!
//! When modeling the time required (e.g. timer waits):
//! * `ComplexTime` - When both wall and monotonic times are always required
//! * `PartialComplexTime` - When some time (wall, monotonic, or both) is always required, but any
//! or both will suffice.
//!

use chrono::{DateTime, Utc};
use futures::future::BoxFuture;
use std::{
    fmt::{Debug, Display},
    hash::Hash,
    time::{Duration, Instant, SystemTime},
};

// NOTE:  Implementations for the main types of this module have been moved to inner modules that
//        that are exposed via `pub use`, so that it's easier to read through the declarations of
//        the main types and see how they are meant to be used together.  Implementations are in the
//        same order as the declarations of the types.

// Implementations and tests for `ComplexTime` and `PartialComplexTime`
mod complex;
pub use complex::system_time_conversion;

/// This is a complete `ComplexTime`, which has values on both the wall clock timeline and the
/// monotonic clock timeline.
///
/// It is not necessarily intended that both values refer to the same moment.  They can, or they
/// can refer to a time on each clock's respective timeline, e.g. for use with the `Timer` trait.
///
/// The `ComplexTime` type implements all the standard math operations in `std::ops` that are
/// implemented for both `std::time::SystemTime` and `std::time::Instant`.  Like those
/// implementations, it will panic on overflow.
#[derive(Copy, Clone, Debug, PartialEq, Eq, Hash)]
pub struct ComplexTime {
    pub wall: SystemTime,
    pub mono: Instant,
}
impl ComplexTime {
    /// Truncate the submicrosecond part of the walltime.
    pub fn truncate_submicrosecond_walltime(&self) -> ComplexTime {
        let nanos_in_one_micro = Duration::from_micros(1).as_nanos();
        let submicrosecond_nanos = match self.wall_duration_since(SystemTime::UNIX_EPOCH) {
            Ok(duration) => duration.as_nanos() % nanos_in_one_micro,
            Err(e) => nanos_in_one_micro - e.duration().as_nanos() % nanos_in_one_micro,
        };
        ComplexTime {
            wall: self.wall - Duration::from_nanos(submicrosecond_nanos as u64),
            mono: self.mono,
        }
    }
    /// Compute the Duration since the given SystemTime, for the SystemTime component of this
    /// ComplexTime.  If this ComplexTime's SystemTime is before the given time, the Duration
    /// is returned as Err (same as `SystemTime::duration_since()`)
    pub fn wall_duration_since(
        &self,
        earlier: impl Into<SystemTime>,
    ) -> Result<Duration, std::time::SystemTimeError> {
        self.wall.duration_since(earlier.into())
    }

    /// Returns `true` if this ComplexTime is after either the SystemTime or the Instant of the
    /// given PartialComplexTime.
    pub fn is_after_or_eq_any(&self, other: impl Into<PartialComplexTime>) -> bool {
        match other.into() {
            PartialComplexTime::Wall(w) => (self.wall >= w),
            PartialComplexTime::Monotonic(m) => (self.mono >= m),
            PartialComplexTime::Complex(c) => (self.wall >= c.wall || self.mono >= c.mono),
        }
    }
}

// Implementations for `Display`, `Add`, `AddAssign`, `Sub`, `SubAssign` are found in
// `mod complex::complex_time_impls`
//
// Implementations for `From<>` are found in `mod complex::complex_time_type_conversions`

/// `PartialComplexTime` provides a `std::interator::EitherOrBoth`-like type which is specifically
/// for holding either one, or both, of the time types that make up a `ComplexTime`.  It's a type
/// that holds a value for at least one of the timelines.
///
/// The important differentiation of this vs. a struct such as:
/// ```
/// struct MaybeBoth {
///   wall: Option<SystemTime>,
///   monotonic: Option<SystemTime>
///}
/// ```
/// is that there is no valid `(None, None)` state for this type to be in, and so code that uses can
/// be certain that _some_ time is specified.
///
/// Like `ComplexTime`, `PartialComplexTime` implements all the standard math operations in
/// `std::ops` that are implemented for both `std::time::SystemTime` and  `std::time:Instant`.  Like
/// those implementations, they will panic on overflow.
#[derive(Copy, Clone, Debug, PartialEq, Eq, Hash)]
pub enum PartialComplexTime {
    /// Just a wall time.
    Wall(SystemTime),
    /// Just a monotonic time.
    Monotonic(Instant),
    /// Both a wall and a monotonic time.
    Complex(ComplexTime),
}
impl PartialComplexTime {
    /// Return the SystemTime component, if one exists.
    pub fn checked_to_system_time(self) -> Option<SystemTime> {
        self.destructure().0
    }

    /// Return the Instant component, if one exists.
    pub fn checked_to_instant(self) -> Option<Instant> {
        self.destructure().1
    }

    /// Convert the SystemTime component of this PartialComplexTime into i64 microseconds from
    /// the UNIX Epoch.  Provides coverage over +/- approx 30,000 years from 1970-01-01 UTC.
    ///
    /// Returns None if it doesn't have a wall time or on overflow (instead of panicking)
    pub fn checked_to_micros_since_epoch(self) -> Option<i64> {
        self.checked_to_system_time()
            .and_then(system_time_conversion::checked_system_time_to_micros_from_epoch)
    }

    /// Return the PartialComplexTime::Wall that represents the same time as the specified
    /// microseconds from the UNIX Epoch (1970-01-01 UTC)
    pub fn from_micros_since_epoch(micros: i64) -> Self {
        PartialComplexTime::from(system_time_conversion::micros_from_epoch_to_system_time(micros))
    }

    /// Return a new ComplexTime that's based on the time values of this PartialComplexTime,
    /// setting either unknown field from the passed-in ComplexTime.
    pub fn complete_with(&self, complex: ComplexTime) -> ComplexTime {
        let (system, instant) = self.destructure();
        ComplexTime::from((system.unwrap_or(complex.wall), instant.unwrap_or(complex.mono)))
    }

    /// Destructure the PartialComplexTime into it's two components, each as an Option.
    pub fn destructure(&self) -> (Option<SystemTime>, Option<Instant>) {
        match *self {
            PartialComplexTime::Wall(w) => (Some(w), None),
            PartialComplexTime::Monotonic(m) => (None, Some(m)),
            PartialComplexTime::Complex(c) => (Some(c.wall), Some(c.mono)),
        }
    }
}

// Implementations for `Display`, `Add`, `AddAssign`, `Sub`, `SubAssign` are found in
// `mod complex::partial_complex_time_impls`
//
// Implementations for `From<>` are found in `mod complex::partial_complex_time_type_conversions`

/// Trait for timers that understand how to work with the `ComplexTime` and `PartialComplexTime`
/// types.
///
/// When using a `PartialComplexTime`, the trait defines Fns for waiting until any of the times have
/// been reached, or until all of them have been reached.
pub trait Timer {
    /// Wait until at least one of the given time bounds has been reached.
    fn wait_until(&mut self, time: impl Into<PartialComplexTime>) -> BoxFuture<'static, ()>;

    /// Wait for the given duration (from now).
    fn wait_for(&mut self, duration: Duration) -> BoxFuture<'static, ()>;
}
// Implementations and tests for test `Timers`.  Not marked as #[cfg(test)] to allow use in tests
// in crates that use this.
pub mod timers;

/// `TimeSource` is a trait for providing access to both the "System" (aka "Wall") time for
/// platform, as well as its monotonic time.
pub trait TimeSource {
    /// Returns the current wall time.
    fn now_in_walltime(&self) -> SystemTime;

    /// Returns the current montonic time.
    fn now_in_monotonic(&self) -> Instant;

    /// Returns the current ComplexTime (both wall and monotonic times).
    fn now(&self) -> ComplexTime;
}

// Implementations and tests for `TimeSource`
pub mod time_source;
pub use time_source::MockTimeSource;
pub use time_source::StandardTimeSource;

impl<T> TimeSource for &T
where
    T: TimeSource,
{
    fn now_in_walltime(&self) -> SystemTime {
        (*self).now_in_walltime()
    }
    fn now_in_monotonic(&self) -> Instant {
        (*self).now_in_monotonic()
    }
    fn now(&self) -> ComplexTime {
        (*self).now()
    }
}

/// Helper struct for providing a consistent, readable `SystemTime`.
///
/// This displays a `SystemTime` in a human-readable date+time in UTC plus the raw `[seconds].[ns]`
/// since epoch of the SystemTime.
///
/// # Example
/// ```
/// let sys_time = SystemTime::UNIX_EPOCH + Duration::from_nanos(994610096026420000);
///
/// assert_eq!(
///     format!("{}", ReadableSystemTime(sys_time)),
///     "2001-07-08 16:34:56.026 UTC (994610096.026420000)"
/// );
/// ```
pub struct ReadableSystemTime(pub SystemTime);
impl Display for ReadableSystemTime {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let format = DateTime::<Utc>::from(self.0).format("%Y-%m-%d %H:%M:%S%.3f %Z (%s%.9f)");
        Display::fmt(&format, f)
    }
}
impl Debug for ReadableSystemTime {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        Display::fmt(self, f)
    }
}

#[cfg(test)]
mod tests_for_readable_system_time {
    use super::*;

    #[test]
    fn test_readable_system_time() {
        let sys_time = SystemTime::UNIX_EPOCH + Duration::from_nanos(994610096026420000);

        assert_eq!(
            format!("{}", ReadableSystemTime(sys_time)),
            "2001-07-08 16:34:56.026 UTC (994610096.026420000)"
        );
    }
}

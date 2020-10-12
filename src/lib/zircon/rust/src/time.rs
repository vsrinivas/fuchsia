// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Type-safe bindings for Zircon timer objects.

use crate::ok;
use crate::{AsHandleRef, ClockId, Handle, HandleBased, HandleRef, Status};
use fuchsia_zircon_sys as sys;
use std::ops;
use std::time as stdtime;

#[derive(Debug, Copy, Clone, Eq, PartialEq, Ord, PartialOrd, Hash)]
#[repr(transparent)]
pub struct Duration(sys::zx_duration_t);

#[derive(Debug, Copy, Clone, Eq, PartialEq, Ord, PartialOrd, Hash)]
#[repr(transparent)]
pub struct Time(sys::zx_time_t);

impl From<stdtime::Duration> for Duration {
    fn from(dur: stdtime::Duration) -> Self {
        Duration::from_seconds(dur.as_secs() as i64)
            + Duration::from_nanos(dur.subsec_nanos() as i64)
    }
}

impl ops::Add<Duration> for Time {
    type Output = Time;
    fn add(self, dur: Duration) -> Time {
        Time::from_nanos(dur.into_nanos() + self.into_nanos())
    }
}

impl ops::Add<Time> for Duration {
    type Output = Time;
    fn add(self, time: Time) -> Time {
        Time::from_nanos(self.into_nanos() + time.into_nanos())
    }
}

impl ops::Add for Duration {
    type Output = Duration;
    fn add(self, dur: Duration) -> Duration {
        Duration::from_nanos(self.into_nanos() + dur.into_nanos())
    }
}

impl ops::Sub for Duration {
    type Output = Duration;
    fn sub(self, dur: Duration) -> Duration {
        Duration::from_nanos(self.into_nanos() - dur.into_nanos())
    }
}

impl ops::Sub<Duration> for Time {
    type Output = Time;
    fn sub(self, dur: Duration) -> Time {
        Time::from_nanos(self.into_nanos() - dur.into_nanos())
    }
}

impl ops::Sub<Time> for Time {
    type Output = Duration;
    fn sub(self, other: Time) -> Duration {
        Duration::from_nanos(self.into_nanos() - other.into_nanos())
    }
}

impl ops::AddAssign for Duration {
    fn add_assign(&mut self, dur: Duration) {
        self.0 += dur.into_nanos()
    }
}

impl ops::SubAssign for Duration {
    fn sub_assign(&mut self, dur: Duration) {
        self.0 -= dur.into_nanos()
    }
}

impl ops::AddAssign<Duration> for Time {
    fn add_assign(&mut self, dur: Duration) {
        self.0 += dur.into_nanos()
    }
}

impl ops::SubAssign<Duration> for Time {
    fn sub_assign(&mut self, dur: Duration) {
        self.0 -= dur.into_nanos()
    }
}

impl<T> ops::Mul<T> for Duration
where
    T: Into<i64>,
{
    type Output = Self;
    fn mul(self, mul: T) -> Self {
        Duration::from_nanos(self.0 * mul.into())
    }
}

impl<T> ops::Div<T> for Duration
where
    T: Into<i64>,
{
    type Output = Self;
    fn div(self, div: T) -> Self {
        Duration::from_nanos(self.0 / div.into())
    }
}

impl Duration {
    /// Sleep for the given amount of time.
    pub fn sleep(self) {
        Time::after(self).sleep()
    }

    #[deprecated(note = "Users should instead use into_nanos")]
    pub fn nanos(self) -> i64 {
        self.0
    }

    #[deprecated(note = "Users should instead use into_micros")]
    pub fn micros(self) -> i64 {
        self.0 / 1_000
    }

    #[deprecated(note = "Users should instead use into_millis")]
    pub fn millis(self) -> i64 {
        self.into_micros() / 1_000
    }

    #[deprecated(note = "Users should instead use into_seconds")]
    pub fn seconds(self) -> i64 {
        self.into_millis() / 1_000
    }

    #[deprecated(note = "Users should instead use into_minutes")]
    pub fn minutes(self) -> i64 {
        self.into_seconds() / 60
    }

    #[deprecated(note = "Users should instead use into_hours")]
    pub fn hours(self) -> i64 {
        self.into_minutes() / 60
    }

    /// Returns the number of nanoseconds contained by this `Duration`.
    pub const fn into_nanos(self) -> i64 {
        self.0
    }

    /// Returns the total number of whole microseconds contained by this `Duration`.
    pub const fn into_micros(self) -> i64 {
        self.0 / 1_000
    }

    /// Returns the total number of whole milliseconds contained by this `Duration`.
    pub const fn into_millis(self) -> i64 {
        self.into_micros() / 1_000
    }

    /// Returns the total number of whole seconds contained by this `Duration`.
    pub const fn into_seconds(self) -> i64 {
        self.into_millis() / 1_000
    }

    /// Returns the total number of whole minutes contained by this `Duration`.
    pub const fn into_minutes(self) -> i64 {
        self.into_seconds() / 60
    }

    /// Returns the total number of whole hours contained by this `Duration`.
    pub const fn into_hours(self) -> i64 {
        self.into_minutes() / 60
    }

    pub const fn from_nanos(nanos: i64) -> Self {
        Duration(nanos)
    }

    pub const fn from_micros(micros: i64) -> Self {
        Duration(micros * 1_000)
    }

    pub const fn from_millis(millis: i64) -> Self {
        Duration::from_micros(millis * 1_000)
    }

    pub const fn from_seconds(secs: i64) -> Self {
        Duration::from_millis(secs * 1_000)
    }

    pub const fn from_minutes(min: i64) -> Self {
        Duration::from_seconds(min * 60)
    }

    pub const fn from_hours(hours: i64) -> Self {
        Duration::from_minutes(hours * 60)
    }
}

pub trait DurationNum: Sized {
    fn nanos(self) -> Duration;
    fn micros(self) -> Duration;
    fn millis(self) -> Duration;
    fn seconds(self) -> Duration;
    fn minutes(self) -> Duration;
    fn hours(self) -> Duration;

    // Singular versions to allow for `1.milli()` and `1.second()`, etc.
    fn micro(self) -> Duration {
        self.micros()
    }
    fn milli(self) -> Duration {
        self.millis()
    }
    fn second(self) -> Duration {
        self.seconds()
    }
    fn minute(self) -> Duration {
        self.minutes()
    }
    fn hour(self) -> Duration {
        self.hours()
    }
}

// Note: this could be implemented for other unsized integer types, but it doesn't seem
// necessary to support the usual case.
impl DurationNum for i64 {
    fn nanos(self) -> Duration {
        Duration::from_nanos(self)
    }

    fn micros(self) -> Duration {
        Duration::from_micros(self)
    }

    fn millis(self) -> Duration {
        Duration::from_millis(self)
    }

    fn seconds(self) -> Duration {
        Duration::from_seconds(self)
    }

    fn minutes(self) -> Duration {
        Duration::from_minutes(self)
    }

    fn hours(self) -> Duration {
        Duration::from_hours(self)
    }
}

impl Time {
    pub const INFINITE: Time = Time(sys::ZX_TIME_INFINITE);
    pub const INFINITE_PAST: Time = Time(sys::ZX_TIME_INFINITE_PAST);

    /// Get the current time, from the specific clock id.
    ///
    /// Wraps the
    /// [zx_clock_get](https://fuchsia.dev/fuchsia-src/reference/syscalls/clock_get.md)
    /// syscall.
    pub fn get(clock_id: ClockId) -> Time {
        unsafe {
            let mut now: sys::zx_time_t = 0;
            sys::zx_clock_get(clock_id as u32, &mut now);
            Time(now)
        }
    }

    /// Get the current monotonic time.
    ///
    /// Wraps the
    /// [zx_clock_get_monotonic](https://fuchsia.dev/fuchsia-src/reference/syscalls/clock_get_monotonic.md)
    /// syscall.
    pub fn get_monotonic() -> Time {
        unsafe { Time(sys::zx_clock_get_monotonic()) }
    }

    /// Compute a deadline for the time in the future that is the given `Duration` away.
    ///
    /// Wraps the
    /// [zx_deadline_after](https://fuchsia.dev/fuchsia-src/reference/syscalls/deadline_after.md)
    /// syscall.
    pub fn after(duration: Duration) -> Time {
        unsafe { Time(sys::zx_deadline_after(duration.0)) }
    }

    /// Sleep until the given time.
    ///
    /// Wraps the
    /// [zx_nanosleep](https://fuchsia.dev/fuchsia-src/reference/syscalls/nanosleep.md)
    /// syscall.
    pub fn sleep(self) {
        unsafe {
            sys::zx_nanosleep(self.0);
        }
    }

    pub fn into_nanos(self) -> i64 {
        self.0
    }

    pub fn from_nanos(nanos: i64) -> Self {
        Time(nanos)
    }
}

/// Read the number of high-precision timer ticks since boot. These ticks may be processor cycles,
/// high speed timer, profiling timer, etc. They are not guaranteed to continue advancing when the
/// system is asleep.
///
/// Wraps the
/// [zx_ticks_get](https://fuchsia.dev/fuchsia-src/reference/syscalls/ticks_get.md)
/// syscall.
pub fn ticks_get() -> i64 {
    unsafe { sys::zx_ticks_get() }
}

/// Return the number of high-precision timer ticks in a second.
///
/// Wraps the
/// [zx_ticks_per_second](https://fuchsia.dev/fuchsia-src/reference/syscalls/ticks_per_second.md)
/// syscall.
pub fn ticks_per_second() -> i64 {
    unsafe { sys::zx_ticks_per_second() }
}

/// An object representing a Zircon timer, such as the one returned by
/// [zx_timer_create](https://fuchsia.dev/fuchsia-src/reference/syscalls/timer_create.md).
///
/// As essentially a subtype of `Handle`, it can be freely interconverted.
#[derive(Debug, Eq, PartialEq, Ord, PartialOrd, Hash)]
#[repr(transparent)]
pub struct Timer(Handle);
impl_handle_based!(Timer);

impl Timer {
    /// Create a timer, an object that can signal when a specified point in time has been reached.
    /// Wraps the
    /// [zx_timer_create](https://fuchsia.dev/fuchsia-src/reference/syscalls/timer_create.md)
    /// syscall.
    pub fn create(clock_id: ClockId) -> Result<Timer, Status> {
        let mut out = 0;
        let opts = 0;
        let status = unsafe { sys::zx_timer_create(opts, clock_id as u32, &mut out) };
        ok(status)?;
        unsafe { Ok(Self::from(Handle::from_raw(out))) }
    }

    /// Start a one-shot timer that will fire when `deadline` passes. Wraps the
    /// [zx_timer_set](https://fuchsia.dev/fuchsia-src/reference/syscalls/timer_set.md)
    /// syscall.
    pub fn set(&self, deadline: Time, slack: Duration) -> Result<(), Status> {
        let status = unsafe {
            sys::zx_timer_set(self.raw_handle(), deadline.into_nanos(), slack.into_nanos())
        };
        ok(status)
    }

    /// Cancels a pending timer that was started with start(). Wraps the
    /// [zx_timer_cancel](https://fuchsia.dev/fuchsia-src/reference/syscalls/timer_cancel.md)
    /// syscall.
    pub fn cancel(&self) -> Result<(), Status> {
        let status = unsafe { sys::zx_timer_cancel(self.raw_handle()) };
        ok(status)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::Signals;

    #[test]
    fn create_timer_invalid_clock() {
        assert_eq!(Timer::create(ClockId::UTC).unwrap_err(), Status::INVALID_ARGS);
        assert_eq!(Timer::create(ClockId::Thread), Err(Status::INVALID_ARGS));
    }

    #[test]
    fn from_std() {
        let std_dur = stdtime::Duration::new(25, 25);
        let dur = Duration::from(std_dur);
        let std_dur_nanos = (1_000_000_000 * std_dur.as_secs()) + std_dur.subsec_nanos() as u64;
        assert_eq!(std_dur_nanos as i64, dur.into_nanos());
    }

    #[test]
    fn i64_conversions() {
        let nanos_in_one_hour = 3_600_000_000_000;
        let dur_from_nanos = Duration::from_nanos(nanos_in_one_hour);
        let dur_from_hours = Duration::from_hours(1);
        assert_eq!(dur_from_nanos, dur_from_hours);
        assert_eq!(dur_from_nanos.into_nanos(), dur_from_hours.into_nanos());
        assert_eq!(dur_from_nanos.into_nanos(), nanos_in_one_hour);
        assert_eq!(dur_from_nanos.into_hours(), 1);
    }

    #[test]
    fn timer_basic() {
        let slack = 0.millis();
        let ten_ms = 10.millis();
        let five_secs = 5.seconds();
        let six_secs = 6.seconds();

        // Create a timer
        let timer = Timer::create(ClockId::Monotonic).unwrap();

        // Should not signal yet.
        assert_eq!(
            timer.wait_handle(Signals::TIMER_SIGNALED, Time::after(ten_ms)),
            Err(Status::TIMED_OUT)
        );

        // Set it, and soon it should signal.
        assert_eq!(timer.set(Time::after(five_secs), slack), Ok(()));
        assert_eq!(
            timer.wait_handle(Signals::TIMER_SIGNALED, Time::after(six_secs)),
            Ok(Signals::TIMER_SIGNALED)
        );

        // Cancel it, and it should stop signalling.
        assert_eq!(timer.cancel(), Ok(()));
        assert_eq!(
            timer.wait_handle(Signals::TIMER_SIGNALED, Time::after(ten_ms)),
            Err(Status::TIMED_OUT)
        );
    }

    #[test]
    fn time_minus_time() {
        let lhs = Time::from_nanos(10);
        let rhs = Time::from_nanos(30);
        assert_eq!(lhs - rhs, Duration::from_nanos(-20));
    }
}

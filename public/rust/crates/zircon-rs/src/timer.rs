// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Type-safe bindings for Magenta timer objects.

use {ClockId, Duration, HandleBase, Handle, HandleRef, Status, Time};
use {sys, into_result};

/// An object representing a Magenta
/// [event pair](https://fuchsia.googlesource.com/magenta/+/master/docs/concepts.md#Other-IPC_Events_Event-Pairs_and-User-Signals).
///
/// As essentially a subtype of `Handle`, it can be freely interconverted.
#[derive(Debug, Eq, PartialEq)]
pub struct Timer(Handle);

impl HandleBase for Timer {
    fn get_ref(&self) -> HandleRef {
        self.0.get_ref()
    }

    fn from_handle(handle: Handle) -> Self {
        Timer(handle)
    }
}

impl Timer {
    /// Create a timer, an object that can signal when a specified point in time has been reached.
    /// Wraps the
    /// [mx_timer_create](https://fuchsia.googlesource.com/magenta/+/master/docs/syscalls/timer_create.md)
    /// syscall.
    pub fn create(options: TimerOpts, clock_id: ClockId) -> Result<Timer, Status> {
        let mut out = 0;
        let status = unsafe { sys::mx_timer_create(options as u32, clock_id as u32, &mut out) };
        into_result(status, || Self::from_handle(Handle(out)))
    }

    /// Starts a timer which will fire when `deadline` passes. Wraps the
    /// [mx_timer_start](https://fuchsia.googlesource.com/magenta/+/master/docs/syscalls/timer_start.md)
    /// syscall.
    pub fn start(&self, deadline: Time, period: Duration, slack: Duration) -> Result<(), Status> {
        let status = unsafe { sys::mx_timer_start(self.raw_handle(), deadline, period, slack) };
        into_result(status, || ())
    }

    /// Cancels a pending timer that was started with start(). Wraps the
    /// [mx_timer_cancel](https://fuchsia.googlesource.com/magenta/+/master/docs/syscalls/timer_cancel.md)
    /// syscall.
    pub fn cancel(&self) -> Result<(), Status> {
        let status = unsafe { sys::mx_timer_cancel(self.raw_handle()) };
        into_result(status, || ())
    }
}

/// Options for creating a timer.
#[repr(u32)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum TimerOpts {
    /// Default options.
    Default = 0,
}

impl Default for TimerOpts {
    fn default() -> Self {
        TimerOpts::Default
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use {Duration, MX_SIGNAL_LAST_HANDLE, MX_TIMER_SIGNALED};
    use deadline_after;

    #[test]
    fn create_timer_invalid_clock() {
        assert_eq!(Timer::create(TimerOpts::Default, ClockId::UTC).unwrap_err(), Status::ErrInvalidArgs);
        assert_eq!(Timer::create(TimerOpts::Default, ClockId::Thread), Err(Status::ErrInvalidArgs));
    }

    #[test]
    fn timer_basic() {
        let ten_ms: Duration = 10_000_000;
        let twenty_ms: Duration = 20_000_000;

        // Create a timer
        let timer = Timer::create(TimerOpts::Default, ClockId::Monotonic).unwrap();

        // Should not signal yet.
        assert_eq!(timer.wait(MX_TIMER_SIGNALED, deadline_after(ten_ms)), Err(Status::ErrTimedOut));

        // Start it, and soon it should signal.
        assert_eq!(timer.start(ten_ms, 0, 0), Ok(()));
        assert_eq!(timer.wait(MX_TIMER_SIGNALED, deadline_after(twenty_ms)).unwrap(),
            MX_TIMER_SIGNALED | MX_SIGNAL_LAST_HANDLE);

        // Cancel it, and it should stop signalling.
        assert_eq!(timer.cancel(), Ok(()));
        assert_eq!(timer.wait(MX_TIMER_SIGNALED, deadline_after(ten_ms)), Err(Status::ErrTimedOut));
    }
}

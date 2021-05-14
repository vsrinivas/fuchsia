// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_async::{self as fasync, TimeoutExt};
use fuchsia_zircon as zx;
use fuchsia_zircon::sys::zx_time_t;
use std::future::Future;
use std::os::raw::c_char;

/// A version of the fidl `DeadlineId` containing unowned data.
#[derive(Clone, Copy)]
pub struct DeadlineId<'a> {
    component_id: &'a str,
    code: &'a str,
}

impl<'a> Into<fidl_fuchsia_testing_deadline::DeadlineId> for DeadlineId<'a> {
    fn into(self) -> fidl_fuchsia_testing_deadline::DeadlineId {
        fidl_fuchsia_testing_deadline::DeadlineId {
            component_id: self.component_id.to_string(),
            code: self.code.to_string(),
        }
    }
}

impl<'a> DeadlineId<'a> {
    /// Create a new deadline identifier.
    pub const fn new(component_id: &'a str, code: &'a str) -> Self {
        Self { component_id, code }
    }
}

extern "C" {
    fn create_named_deadline(
        component: *const c_char,
        component_len: usize,
        code: *const c_char,
        code_len: usize,
        duration: zx_time_t,
        out: *mut zx_time_t,
    ) -> bool;
}

fn create_named_deadline_rust(deadline: &DeadlineId<'_>, duration: zx::Duration) -> fasync::Time {
    let mut time: zx_time_t = 0;
    let time_valid = unsafe {
        create_named_deadline(
            deadline.component_id.as_ptr() as *const c_char,
            deadline.component_id.len(),
            deadline.code.as_ptr() as *const c_char,
            deadline.code.len(),
            duration.into_nanos(),
            &mut time,
        )
    };
    match time_valid {
        true => zx::Time::from_nanos(time).into(),
        false => fasync::Time::now() + duration,
    }
}

/// A timer with an associated name.
/// This timer is intended to be used in conjunction with the fake-clock library. Under normal
/// execution, the timer behaves the same as a regular [`fuchsia_async::Timer`]. When run in an
/// integration test with the fake-clock library linked in, the creation of the timer and
/// the expiration of the timer are reported to the fake-clock service. The integration test may
/// register interest in these events to stop time when they occur.
pub struct NamedTimer;

impl NamedTimer {
    /// Create a new `NamedTimer` that will expire `duration` in the future.
    /// In an integration test, the `SET` event is reported immediately when this method is called,
    /// and `EXPIRED` is reported after `duration` elapses. Note `EXPIRED` is still reported even
    /// if the timer is dropped before `duration` elapses.
    pub fn new(id: &DeadlineId<'_>, duration: zx::Duration) -> fasync::Timer {
        let deadline = create_named_deadline_rust(id, duration);
        fasync::Timer::new(deadline)
    }
}

/// An extension trait that allows setting a timeout with an associated name.
/// The timeout is intended to be used in conjunction with the fake-clock library. Under normal
/// execution, this behaves identically to [`fuchsia_async::TimeoutExt`].
/// When run in an integration test with the fake-clock library linked in, the creation of the
/// timer and the expiration of the timer are reported to the fake-clock service. The integration
/// test may register interest in these events to stop time when they occur.
pub trait NamedTimeoutExt: Future + Sized {
    /// Wraps the future in a timeout, calling `on_timeout` when the timeout occurs.
    /// In an integration test, the `SET` event is reported immediately when this method is called,
    /// and `EXPIRED` is reported after `duration` elapses. Note `EXPIRED` is still reported even
    /// if `on_timeout` is not run.
    fn on_timeout_named<OT>(
        self,
        id: &DeadlineId<'_>,
        duration: zx::Duration,
        on_timeout: OT,
    ) -> fasync::OnTimeout<Self, OT>
    where
        OT: FnOnce() -> Self::Output,
    {
        let deadline = create_named_deadline_rust(id, duration);
        self.on_timeout(deadline, on_timeout)
    }
}

impl<F: Future + Sized> NamedTimeoutExt for F {}

#[cfg(test)]
mod test {
    use super::*;
    use core::task::Poll;
    // When the fake-clock library is not linked in, these timers should behave identical to
    // fasync::Timer. These tests verify that the fake time utilities provided by
    // fasync::TestExecutor continue to work when fake-clock is NOT linked in. Behavior with
    // fake-clock linked in is verified by integration tests in fake-clock/examples.

    const ONE_HOUR: zx::Duration = zx::Duration::from_hours(1);
    const DEADLINE_ID: DeadlineId<'static> = DeadlineId::new("component", "code");

    #[test]
    fn test_timer() {
        let mut executor =
            fasync::TestExecutor::new_with_fake_time().expect("creating executor failed");
        let start_time = executor.now();
        let mut timer = NamedTimer::new(&DEADLINE_ID, ONE_HOUR);
        assert!(executor.run_until_stalled(&mut timer).is_pending());

        executor.set_fake_time(start_time + ONE_HOUR);
        assert_eq!(executor.wake_next_timer(), Some(start_time + ONE_HOUR));
        assert!(executor.run_until_stalled(&mut timer).is_ready());
    }

    #[test]
    fn test_timeout_not_invoked() {
        let mut executor =
            fasync::TestExecutor::new_with_fake_time().expect("creating executor failed");

        let mut ready_future =
            futures::future::ready("ready").on_timeout_named(&DEADLINE_ID, ONE_HOUR, || "timeout");
        assert_eq!(executor.run_until_stalled(&mut ready_future), Poll::Ready("ready"));
    }

    #[test]
    fn test_timeout_invoked() {
        let mut executor =
            fasync::TestExecutor::new_with_fake_time().expect("creating executor failed");

        let start_time = executor.now();
        let mut stalled_future =
            futures::future::pending().on_timeout_named(&DEADLINE_ID, ONE_HOUR, || "timeout");
        assert!(executor.run_until_stalled(&mut stalled_future).is_pending());
        executor.set_fake_time(start_time + ONE_HOUR);
        assert_eq!(executor.wake_next_timer(), Some(start_time + ONE_HOUR));
        assert_eq!(executor.run_until_stalled(&mut stalled_future), Poll::Ready("timeout"));
    }
}

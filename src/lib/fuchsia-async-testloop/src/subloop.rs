// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_async::{self as fasync, WaitState};
use fuchsia_zircon as zx;
use futures::Future;
use std::marker::PhantomPinned;
use std::pin::Pin;

/// A subloop as understood by the test loop.
pub(crate) trait Subloop {
    /// Advances the fake time to `time`.
    fn advance_time_to(pin_self: Pin<&mut Self>, time: zx::Time);

    /// Dispatches the next due message. Returns true iff a message
    /// was dispatched.
    fn dispatch_next_due_message(pin_self: Pin<&mut Self>) -> bool;

    /// Returns what |dispatch_next_due_message| would return but does
    /// not perform any work.
    fn has_pending_work(pin_self: Pin<&mut Self>) -> bool;

    /// Returns the next time at which this loop should be woken up if
    /// nothing else happens, or ZX_TIME_INFINITE.
    fn get_next_task_due_time(pin_self: Pin<&mut Self>) -> zx::Time;
}

/// A subloop wrapping an executor and its main future.
pub(crate) struct SubloopExecutor<F> {
    executor: fasync::TestExecutor,
    future: F,
    _pinned: PhantomPinned,
}

/// A mutable borrow of the fields of a `SubloopExecutor`.
struct SubloopExecutorBorrowMut<'a, F> {
    executor: &'a mut fasync::TestExecutor,
    future: Pin<&'a mut F>,
}

impl<F> SubloopExecutor<F> {
    /// Constructs a new `SubloopExecutor` with the given main future.
    pub(crate) fn new(future: F) -> SubloopExecutor<F> {
        let mut executor =
            fasync::TestExecutor::new_with_fake_time().expect("unable to create executor");
        executor.wake_main_future();
        SubloopExecutor { executor, future, _pinned: PhantomPinned }
    }

    // Implements pinned projection for SubloopExecutor<F>.
    // pin_utils::unsafe_{,un}pinned is not enough because we need to access the two fields concurrently.
    // Safe because SubloopExecutor<F> is !Unpin, and the returned references are to disjoint parts
    // of the executor.
    fn as_mut(self: Pin<&mut Self>) -> SubloopExecutorBorrowMut<'_, F> {
        unsafe {
            let mut_self = self.get_unchecked_mut();
            SubloopExecutorBorrowMut {
                executor: &mut mut_self.executor,
                future: Pin::new_unchecked(&mut mut_self.future),
            }
        }
    }
}

impl<F> Subloop for SubloopExecutor<F>
where
    F: Future,
{
    fn advance_time_to(pin_self: Pin<&mut Self>, time: zx::Time) {
        let mut_self = SubloopExecutor::as_mut(pin_self);
        mut_self.executor.set_fake_time(fasync::Time::from_zx(time));
    }

    fn dispatch_next_due_message(pin_self: Pin<&mut Self>) -> bool {
        let mut mut_self = SubloopExecutor::as_mut(pin_self);
        mut_self.executor.run_one_step(&mut mut_self.future).is_some()
    }

    fn has_pending_work(pin_self: Pin<&mut Self>) -> bool {
        let mut_self = SubloopExecutor::as_mut(pin_self);
        mut_self.executor.is_waiting() == WaitState::Ready
    }

    fn get_next_task_due_time(pin_self: Pin<&mut Self>) -> zx::Time {
        let mut_self = SubloopExecutor::as_mut(pin_self);
        match mut_self.executor.is_waiting() {
            WaitState::Ready => zx::Time::INFINITE_PAST,
            WaitState::Waiting(t) => t.into_zx(),
        }
    }
}

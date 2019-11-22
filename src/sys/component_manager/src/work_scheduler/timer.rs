// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::work_scheduler::work_scheduler::WorkScheduler,
    fuchsia_async::{self as fasync, Time, Timer},
    futures::future::{AbortHandle, Abortable},
    std::sync::Weak,
};

/// A self-managed timer instantiated by `WorkScheduler` to implement the "wakeup" part of its
/// wakeup, batch, and dispatch cycles.
pub(super) struct WorkSchedulerTimer {
    /// Next absolute monotonic time when a timeout should be triggered to wakeup, batch, and
    /// dispatch work.
    next_timeout_monotonic: i64,
    /// The handle used to abort the next wakeup, batch, and dispatch cycle if it needs to be
    /// replaced by a different timer to be woken up at a different time.
    abort_handle: AbortHandle,
}

impl WorkSchedulerTimer {
    /// Construct a new timer that will fire at monotonic time `next_timeout_monotonic`. When the
    /// the timer fires, if it was not aborted, it will invoke `work_scheduler.dispatch_work()`.
    pub(super) fn new(
        next_timeout_monotonic: i64,
        weak_work_scheduler: Weak<WorkScheduler>,
    ) -> Self {
        let (abort_handle, abort_registration) = AbortHandle::new_pair();

        let future = Abortable::new(
            Timer::new(Time::from_nanos(next_timeout_monotonic)),
            abort_registration,
        );
        fasync::spawn(async move {
            // Dispatch work only when abortable was not aborted and `WorkScheduler` is still valid.
            if future.await.is_ok() {
                if let Some(work_scheduler) = weak_work_scheduler.upgrade() {
                    work_scheduler.dispatch_work().await;
                }
            }
        });

        WorkSchedulerTimer { next_timeout_monotonic, abort_handle }
    }

    pub(super) fn next_timeout_monotonic(&self) -> i64 {
        self.next_timeout_monotonic
    }
}

/// Automatically cancel a timer that is dropped by `WorkScheduler`. This allows `WorkScheduler` to
/// use patterns like:
///
///   WorkScheduler.timer = Some(WorkSchedulerTimer::new(deadline, self.clone()))
///
/// and expect any timer previously stored in `WorkScheduler.timer` to be aborted as a part of the
/// operation.
impl Drop for WorkSchedulerTimer {
    fn drop(&mut self) {
        self.abort_handle.abort();
    }
}

// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Fuchsia Executor Instrumentation.
//!
//! This module contains types used for instrumenting the fuchsia-async executor.
//! It exposes an event style API, intended to be invoked at certain points
//! during execution time, agnostic to the implementation of the executor.
//! The current implementation records counts and time slices with a focus
//! on minimal overhead.

use fuchsia_zircon as zx;
use std::sync::atomic::{AtomicU64, AtomicUsize, Ordering};
use std::{cmp, mem};

/// A low-overhead metrics collection type intended for use by an async executor.
// TODO(fxbug.dev/58578): Impl debug with Ordering::Acquire, instead of the SeqCst derived impl.
#[derive(Default, Debug)]
pub struct Collector {
    tasks_created: AtomicUsize,
    tasks_completed: AtomicUsize,
    tasks_pending_max: AtomicUsize,
    polls: AtomicUsize,
    wakeups_io: AtomicUsize,
    wakeups_deadline: AtomicUsize,
    wakeups_notification: AtomicUsize,
    ticks_awake: AtomicU64,
    ticks_asleep: AtomicU64,
}

impl Collector {
    /// Create a new blank collector.
    pub fn new() -> Self {
        Self::default()
    }

    /// Called when a task is created (usually, this means spawned).
    pub fn task_created(&self, _id: usize) {
        self.tasks_created.fetch_add(1, Ordering::Relaxed);
    }

    /// Called when a task is complete.
    pub fn task_completed(&self, _id: usize) {
        self.tasks_completed.fetch_add(1, Ordering::Relaxed);
    }

    /// Creates a local collector. Each run loop should have its own local collector.
    /// The local collector is initially awake, and has no recorded events.
    pub fn create_local_collector(&self) -> LocalCollector<'_> {
        LocalCollector {
            collector: &self,
            last_ticks: zx::ticks_get(),
            polls: 0,
            tasks_pending_max: 0, // Loading not necessary, handled by first update with same cost
        }
    }
}

/// A logical sub-collector type of a `Collector`, for a local run-loop.
pub struct LocalCollector<'a> {
    /// The main collector of this local collector
    collector: &'a Collector,

    /// Ticks since the awake state was last toggled
    last_ticks: i64,

    /// Number of polls since last `will_wait`
    polls: usize,

    /// Last observed `tasks_pending_max` from main Collector
    tasks_pending_max: usize,
}

impl<'a> LocalCollector<'a> {
    /// Called after a task was polled. If the task completed, `complete`
    /// should be true. `pending_tasks` is the observed size of the pending task
    /// queue (excluding the currently polled task).
    pub fn task_polled(&mut self, id: usize, complete: bool, tasks_pending: usize) {
        self.polls += 1;
        let new_local_max = cmp::max(self.tasks_pending_max, tasks_pending);
        if new_local_max > self.tasks_pending_max {
            let prev_upstream_max =
                self.collector.tasks_pending_max.fetch_max(new_local_max, Ordering::Relaxed);
            self.tasks_pending_max = cmp::max(new_local_max, prev_upstream_max);
        }
        if complete {
            self.collector.task_completed(id);
        }
    }

    /// Called before the loop waits. Must be followed by a `woke_up` call.
    pub fn will_wait(&mut self) {
        let delta = self.bump_ticks();
        self.collector.ticks_awake.fetch_add(delta, Ordering::Relaxed);
        self.collector.polls.fetch_add(mem::replace(&mut self.polls, 0), Ordering::Relaxed);
    }

    /// Called after the loop wakes up from waiting, containing the reason
    /// for the wakeup. Must follow a `will_wait` call.
    pub fn woke_up(&mut self, wakeup_reason: WakeupReason) {
        let delta = self.bump_ticks();
        let counter = match wakeup_reason {
            WakeupReason::Io => &self.collector.wakeups_io,
            WakeupReason::Deadline => &self.collector.wakeups_deadline,
            WakeupReason::Notification => &self.collector.wakeups_notification,
        };
        counter.fetch_add(1, Ordering::Relaxed);
        self.collector.ticks_asleep.fetch_add(delta, Ordering::Relaxed);
    }

    /// Helper which replaces `last_ticks` with the current ticks.
    /// Returns the ticks elapsed since `last_ticks`.
    fn bump_ticks(&mut self) -> u64 {
        let current_ticks = zx::ticks_get();
        let delta = current_ticks - self.last_ticks;
        assert!(delta >= 0, "time moved backwards in zx::ticks_get()");
        self.last_ticks = current_ticks;
        delta as u64
    }
}

impl<'a> Drop for LocalCollector<'a> {
    fn drop(&mut self) {
        let delta = self.bump_ticks();
        self.collector.polls.fetch_add(self.polls, Ordering::Release);
        self.collector.ticks_awake.fetch_add(delta, Ordering::Release);
    }
}

/// The reason a waiting run-loop was woken up.
pub enum WakeupReason {
    /// An external io packet was received on the port.
    Io,

    /// Deadline from a user-space timer.
    Deadline,

    /// An executor-internal notification.
    Notification,
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Helper which keeps track of last observed tick counts, and reports
    /// changes.
    struct Ticker<'a> {
        c: &'a Collector,
        awake: u64,  // Last observed awake ticks
        asleep: u64, // Last observed asleep ticks
    }

    impl<'a> Ticker<'a> {
        fn new(c: &'a Collector) -> Self {
            Self { c, awake: 0, asleep: 0 }
        }

        /// Updates awake and asleep ticks to current values. Returns a bool
        /// 2-tuple indicating whether awake and asleep time, respectively,
        /// progressed since last cycle.
        fn update(&mut self) -> (bool, bool) {
            let old_awake =
                mem::replace(&mut self.awake, self.c.ticks_awake.load(Ordering::Relaxed));
            let old_asleep =
                mem::replace(&mut self.asleep, self.c.ticks_asleep.load(Ordering::Relaxed));
            (self.awake > old_awake, self.asleep > old_asleep)
        }
    }

    #[test]
    fn debug_str() {
        let collector = Collector {
            tasks_created: 10.into(),
            tasks_completed: 9.into(),
            tasks_pending_max: 3.into(),
            polls: 1000.into(),
            wakeups_io: 123.into(),
            wakeups_deadline: 456.into(),
            wakeups_notification: 789.into(),
            ticks_awake: 100000.into(),
            ticks_asleep: 200000.into(),
        };
        assert_eq!(
            format!("{:?}", collector),
            "\
        Collector { tasks_created: 10, tasks_completed: 9, tasks_pending_max: 3, \
        polls: 1000, wakeups_io: 123, wakeups_deadline: 456, wakeups_notification: 789, \
        ticks_awake: 100000, ticks_asleep: 200000 }"
        );
    }

    #[test]
    fn collector() {
        let collector = Collector::new();
        collector.task_created(0);
        collector.task_created(1);
        collector.task_completed(0);
        assert_eq!(collector.tasks_created.load(Ordering::Relaxed), 2);
        assert_eq!(collector.tasks_completed.load(Ordering::Relaxed), 1);
    }

    #[test]
    fn task_polled() {
        let collector = Collector::new();
        collector.tasks_pending_max.store(6, Ordering::Relaxed);
        collector.polls.store(1, Ordering::Relaxed);
        let mut local_collector = collector.create_local_collector();
        local_collector.task_polled(0, false, /* pending_tasks */ 5);
        assert_eq!(collector.tasks_pending_max.load(Ordering::Relaxed), 6);
        local_collector.task_polled(0, false, /* pending_tasks */ 7);
        assert_eq!(collector.tasks_pending_max.load(Ordering::Relaxed), 7);
        assert_eq!(local_collector.polls, 2);

        // Polls not yet flushed
        assert_eq!(collector.polls.load(Ordering::Relaxed), 1);

        // Collector polls: 1 -> 3
        local_collector.will_wait();
        assert_eq!(collector.polls.load(Ordering::Relaxed), 3);

        // Check that local collector was reset
        assert_eq!(local_collector.tasks_pending_max, 7);
        assert_eq!(local_collector.polls, 0);

        // One more cycle to check that max is idempotent when main collector is greater
        collector.tasks_pending_max.store(10, Ordering::Relaxed);
        local_collector.woke_up(WakeupReason::Io);
        local_collector.task_polled(0, false, /* pending_tasks */ 8);
        assert_eq!(local_collector.tasks_pending_max, 10);

        // Flush with drop this time. Polls 3 -> 4
        drop(local_collector);

        assert_eq!(collector.tasks_pending_max.load(Ordering::Relaxed), 10);
        assert_eq!(collector.polls.load(Ordering::Relaxed), 4);
    }

    #[test]
    fn ticks() {
        let collector = Collector::new();
        let mut ticker = Ticker::new(&collector);
        let mut local_collector = collector.create_local_collector();
        assert_eq!(ticker.update(), (false, false));

        // Will wait should move awake time forward
        local_collector.will_wait();
        assert_eq!(ticker.update(), (true, false));

        // Woke up should move asleep time forward
        local_collector.woke_up(WakeupReason::Io);
        assert_eq!(ticker.update(), (false, true));

        // Poll should NOT move awake time forward
        local_collector.task_polled(0, true, 1);
        assert_eq!(ticker.update(), (false, false));

        // Drop should move awake time forward
        drop(local_collector);
        assert_eq!(ticker.update(), (true, false));
    }

    #[test]
    fn wakeups() {
        let collector = Collector::new();
        let mut local_collector = collector.create_local_collector();

        local_collector.will_wait();
        local_collector.woke_up(WakeupReason::Io);
        local_collector.will_wait();
        local_collector.woke_up(WakeupReason::Deadline);
        local_collector.will_wait();
        local_collector.woke_up(WakeupReason::Deadline);
        local_collector.will_wait();
        local_collector.woke_up(WakeupReason::Notification);
        local_collector.will_wait();
        local_collector.woke_up(WakeupReason::Notification);
        local_collector.will_wait();
        local_collector.woke_up(WakeupReason::Notification);

        assert_eq!(collector.wakeups_io.load(Ordering::Relaxed), 1);
        assert_eq!(collector.wakeups_deadline.load(Ordering::Relaxed), 2);
        assert_eq!(collector.wakeups_notification.load(Ordering::Relaxed), 3);
    }

    // Check that multiple local collectors coalesce into expected aggregates.
    // Covers some permutations of interleaved calls.
    #[test]
    fn multiple_local_collectors() {
        let collector = Collector::new();
        let mut ticker = Ticker::new(&collector);

        // tasks_created += 1
        collector.task_created(0);
        let mut local_1 = collector.create_local_collector();

        // tasks_polled += 1, tasks_pending_max = 5
        local_1.task_polled(0, false, 5);

        // ticks_awake += T
        local_1.will_wait();
        assert_eq!(ticker.update(), (true, false));

        let mut local_2 = collector.create_local_collector();

        // tasks_created += 1, tasks_polled += 2, tasks_complete += 1, tasks_pending_max = 7
        collector.task_created(1);
        local_2.task_polled(0, false, 7);
        local_2.task_polled(0, true, 3);

        // ticks_awake += T
        local_2.will_wait();
        assert_eq!(ticker.update(), (true, false));

        // ticks_asleep += T
        local_1.woke_up(WakeupReason::Io);
        assert_eq!(ticker.update(), (false, true));

        // ticks_asleep += T
        local_1.woke_up(WakeupReason::Deadline);
        assert_eq!(ticker.update(), (false, true));

        // ticks_awake += T
        drop(local_1);
        assert_eq!(ticker.update(), (true, false));

        // ticks_awake += T
        drop(local_2);
        assert_eq!(ticker.update(), (true, false));

        // Finally, check that counters match their expected values
        assert_eq!(collector.tasks_created.load(Ordering::Relaxed), 2);
        assert_eq!(collector.tasks_completed.load(Ordering::Relaxed), 1);
        assert_eq!(collector.polls.load(Ordering::Relaxed), 3);
        assert_eq!(collector.tasks_pending_max.load(Ordering::Relaxed), 7);
        assert_eq!(collector.wakeups_io.load(Ordering::Relaxed), 1);
        assert_eq!(collector.wakeups_deadline.load(Ordering::Relaxed), 1);
        assert_eq!(collector.wakeups_notification.load(Ordering::Relaxed), 0);
    }
}

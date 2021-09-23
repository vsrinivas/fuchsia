// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::HashMap;
use std::sync::Arc;

use crate::signals::types::*;
use crate::task::Task;
use crate::task::Waiter;
use crate::types::*;

pub struct Scheduler {
    /// The waiters that suspended tasks are waiting on, organized by pid_t of the suspended task.
    pub suspended_tasks: HashMap<pid_t, Arc<Waiter>>,

    /// The number of pending signals for a given task.
    ///
    /// There may be more than one instance of a real-time signal pending, but for standard
    /// signals there is only ever one instance of any given signal.
    ///
    /// Signals are delivered immediately if the target is running, but there are two cases where
    /// the signal would end up pending:
    ///   1. The task is not running, the signal will then be delivered the next time the task is
    ///      scheduled to run.
    ///   2. The signal is blocked by the target. The signal is then pending until the signal is
    ///      unblocked and can be delivered to the target.
    pub pending_signals: HashMap<pid_t, HashMap<Signal, u64>>,
}

impl Scheduler {
    pub fn new() -> Scheduler {
        Scheduler { pending_signals: HashMap::new(), suspended_tasks: HashMap::new() }
    }

    /// Adds a task to the set of tasks currently suspended via `rt_sigsuspend`.
    ///
    /// The task's `task.waiter` will be used to wake the task back up.
    pub fn add_suspended_task(&mut self, task: &Task) {
        log::info!("Suspending task: {}", task.id);
        self.suspended_tasks.insert(task.id, task.waiter.clone());
    }

    #[cfg(test)]
    /// Returns true if the Task associated with `pid` is currently suspended in `rt_sigsuspend`.
    pub fn is_task_suspended(&self, pid: pid_t) -> bool {
        self.suspended_tasks.contains_key(&pid)
    }

    /// Removes the waiter that `pid` is waiting on.
    ///
    /// The returned waiter is meant to be notified before it is dropped in order
    /// for the task to resume operation in `rt_sigsuspend`.
    pub fn remove_suspended_task(&mut self, pid: pid_t) -> Option<Arc<Waiter>> {
        self.suspended_tasks.remove(&pid)
    }

    /// Adds a pending signal for `pid`.
    ///
    /// If there is already a `signal` pending for `pid`, the new signal is:
    ///   - Ignored if the signal is a standard signal.
    ///   - Added to the queue if the signal is a real-time signal.
    pub fn add_pending_signal(&mut self, pid: pid_t, signal: Signal) {
        let pending_signals = self.pending_signals.entry(pid).or_default();

        let number_of_pending_signals = pending_signals.entry(signal.clone()).or_insert(0);

        // A single real-time signal can be queued multiple times, but all other signals are only
        // queued once.
        if signal.is_real_time() {
            *number_of_pending_signals += 1;
        } else {
            *number_of_pending_signals = 1;
        }
    }

    /// Gets the pending signals for `pid`.
    ///
    /// Note: `self` is `&mut` because an empty map is created if no map currently exists. This
    /// could potentially return Option<&HashMap> if the `&mut` becomes a problem.
    pub fn get_pending_signals(&mut self, pid: pid_t) -> &mut HashMap<Signal, u64> {
        self.pending_signals.entry(pid).or_default()
    }
}

// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon::{self as zx, AsHandleRef, Task as zxTask};
use log::warn;
use parking_lot::RwLock;
use std::collections::HashSet;
use std::ffi::CStr;
use std::sync::Arc;

use crate::signals::types::*;
use crate::task::*;
use crate::types::*;

#[derive(Debug, Default)]
pub struct SignalState {
    /// The ITIMER_REAL timer.
    ///
    /// See <https://linux.die.net/man/2/setitimer>/
    // TODO: Actually schedule and fire the timer.
    pub itimer_real: itimerval,

    /// The ITIMER_VIRTUAL timer.
    ///
    /// See <https://linux.die.net/man/2/setitimer>/
    // TODO: Actually schedule and fire the timer.
    pub itimer_virtual: itimerval,

    /// The ITIMER_PROF timer.
    ///
    /// See <https://linux.die.net/man/2/setitimer>/
    // TODO: Actually schedule and fire the timer.
    pub itimer_prof: itimerval,
}

pub struct ThreadGroup {
    /// The kernel to which this thread group belongs.
    pub kernel: Arc<Kernel>,

    /// A handle to the underlying Zircon process object.
    ///
    /// Currently, we have a 1-to-1 mapping between thread groups and zx::process
    /// objects. This approach might break down if/when we implement CLONE_VM
    /// without CLONE_THREAD because that creates a situation where two thread
    /// groups share an address space. To implement that situation, we might
    /// need to break the 1-to-1 mapping between thread groups and zx::process
    /// or teach zx::process to share address spaces.
    pub process: zx::Process,

    /// The lead task of this thread group.
    ///
    /// The lead task is typically the initial thread created in the thread group.
    pub leader: pid_t,

    /// The tasks in the thread group.
    pub tasks: RwLock<HashSet<pid_t>>,

    /// The signal state for this thread group.
    pub signal_state: RwLock<SignalState>,

    /// The signal actions that are registered for `tasks`. All `tasks` share the same `sigaction`
    /// for a given signal.
    // TODO: Move into signal_state.
    pub signal_actions: RwLock<SignalActions>,
}

impl PartialEq for ThreadGroup {
    fn eq(&self, other: &Self) -> bool {
        self.leader == other.leader
    }
}

impl ThreadGroup {
    pub fn new(kernel: Arc<Kernel>, process: zx::Process, leader: pid_t) -> ThreadGroup {
        let mut tasks = HashSet::new();
        tasks.insert(leader);

        ThreadGroup {
            kernel,
            process,
            leader,
            tasks: RwLock::new(tasks),
            signal_state: RwLock::new(SignalState::default()),
            signal_actions: RwLock::new(SignalActions::default()),
        }
    }

    pub fn add(&self, task: &Task) {
        let mut tasks = self.tasks.write();
        tasks.insert(task.id);
    }

    pub fn remove(&self, task: &Task) {
        let kill_process = {
            let mut tasks = self.tasks.write();
            self.kernel.pids.write().remove_task(task.id);
            tasks.remove(&task.id);
            tasks.is_empty()
        };
        if kill_process {
            if let Err(e) = self.process.kill() {
                warn!("Failed to kill process: {}", e);
            }
            self.kernel.pids.write().remove_thread_group(self.leader);
        }
    }

    pub fn set_name(&self, name: &CStr) -> Result<(), Errno> {
        self.process.set_name(name).map_err(Errno::from_status_like_fdio)
    }
}

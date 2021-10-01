// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon::{self as zx, AsHandleRef, Task as zxTask};
use fuchsia_zircon_sys as sys;
use parking_lot::{Mutex, RwLock};
use std::collections::HashSet;
use std::ffi::CStr;
use std::sync::Arc;

use crate::errno;
use crate::from_status_like_fdio;
use crate::signals::signal_handling::send_checked_signal;
use crate::signals::types::*;
use crate::task::*;
use crate::types::*;

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

    /// The itimers for this thread group.
    pub itimers: RwLock<[itimerval; 3]>,

    zombie_leader: Mutex<Option<ZombieTask>>,

    /// WaitQueue for updates to the zombie_children lists of tasks in this group.
    pub child_exit_waiters: Mutex<WaitQueue>,
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
            itimers: Default::default(),
            zombie_leader: Mutex::new(None),
            child_exit_waiters: Mutex::default(),
        }
    }

    pub fn exit(&self) {
        // TODO: Set the error_code on the Zircon process object. Currently missing a way
        //       to do this in Zircon. Might be easier in the new execution model.
        self.process.kill().expect("Failed to terminate process.");
        // TODO: Should we wait for the process to actually terminate?
    }

    pub fn add(&self, task: &Task) {
        let mut tasks = self.tasks.write();
        tasks.insert(task.id);
    }

    fn remove_internal(&self, task: &Arc<Task>) -> bool {
        let mut tasks = self.tasks.write();
        self.kernel.pids.write().remove_task(task.id);
        tasks.remove(&task.id);

        if task.id == self.leader {
            *self.zombie_leader.lock() = Some(task.as_zombie());
        }

        tasks.is_empty()
    }

    pub fn remove(&self, task: &Arc<Task>) {
        if self.remove_internal(task) {
            let zombie =
                self.zombie_leader.lock().take().expect("Failed to capture zombie leader.");

            if let Some(parent) = self.kernel.pids.read().get_task(zombie.parent) {
                parent.zombie_children.lock().push(zombie);
                // TODO: Should this be zombie_leader.exit_signal?
                send_checked_signal(&parent, Signal::SIGCHLD).unwrap_or_else(|err| {
                    log::warn!(
                        "unexpected error with send_checked_signal in ThreadGroup::remove {}",
                        err
                    );
                });
            }

            if let Some(parent) = self.kernel.pids.read().get_task(task.parent) {
                parent.thread_group.child_exit_waiters.lock().notify_all();
            }
            self.kernel.pids.write().remove_thread_group(self.leader);
        }
    }

    /// Sets the name of process associated with this thread group.
    ///
    /// - `name`: The name to set for the process. If the length of `name` is >= `ZX_MAX_NAME_LEN`,
    /// a truncated version of the name will be used.
    pub fn set_name(&self, name: &CStr) -> Result<(), Errno> {
        if name.to_bytes().len() >= sys::ZX_MAX_NAME_LEN {
            // TODO: Might want to use [..sys::ZX_MAX_NAME_LEN] of only the last path component.
            let mut clone = name.clone().to_owned().into_bytes();
            clone[sys::ZX_MAX_NAME_LEN - 1] = 0;
            let name = CStr::from_bytes_with_nul(&clone[..sys::ZX_MAX_NAME_LEN])
                .map_err(|_| errno!(EINVAL))?;
            self.process.set_name(name).map_err(|status| from_status_like_fdio!(status))
        } else {
            self.process.set_name(name).map_err(|status| from_status_like_fdio!(status))
        }
    }
}

#[cfg(test)]
mod test {
    use fuchsia_async as fasync;

    use super::*;
    use crate::testing::*;
    use std::ffi::CString;

    #[fasync::run_singlethreaded(test)]
    async fn test_long_name() {
        let (_kernel, task_owner) = create_kernel_and_task();
        let bytes = [1; sys::ZX_MAX_NAME_LEN];
        let name = CString::new(bytes).unwrap();

        let max_bytes = [1; sys::ZX_MAX_NAME_LEN - 1];
        let expected_name = CString::new(max_bytes).unwrap();

        assert!(task_owner.task.thread_group.set_name(&name).is_ok());
        assert_eq!(task_owner.task.thread_group.process.get_name(), Ok(expected_name));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_max_length_name() {
        let (_kernel, task_owner) = create_kernel_and_task();
        let bytes = [1; sys::ZX_MAX_NAME_LEN - 1];
        let name = CString::new(bytes).unwrap();

        assert!(task_owner.task.thread_group.set_name(&name).is_ok());
        assert_eq!(task_owner.task.thread_group.process.get_name(), Ok(name));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_short_name() {
        let (_kernel, task_owner) = create_kernel_and_task();
        let bytes = [1; sys::ZX_MAX_NAME_LEN - 10];
        let name = CString::new(bytes).unwrap();

        assert!(task_owner.task.thread_group.set_name(&name).is_ok());
        assert_eq!(task_owner.task.thread_group.process.get_name(), Ok(name));
    }
}

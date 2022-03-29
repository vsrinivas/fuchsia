// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon::sys;
use fuchsia_zircon::{self as zx, AsHandleRef};
use parking_lot::{Mutex, RwLock};
use std::collections::HashSet;
use std::ffi::CStr;
use std::sync::Arc;

use crate::auth::ShellJobControl;
use crate::signals::*;
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

    /// The IDs used to perform shell job control.
    pub job_control: RwLock<ShellJobControl>,

    /// The itimers for this thread group.
    pub itimers: RwLock<[itimerval; 3]>,

    zombie_leader: Mutex<Option<ZombieTask>>,

    /// WaitQueue for updates to the zombie_children lists of tasks in this group.
    pub child_exit_waiters: Mutex<WaitQueue>,

    terminating: Mutex<bool>,

    pub did_exec: RwLock<bool>,
}

impl PartialEq for ThreadGroup {
    fn eq(&self, other: &Self) -> bool {
        self.leader == other.leader
    }
}

impl ThreadGroup {
    pub fn new(
        kernel: Arc<Kernel>,
        process: zx::Process,
        leader: pid_t,
        job_control: ShellJobControl,
    ) -> ThreadGroup {
        let mut tasks = HashSet::new();
        tasks.insert(leader);

        ThreadGroup {
            kernel,
            process,
            leader,
            job_control: RwLock::new(job_control),
            tasks: RwLock::new(tasks),
            itimers: Default::default(),
            zombie_leader: Mutex::new(None),
            child_exit_waiters: Mutex::default(),
            terminating: Mutex::new(false),
            did_exec: RwLock::new(false),
        }
    }

    pub fn exit(&self, exit_code: i32) {
        let mut terminating = self.terminating.lock();
        if *terminating {
            // The thread group is already terminating and the SIGKILL signals have already been
            // sent to all threads in the thread group.
            return;
        }
        *terminating = true;

        // Send a SIGKILL signal to each task.
        let pids = self.kernel.pids.read();
        for tid in &*self.tasks.read() {
            if let Some(task) = pids.get_task(*tid) {
                // NOTE: It's possible for a task calling `sys_exit` to race with this,
                // which could lead to an unexpected exit code.
                *task.exit_code.lock() = Some(exit_code);
                send_signal(&*task, SignalInfo::default(SIGKILL));
            }
        }
    }

    pub fn add(&self, task: &Task) -> Result<(), Errno> {
        let terminating = self.terminating.lock();
        if *terminating {
            return error!(EINVAL);
        }
        let mut tasks = self.tasks.write();
        tasks.insert(task.id);
        Ok(())
    }

    pub fn setsid(&self) -> Result<(), Errno> {
        let mut pids = self.kernel.pids.write();
        if !pids.get_process_group(self.leader).is_none() {
            return error!(EPERM);
        }
        let job_control = ShellJobControl { sid: self.leader, pgid: self.leader };
        pids.set_job_control(self.leader, &job_control);
        *self.job_control.write() = job_control;
        Ok(())
    }

    pub fn setpgid(&self, target: &Task, pgid: pid_t) -> Result<(), Errno> {
        // The target process must be either the current process of a child of the current process
        let target_thread_group = &target.thread_group;
        if target_thread_group.leader != self.leader && target.parent != self.leader {
            return error!(ESRCH);
        }

        // If the target process is a child of the current task, it must not have executed one of the exec
        // function. Keep target_did_exec lock until the end of this function to prevent a race.
        let target_did_exec;
        if target.parent == self.leader {
            target_did_exec = target_thread_group.did_exec.read();
            if *target_did_exec {
                return error!(EACCES);
            }
        }

        let mut pids = self.kernel.pids.write();
        let sid;
        let target_pgid;
        {
            let current_job_control = self.job_control.read();
            let target_job_control = target_thread_group.job_control.read();

            // The target process must not be a session leader and must be in the same session as the current process.
            if target_thread_group.leader == target_job_control.sid
                || current_job_control.sid != target_job_control.sid
            {
                return error!(EPERM);
            }

            target_pgid = if pgid == 0 { target_thread_group.leader } else { pgid };
            if target_pgid < 0 {
                return error!(EINVAL);
            }

            // If pgid is not equal to the target process id, the associated process group must exist
            // and be in the same session as the target process.
            if target_pgid != target_thread_group.leader {
                let process_group = pids.get_process_group(target_pgid).ok_or(EPERM)?;
                if process_group.sid != target_job_control.sid {
                    return error!(EPERM);
                }
            }

            sid = target_job_control.sid;
        }

        let new_job_control = ShellJobControl { sid: sid, pgid: target_pgid };
        pids.set_job_control(target.id, &new_job_control);
        *target_thread_group.job_control.write() = new_job_control;

        Ok(())
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
            let mut terminating = self.terminating.lock();
            *terminating = true;

            let zombie =
                self.zombie_leader.lock().take().expect("Failed to capture zombie leader.");

            self.kernel.pids.write().remove_thread_group(self.leader);

            if let Some(parent) = self.kernel.pids.read().get_task(zombie.parent) {
                parent.zombie_children.lock().push(zombie);
                // TODO: Should this be zombie_leader.exit_signal?
                send_signal(&parent, SignalInfo::default(SIGCHLD));
                parent.thread_group.child_exit_waiters.lock().notify_all();
            }

            // TODO: Set the error_code on the Zircon process object. Currently missing a way
            // to do this in Zircon. Might be easier in the new execution model.

            // Once the last zircon thread stops, the zircon process will also stop executing.
        }
    }

    /// Sets the name of process associated with this thread group.
    ///
    /// - `name`: The name to set for the process. If the length of `name` is >= `ZX_MAX_NAME_LEN`,
    /// a truncated version of the name will be used.
    pub fn set_name(&self, name: &CStr) -> Result<(), Errno> {
        if name.to_bytes().len() >= sys::ZX_MAX_NAME_LEN {
            // TODO: Might want to use [..sys::ZX_MAX_NAME_LEN] of only the last path component.
            #[allow(clippy::clone_double_ref)] // TODO(fxbug.dev/95057)
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
    use super::*;
    use crate::testing::*;
    use std::ffi::CString;

    #[::fuchsia::test]
    fn test_long_name() {
        let (_kernel, current_task) = create_kernel_and_task();
        let bytes = [1; sys::ZX_MAX_NAME_LEN];
        let name = CString::new(bytes).unwrap();

        let max_bytes = [1; sys::ZX_MAX_NAME_LEN - 1];
        let expected_name = CString::new(max_bytes).unwrap();

        assert!(current_task.thread_group.set_name(&name).is_ok());
        assert_eq!(current_task.thread_group.process.get_name(), Ok(expected_name));
    }

    #[::fuchsia::test]
    fn test_max_length_name() {
        let (_kernel, current_task) = create_kernel_and_task();
        let bytes = [1; sys::ZX_MAX_NAME_LEN - 1];
        let name = CString::new(bytes).unwrap();

        assert!(current_task.thread_group.set_name(&name).is_ok());
        assert_eq!(current_task.thread_group.process.get_name(), Ok(name));
    }

    #[::fuchsia::test]
    fn test_short_name() {
        let (_kernel, current_task) = create_kernel_and_task();
        let bytes = [1; sys::ZX_MAX_NAME_LEN - 10];
        let name = CString::new(bytes).unwrap();

        assert!(current_task.thread_group.set_name(&name).is_ok());
        assert_eq!(current_task.thread_group.process.get_name(), Ok(name));
    }

    #[::fuchsia::test]
    fn test_setsid() {
        let (_kernel, current_task) = create_kernel_and_task();
        assert_eq!(current_task.thread_group.setsid(), error!(EPERM));

        let child_task = current_task
            .clone_task(
                0,
                UserRef::new(UserAddress::default()),
                UserRef::new(UserAddress::default()),
            )
            .expect("clone process");
        // Set an exit code to not panic when task is dropped and moved to the zombie state.
        *child_task.exit_code.lock() = Some(0);
        assert_eq!(
            current_task.thread_group.job_control.read().sid,
            child_task.thread_group.job_control.read().sid
        );

        assert_eq!(child_task.thread_group.setsid(), Ok(()));
        assert_eq!(child_task.thread_group.job_control.read().sid, child_task.get_pid());
    }

    #[::fuchsia::test]
    fn test_setgpid() {
        let (_kernel, current_task) = create_kernel_and_task();
        assert_eq!(current_task.thread_group.setsid(), error!(EPERM));

        let child_task1 = current_task
            .clone_task(
                0,
                UserRef::new(UserAddress::default()),
                UserRef::new(UserAddress::default()),
            )
            .expect("clone process");
        *child_task1.exit_code.lock() = Some(0);
        let child_task2 = current_task
            .clone_task(
                0,
                UserRef::new(UserAddress::default()),
                UserRef::new(UserAddress::default()),
            )
            .expect("clone process");
        *child_task2.exit_code.lock() = Some(0);
        let execd_child_task = current_task
            .clone_task(
                0,
                UserRef::new(UserAddress::default()),
                UserRef::new(UserAddress::default()),
            )
            .expect("clone process");
        *execd_child_task.exit_code.lock() = Some(0);
        *execd_child_task.thread_group.did_exec.write() = true;
        let other_session_child_task = current_task
            .clone_task(
                0,
                UserRef::new(UserAddress::default()),
                UserRef::new(UserAddress::default()),
            )
            .expect("clone process");
        *other_session_child_task.exit_code.lock() = Some(0);
        assert_eq!(other_session_child_task.thread_group.setsid(), Ok(()));

        assert_eq!(child_task1.thread_group.setpgid(&current_task, 0), error!(ESRCH));
        assert_eq!(current_task.thread_group.setpgid(&execd_child_task, 0), error!(EACCES));
        assert_eq!(current_task.thread_group.setpgid(&current_task, 0), error!(EPERM));
        assert_eq!(current_task.thread_group.setpgid(&other_session_child_task, 0), error!(EPERM));
        assert_eq!(current_task.thread_group.setpgid(&child_task1, -1), error!(EINVAL));
        assert_eq!(current_task.thread_group.setpgid(&child_task1, 255), error!(EPERM));
        assert_eq!(
            current_task.thread_group.setpgid(&child_task1, other_session_child_task.id),
            error!(EPERM)
        );

        assert_eq!(child_task1.thread_group.setpgid(&child_task1, 0), Ok(()));
        assert_eq!(child_task1.thread_group.job_control.read().sid, current_task.id);
        assert_eq!(child_task1.thread_group.job_control.read().pgid, child_task1.id);

        assert_eq!(current_task.thread_group.setpgid(&child_task2, child_task1.id), Ok(()));
        assert_eq!(child_task2.thread_group.job_control.read().pgid, child_task1.id);
    }
}

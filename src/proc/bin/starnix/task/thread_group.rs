// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon::sys;
use fuchsia_zircon::{self as zx, AsHandleRef};
use parking_lot::{Mutex, RwLock};
use std::collections::HashSet;
use std::ffi::CStr;
use std::sync::Arc;

use crate::device::terminal::*;
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
    pub process_group: RwLock<Arc<ProcessGroup>>,

    /// The itimers for this thread group.
    pub itimers: RwLock<[itimerval; 3]>,

    zombie_leader: Mutex<Option<ZombieTask>>,

    /// WaitQueue for updates to the zombie_children lists of tasks in this group.
    pub child_exit_waiters: Mutex<WaitQueue>,

    terminating: Mutex<bool>,

    pub did_exec: RwLock<bool>,

    /// The signal actions that are registered for this process.
    pub signal_actions: Arc<SignalActions>,
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
        process_group: Arc<ProcessGroup>,
        signal_actions: Arc<SignalActions>,
    ) -> ThreadGroup {
        let mut tasks = HashSet::new();
        tasks.insert(leader);
        process_group.thread_groups.write().insert(leader);

        ThreadGroup {
            kernel,
            process,
            leader,
            process_group: RwLock::new(process_group),
            tasks: RwLock::new(tasks),
            itimers: Default::default(),
            zombie_leader: Mutex::new(None),
            child_exit_waiters: Mutex::default(),
            terminating: Mutex::new(false),
            did_exec: RwLock::new(false),
            signal_actions,
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
        let process_group = ProcessGroup::new(Session::new(self.leader), self.leader);
        pids.add_process_group(&process_group);
        self.set_process_group(process_group);

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

        let new_process_group;
        {
            let mut pids = self.kernel.pids.write();
            let current_process_group = self.process_group.read();
            let target_process_group = target_thread_group.process_group.read();

            // The target process must not be a session leader and must be in the same session as the current process.
            if target_thread_group.leader == target_process_group.session.leader
                || current_process_group.session != target_process_group.session
            {
                return error!(EPERM);
            }

            let target_pgid = if pgid == 0 { target_thread_group.leader } else { pgid };
            if target_pgid < 0 {
                return error!(EINVAL);
            }

            if target_pgid == target_process_group.leader {
                return Ok(());
            }

            // If pgid is not equal to the target process id, the associated process group must exist
            // and be in the same session as the target process.
            if target_pgid != target_thread_group.leader {
                new_process_group = pids.get_process_group(target_pgid).ok_or(EPERM)?;
                if new_process_group.session != target_process_group.session {
                    return error!(EPERM);
                }
            } else {
                // Create a new process group
                new_process_group =
                    ProcessGroup::new(target_process_group.session.clone(), target_pgid);
                pids.add_process_group(&new_process_group);
            }
        }

        target_thread_group.set_process_group(new_process_group);

        Ok(())
    }

    fn set_process_group(&self, process_group: Arc<ProcessGroup>) {
        let mut process_group_writer = self.process_group.write();
        if *process_group_writer == process_group {
            return;
        }
        self.leave_process_group(&process_group_writer);
        *process_group_writer = process_group;
        process_group_writer.thread_groups.write().insert(self.leader);
    }

    fn leave_process_group(&self, process_group: &Arc<ProcessGroup>) {
        if process_group.remove(self.leader) {
            let mut pids = self.kernel.pids.write();
            if process_group.session.remove(process_group.leader) {
                // TODO(qsr): Handle signals ?
            }
            pids.remove_process_group(process_group.leader);
        }
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

            self.leave_process_group(&self.process_group.read());

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

    /// Returns whether |session| is the controlling session inside of |controlling_session|.
    fn is_controlling_session(
        &self,
        session: &Arc<Session>,
        controlling_session: &Option<ControllingSession>,
    ) -> bool {
        controlling_session
            .as_ref()
            .map(|cs| cs.session.upgrade().as_ref() == Some(session))
            .unwrap_or(false)
    }

    pub fn get_foreground_process_group(
        &self,
        terminal: &Arc<Terminal>,
        is_main: bool,
    ) -> Result<pid_t, Errno> {
        let process_group = self.process_group.read();
        let controlling_session = terminal.get_controlling_session(is_main);

        // "When fd does not refer to the controlling terminal of the calling
        // process, -1 is returned" - tcgetpgrp(3)
        if !self.is_controlling_session(&process_group.session, &controlling_session) {
            return error!(ENOTTY);
        }
        Ok(controlling_session.as_ref().unwrap().foregound_process_group)
    }

    pub fn set_foreground_process_group(
        &self,
        terminal: &Arc<Terminal>,
        is_main: bool,
        pgid: pid_t,
    ) -> Result<(), Errno> {
        // Keep locks to ensure atomicity.
        let process_group = self.process_group.read();
        let mut controlling_session = terminal.get_controlling_session_mut(is_main);

        // TODO(qsr): Handle SIGTTOU

        // tty must be the controlling terminal.
        if !self.is_controlling_session(&process_group.session, &controlling_session) {
            return error!(ENOTTY);
        }

        // pgid must be positive.
        if pgid < 0 {
            return error!(EINVAL);
        }

        let new_process_group = self.kernel.pids.read().get_process_group(pgid).ok_or(ESRCH)?;
        if new_process_group.session != process_group.session {
            return error!(EPERM);
        }

        *controlling_session =
            controlling_session.as_ref().unwrap().set_foregound_process_group(pgid);
        Ok(())
    }

    pub fn set_controlling_terminal(
        &self,
        current_task: &CurrentTask,
        terminal: &Arc<Terminal>,
        is_main: bool,
        steal: bool,
        is_readable: bool,
    ) -> Result<(), Errno> {
        // Keep locks to ensure atomicity.
        let process_group = self.process_group.read();
        let mut controlling_terminal = process_group.session.controlling_terminal.write();
        let mut controlling_session = terminal.get_controlling_session_mut(is_main);

        // "The calling process must be a session leader and not have a
        // controlling terminal already." - tty_ioctl(4)
        if process_group.session.leader != self.leader || controlling_terminal.is_some() {
            return error!(EINVAL);
        }

        let has_admin = current_task.has_capability(CAP_SYS_ADMIN);

        // "If this terminal is already the controlling terminal of a different
        // session group, then the ioctl fails with EPERM, unless the caller
        // has the CAP_SYS_ADMIN capability and arg equals 1, in which case the
        // terminal is stolen, and all processes that had it as controlling
        // terminal lose it." - tty_ioctl(4)
        match &*controlling_session {
            Some(cs) => {
                if let Some(other_session) = cs.session.upgrade() {
                    if other_session != process_group.session {
                        if !has_admin || !steal {
                            return error!(EPERM);
                        }

                        // Steal the TTY away. Unlike TIOCNOTTY, don't send signals.
                        *other_session.controlling_terminal.write() = None;
                    }
                }
            }
            _ => {}
        }

        if !is_readable && !has_admin {
            return error!(EPERM);
        }

        *controlling_terminal = Some(ControllingTerminal::new(terminal.clone(), is_main));
        *controlling_session = ControllingSession::new(&process_group.session);
        Ok(())
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
            current_task.thread_group.process_group.read().session.leader,
            child_task.thread_group.process_group.read().session.leader
        );

        let old_process_group = child_task.thread_group.process_group.read().clone();
        assert_eq!(child_task.thread_group.setsid(), Ok(()));
        assert_eq!(
            child_task.thread_group.process_group.read().session.leader,
            child_task.get_pid()
        );
        assert!(!old_process_group.thread_groups.read().contains(&child_task.thread_group.leader));
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
        assert_eq!(child_task1.thread_group.process_group.read().session.leader, current_task.id);
        assert_eq!(child_task1.thread_group.process_group.read().leader, child_task1.id);

        let old_process_group = child_task2.thread_group.process_group.read().clone();
        assert_eq!(current_task.thread_group.setpgid(&child_task2, child_task1.id), Ok(()));
        assert_eq!(child_task2.thread_group.process_group.read().leader, child_task1.id);
        assert!(!old_process_group.thread_groups.read().contains(&child_task2.thread_group.leader));
    }
}

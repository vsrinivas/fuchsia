// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon::sys;
use fuchsia_zircon::{self as zx, AsHandleRef};
use parking_lot::{RwLock, RwLockReadGuard, RwLockWriteGuard};
use std::collections::HashSet;
use std::ffi::CStr;
use std::sync::Arc;

use crate::auth::Credentials;
use crate::device::terminal::*;
use crate::signals::syscalls::WaitingOptions;
use crate::signals::*;
use crate::task::*;
use crate::types::*;

/// The mutable state of the ThreadGroup.
pub struct ThreadGroupMutableState {
    /// The identifier of the ThreadGroup. This value is duplicated from ThreadGroup itself because
    /// it is necessary for most of the internal operations.
    leader: pid_t,

    /// The parent thread group.
    ///
    /// The value needs to be writable so that it can be re-parent to the correct subreaper is the
    /// parent ends before the child.
    pub parent: pid_t,

    /// The tasks in the thread group.
    pub tasks: HashSet<pid_t>,

    /// The children of this thread group.
    pub children: HashSet<pid_t>,

    /// The IDs used to perform shell job control.
    pub process_group: Arc<ProcessGroup>,

    /// The itimers for this thread group.
    pub itimers: [itimerval; 3],

    /// WaitQueue for updates to the zombie_children lists of tasks in this group.
    /// Child tasks that have exited, but not yet been waited for.
    pub zombie_children: Vec<ZombieProcess>,

    pub did_exec: bool,

    /// Whether the process is currently stopped.
    pub stopped: bool,

    /// Whether the process is currently waitable via waitid and waitpid for either WSTOPPED or
    /// WCONTINUED, depending on the value of `stopped`. If not None, contains the SignalInfo to
    /// return.
    pub waitable: Option<SignalInfo>,

    pub zombie_leader: Option<ZombieProcess>,

    pub terminating: bool,
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

    /// The signal actions that are registered for this process.
    pub signal_actions: Arc<SignalActions>,

    /// The mutable state of the ThreadGroup.
    mutable_state: RwLock<ThreadGroupMutableState>,
}

impl PartialEq for ThreadGroup {
    fn eq(&self, other: &Self) -> bool {
        self.leader == other.leader
    }
}

/// A selector that can match a process. Works as a representation of the pid argument to syscalls
/// like wait and kill.
#[derive(Debug, Clone, Copy)]
pub enum ProcessSelector {
    /// Matches any process at all.
    Any,
    /// Matches only the process with the specified pid
    Pid(pid_t),
    /// Matches all the processes in the given process group
    Pgid(pid_t),
}

#[derive(Clone, Debug, PartialEq)]
pub struct ZombieProcess {
    pub pid: pid_t,
    pub pgid: pid_t,
    pub uid: uid_t,
    pub exit_status: ExitStatus,
}

impl ZombieProcess {
    pub fn new(
        thread_group: &ThreadGroupMutableState,
        credentials: &Credentials,
        exit_status: ExitStatus,
    ) -> Self {
        ZombieProcess {
            pid: thread_group.leader,
            pgid: thread_group.process_group.leader,
            uid: credentials.uid,
            exit_status,
        }
    }

    pub fn as_signal_info(&self) -> SignalInfo {
        match &self.exit_status {
            ExitStatus::Exit(_) => SignalInfo::new(
                SIGCHLD,
                CLD_EXITED,
                SignalDetail::SigChld {
                    pid: self.pid,
                    uid: self.uid,
                    status: self.exit_status.wait_status(),
                },
            ),
            ExitStatus::Kill(si)
            | ExitStatus::CoreDump(si)
            | ExitStatus::Stop(si)
            | ExitStatus::Continue(si) => si.clone(),
        }
    }
}

/// Return value of `get_waitable_child`/
pub enum WaitableChild {
    /// The given child matches the given option.
    Available(ZombieProcess),
    /// No child currently matches the given option, but some child may in the future.
    Pending,
    /// No child matches the given option, nor may in the future.
    NotFound,
}

impl ThreadGroup {
    pub fn new(
        kernel: Arc<Kernel>,
        process: zx::Process,
        parent: Option<&ThreadGroup>,
        leader: pid_t,
        process_group: Arc<ProcessGroup>,
        signal_actions: Arc<SignalActions>,
    ) -> ThreadGroup {
        let mut tasks = HashSet::new();
        tasks.insert(leader);
        process_group.thread_groups.write().insert(leader);

        let parent_id = if let Some(parent_tg) = parent {
            parent_tg.write().children.insert(leader);
            parent_tg.leader
        } else {
            leader
        };

        ThreadGroup {
            kernel,
            process,
            leader,
            signal_actions,
            mutable_state: RwLock::new(ThreadGroupMutableState {
                leader,
                parent: parent_id,
                tasks: tasks,
                children: HashSet::new(),
                process_group: process_group,
                itimers: Default::default(),
                zombie_children: vec![],
                did_exec: false,
                stopped: false,
                waitable: None,
                zombie_leader: None,
                terminating: false,
            }),
        }
    }

    /// Access mutable state with a read lock.
    pub fn read(&self) -> RwLockReadGuard<'_, ThreadGroupMutableState> {
        self.mutable_state.read()
    }

    /// Access mutable state with a write lock.
    fn write(&self) -> RwLockWriteGuard<'_, ThreadGroupMutableState> {
        self.mutable_state.write()
    }

    pub fn exit(&self, exit_status: ExitStatus) {
        let pids = self.kernel.pids.read();
        let mut state = self.write();
        if state.terminating {
            // The thread group is already terminating and all threads in the thread group have
            // already been interrupted.
            return;
        }
        state.terminating = true;

        // Interrupt each task.
        for tid in &state.tasks {
            if let Some(task) = pids.get_task(*tid) {
                *task.exit_status.lock() = Some(exit_status.clone());
                task.interrupt(InterruptionType::Exit);
            }
        }
    }

    pub fn add(&self, task: &Task) -> Result<(), Errno> {
        let mut state = self.write();
        if state.terminating {
            return error!(EINVAL);
        }
        state.tasks.insert(task.id);
        Ok(())
    }

    pub fn remove(&self, task: &Arc<Task>) {
        let mut pids = self.kernel.pids.write();
        let mut state = self.write();
        if state.remove_internal(task, &mut pids) {
            state.terminating = true;

            // Because of lock ordering, one cannot get a lock to the state of the children of this
            // thread group while having a lock of the thread group itself. Instead, the set of
            // children will be computed here, the lock will be dropped and the children will be
            // passed to the reaper that will capture the state in the correct order.
            let children = state
                .children
                .iter()
                .flat_map(|pid| pids.get_thread_group(*pid))
                .collect::<Vec<_>>();
            std::mem::drop(state);
            let children_state = children.iter().map(|c| c.write()).collect::<Vec<_>>();
            self.write().remove(children_state, &mut pids);
        }
    }

    pub fn setsid(&self) -> Result<(), Errno> {
        let mut pids = self.kernel.pids.write();
        if !pids.get_process_group(self.leader).is_none() {
            return error!(EPERM);
        }
        let process_group = ProcessGroup::new(Session::new(self.leader), self.leader);
        pids.add_process_group(&process_group);
        self.write().set_process_group(process_group, &mut pids);

        Ok(())
    }

    pub fn setpgid(&self, target: &Task, pgid: pid_t) -> Result<(), Errno> {
        let mut pids = self.kernel.pids.write();

        // The target process must be either the current process of a child of the current process
        let mut target_thread_group = target.thread_group.write();
        let is_target_current_process_child =
            target_thread_group.leader != self.leader && target_thread_group.parent == self.leader;
        if target_thread_group.leader != self.leader && !is_target_current_process_child {
            return error!(ESRCH);
        }

        // If the target process is a child of the current task, it must not have executed one of the exec
        // function.
        if is_target_current_process_child && target_thread_group.did_exec {
            return error!(EACCES);
        }

        let new_process_group;
        {
            let current_process_group = if is_target_current_process_child {
                Arc::clone(&self.read().process_group)
            } else {
                Arc::clone(&target_thread_group.process_group)
            };
            let target_process_group = &target_thread_group.process_group;

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

        target_thread_group.set_process_group(new_process_group, &mut pids);

        Ok(())
    }

    pub fn set_itimer(&self, which: u32, value: itimerval) -> Result<itimerval, Errno> {
        let mut state = self.write();
        let timer = state.itimers.get_mut(which as usize).ok_or(errno!(EINVAL))?;
        let old_value = *timer;
        *timer = value;
        Ok(old_value)
    }

    /// Set the stop status of the process.
    pub fn set_stopped(&self, stopped: bool, siginfo: SignalInfo) {
        let pids = self.kernel.pids.read();
        let mut state = self.write();
        if stopped != state.stopped {
            // TODO(qsr): When task can be running_status inside user code, task will need to be
            // either restarted or running_status here.
            state.stopped = stopped;
            state.waitable = Some(siginfo);
            if !stopped {
                state.interrupt(InterruptionType::Continue, &pids);
            }
            if let Some(parent) = state.get_parent(&pids) {
                parent.read().interrupt(InterruptionType::ChildChange, &pids);
            }
        }
    }

    /// Mark the process as having run exec and sets the name of process associated with this
    /// thread group.
    ///
    /// - `name`: The name to set for the process. If the length of `name` is >= `ZX_MAX_NAME_LEN`,
    /// a truncated version of the name will be used.
    pub fn mark_executed(&self, name: &CStr) -> Result<(), Errno> {
        let mut state = self.write();
        state.did_exec = true;
        if name.to_bytes().len() >= sys::ZX_MAX_NAME_LEN {
            // TODO: Might want to use [..sys::ZX_MAX_NAME_LEN] of only the last path component.
            #[allow(clippy::clone_double_ref)] // TODO(fxbug.dev/95057)
            let mut clone = name.to_owned().into_bytes();
            clone[sys::ZX_MAX_NAME_LEN - 1] = 0;
            let name = CStr::from_bytes_with_nul(&clone[..sys::ZX_MAX_NAME_LEN])
                .map_err(|_| errno!(EINVAL))?;
            self.process.set_name(name).map_err(|status| from_status_like_fdio!(status))
        } else {
            self.process.set_name(name).map_err(|status| from_status_like_fdio!(status))
        }
    }

    /// Returns any waitable child matching the given `selector` and `options`.
    ///
    ///Will remove the waitable status from the child depending on `options`.
    pub fn get_waitable_child(
        &self,
        selector: ProcessSelector,
        options: &WaitingOptions,
    ) -> Result<WaitableChild, Errno> {
        let pids = self.kernel.pids.read();
        // Built a list of mutable child state before acquire a write lock to the state of this
        // object because lock ordering imposes the child lock is acquired before the parent.
        let children = self
            .read()
            .children
            .iter()
            .flat_map(|pid| pids.get_thread_group(*pid))
            .collect::<Vec<_>>();
        let children_state = children.iter().map(|c| c.write()).collect::<Vec<_>>();
        self.write().get_waitable_child(children_state, selector, options, &pids)
    }

    /// Returns whether |session| is the controlling session inside of |controlling_session|.
    fn is_controlling_session(
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
        let state = self.read();
        let process_group = &state.process_group;
        let controlling_session = terminal.get_controlling_session(is_main);

        // "When fd does not refer to the controlling terminal of the calling
        // process, -1 is returned" - tcgetpgrp(3)
        if !Self::is_controlling_session(&process_group.session, &controlling_session) {
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
        let state = self.read();
        let process_group = &state.process_group;
        let mut controlling_session = terminal.get_controlling_session_mut(is_main);

        // TODO(qsr): Handle SIGTTOU

        // tty must be the controlling terminal.
        if !Self::is_controlling_session(&process_group.session, &controlling_session) {
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
        let state = self.read();
        let process_group = &state.process_group;
        let mut controlling_terminal = process_group.session.controlling_terminal.write();
        let mut controlling_session = terminal.get_controlling_session_mut(is_main);

        // "The calling process must be a session leader and not have a
        // controlling terminal already." - tty_ioctl(4)
        if process_group.session.leader != self.leader || controlling_terminal.is_some() {
            return error!(EINVAL);
        }

        let has_admin = current_task.creds.read().has_capability(CAP_SYS_ADMIN);

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

    pub fn release_controlling_terminal(
        &self,
        _current_task: &CurrentTask,
        terminal: &Arc<Terminal>,
        is_main: bool,
    ) -> Result<(), Errno> {
        // Keep locks to ensure atomicity.
        let state = self.read();
        let process_group = &state.process_group;
        let mut controlling_terminal = process_group.session.controlling_terminal.write();
        let mut controlling_session = terminal.get_controlling_session_mut(is_main);

        // tty must be the controlling terminal.
        if !Self::is_controlling_session(&process_group.session, &controlling_session) {
            return error!(ENOTTY);
        }

        // "If the process was session leader, then send SIGHUP and SIGCONT to the foreground
        // process group and all processes in the current session lose their controlling terminal."
        // - tty_ioctl(4)

        // Remove tty as the controlling tty for each process in the session, then
        // send them SIGHUP and SIGCONT.

        let _foreground_process_group_id =
            controlling_session.as_ref().unwrap().foregound_process_group;
        *controlling_terminal = None;
        *controlling_session = None;

        if process_group.session.leader == self.leader {
            // TODO(qsr): If the process is the session leader, send signals.
        }

        Ok(())
    }
}

impl ThreadGroupMutableState {
    /// Returns the parent of the process, if it exists.
    pub fn get_parent(&self, pids: &PidTable) -> Option<Arc<ThreadGroup>> {
        if self.leader == self.parent {
            None
        } else {
            pids.get_thread_group(self.parent)
        }
    }

    pub fn set_process_group(&mut self, process_group: Arc<ProcessGroup>, pids: &mut PidTable) {
        if self.process_group == process_group {
            return;
        }
        self.leave_process_group(pids);
        self.process_group = process_group;
        self.process_group.thread_groups.write().insert(self.leader);
    }

    pub fn leave_process_group(&self, pids: &mut PidTable) {
        if self.process_group.remove(self.leader) {
            if self.process_group.session.remove(self.process_group.leader) {
                // TODO(qsr): Handle signals ?
            }
            pids.remove_process_group(self.process_group.leader);
        }
    }

    pub fn remove_internal(&mut self, task: &Arc<Task>, pids: &mut PidTable) -> bool {
        pids.remove_task(task.id);
        self.tasks.remove(&task.id);

        if task.id == self.leader {
            let exit_status = task.exit_status.lock().clone();
            #[cfg(not(test))]
            let exit_status =
                exit_status.expect("a process should not be exiting without an exit status");
            #[cfg(test)]
            let exit_status = exit_status.unwrap_or(ExitStatus::Exit(0));
            self.zombie_leader = Some(ZombieProcess {
                pid: self.leader,
                pgid: self.process_group.leader,
                uid: task.creds.read().uid,
                exit_status,
            });
        }

        self.tasks.is_empty()
    }

    /// Returns any waitable child matching the given `selector` and `options`.
    ///
    ///Will remove the waitable status from the child depending on `options`.
    pub fn get_waitable_child(
        &mut self,
        children: Vec<RwLockWriteGuard<'_, ThreadGroupMutableState>>,
        selector: ProcessSelector,
        options: &WaitingOptions,
        pids: &PidTable,
    ) -> Result<WaitableChild, Errno> {
        if options.wait_for_exited {
            if let Some(child) = match selector {
                ProcessSelector::Any => {
                    if self.zombie_children.len() > 0 {
                        Some(self.zombie_children.len() - 1)
                    } else {
                        None
                    }
                }
                ProcessSelector::Pgid(pid) => {
                    self.zombie_children.iter().position(|zombie| zombie.pgid == pid)
                }
                ProcessSelector::Pid(pid) => {
                    self.zombie_children.iter().position(|zombie| zombie.pid == pid)
                }
            }
            .map(|pos| {
                if options.keep_waitable_state {
                    self.zombie_children[pos].clone()
                } else {
                    self.zombie_children.remove(pos)
                }
            }) {
                return Ok(WaitableChild::Available(child));
            }
        }

        // The vector of potential matches.
        let children_filter = |child: &RwLockWriteGuard<'_, ThreadGroupMutableState>| match selector
        {
            ProcessSelector::Any => true,
            ProcessSelector::Pid(pid) => child.leader == pid,
            ProcessSelector::Pgid(pgid) => {
                pids.get_process_group(pgid).as_ref() == Some(&child.process_group)
            }
        };

        let mut selected_children = children.into_iter().filter(children_filter).peekable();
        if selected_children.peek().is_none() {
            return Ok(WaitableChild::NotFound);
        }
        for mut child in selected_children {
            if child.waitable.is_some() {
                if !child.stopped && options.wait_for_continued {
                    let siginfo = if options.keep_waitable_state {
                        child.waitable.clone().unwrap()
                    } else {
                        child.waitable.take().unwrap()
                    };
                    return Ok(WaitableChild::Available(ZombieProcess::new(
                        &child,
                        &child.get_task(&pids)?.creds.read(),
                        ExitStatus::Continue(siginfo),
                    )));
                }
                if child.stopped && options.wait_for_stopped {
                    let siginfo = if options.keep_waitable_state {
                        child.waitable.clone().unwrap()
                    } else {
                        child.waitable.take().unwrap()
                    };
                    return Ok(WaitableChild::Available(ZombieProcess::new(
                        &child,
                        &child.get_task(&pids)?.creds.read(),
                        ExitStatus::Stop(siginfo),
                    )));
                }
            }
        }

        Ok(WaitableChild::Pending)
    }

    pub fn adopt_children(
        &mut self,
        other: &mut ThreadGroupMutableState,
        children: Vec<RwLockWriteGuard<'_, ThreadGroupMutableState>>,
        pids: &PidTable,
    ) {
        // TODO(qsr): Implement PR_SET_CHILD_SUBREAPER

        // If parent != self, reparent.
        if let Some(parent) = self.get_parent(pids) {
            parent.write().adopt_children(other, children, pids);
        } else {
            // Else, act like init.
            for mut child in children.into_iter() {
                child.parent = self.leader;
                self.children.insert(child.leader);
            }

            other.children.clear();
            self.zombie_children.append(&mut other.zombie_children);
        }
    }

    pub fn remove(
        &mut self,
        children: Vec<RwLockWriteGuard<'_, ThreadGroupMutableState>>,
        pids: &mut PidTable,
    ) {
        let parent_opt = self.get_parent(pids);

        // Before unregistering this object from other places, register the zombie.
        if let Some(parent) = parent_opt.as_ref() {
            let mut parent_writer = parent.write();
            // Reparent the children.
            parent_writer.adopt_children(self, children, &pids);

            let zombie = self.zombie_leader.take().expect("Failed to capture zombie leader.");

            parent_writer.children.remove(&self.leader);
            parent_writer.zombie_children.push(zombie);
        }
        pids.remove_thread_group(self.leader);

        // Unregister this object.
        self.leave_process_group(pids);

        // Send signals
        if let Some(parent) = parent_opt.as_ref() {
            // TODO: Should this be zombie_leader.exit_signal?
            if let Some(signal_target) = get_signal_target(&parent, &SIGCHLD.into(), &pids) {
                send_signal(&signal_target, SignalInfo::default(SIGCHLD));
            }
            parent.read().interrupt(InterruptionType::ChildChange, &pids);
        }

        // TODO: Set the error_code on the Zircon process object. Currently missing a way
        // to do this in Zircon. Might be easier in the new execution model.

        // Once the last zircon thread stops, the zircon process will also stop executing.
    }

    /// Returns a task in the current thread group.
    pub fn get_task(&self, pids: &PidTable) -> Result<Arc<Task>, Errno> {
        if let Some(task) = pids.get_task(self.leader) {
            return Ok(task);
        }
        for pid in &self.children {
            if let Some(task) = pids.get_task(*pid) {
                return Ok(task);
            }
        }
        error!(ESRCH)
    }

    /// Interrupt the thread group.
    ///
    /// This will interrupt every task in the thread group.
    pub fn interrupt(&self, interruption_type: InterruptionType, pids: &PidTable) {
        for pid in self.tasks.iter() {
            if let Some(task) = pids.get_task(*pid) {
                task.interrupt(interruption_type);
            }
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

        assert!(current_task.thread_group.mark_executed(&name).is_ok());
        assert_eq!(current_task.thread_group.process.get_name(), Ok(expected_name));
    }

    #[::fuchsia::test]
    fn test_max_length_name() {
        let (_kernel, current_task) = create_kernel_and_task();
        let bytes = [1; sys::ZX_MAX_NAME_LEN - 1];
        let name = CString::new(bytes).unwrap();

        assert!(current_task.thread_group.mark_executed(&name).is_ok());
        assert_eq!(current_task.thread_group.process.get_name(), Ok(name));
    }

    #[::fuchsia::test]
    fn test_short_name() {
        let (_kernel, current_task) = create_kernel_and_task();
        let bytes = [1; sys::ZX_MAX_NAME_LEN - 10];
        let name = CString::new(bytes).unwrap();

        assert!(current_task.thread_group.mark_executed(&name).is_ok());
        assert_eq!(current_task.thread_group.process.get_name(), Ok(name));
    }

    #[::fuchsia::test]
    fn test_setsid() {
        let (_kernel, current_task) = create_kernel_and_task();
        assert_eq!(current_task.thread_group.setsid(), error!(EPERM));

        let child_task = current_task.clone_task_for_test(0);
        assert_eq!(
            current_task.thread_group.read().process_group.session.leader,
            child_task.thread_group.read().process_group.session.leader
        );

        let old_process_group = child_task.thread_group.read().process_group.clone();
        assert_eq!(child_task.thread_group.setsid(), Ok(()));
        assert_eq!(
            child_task.thread_group.read().process_group.session.leader,
            child_task.get_pid()
        );
        assert!(!old_process_group.thread_groups.read().contains(&child_task.thread_group.leader));
    }

    #[::fuchsia::test]
    fn test_exit_status() {
        let (_kernel, current_task) = create_kernel_and_task();
        let child = current_task.clone_task_for_test(0);
        child.thread_group.exit(ExitStatus::Exit(42));
        std::mem::drop(child);
        assert_eq!(
            current_task.thread_group.read().zombie_children[0].exit_status,
            ExitStatus::Exit(42)
        );
    }

    #[::fuchsia::test]
    fn test_setgpid() {
        let (_kernel, current_task) = create_kernel_and_task();
        assert_eq!(current_task.thread_group.setsid(), error!(EPERM));

        let child_task1 = current_task.clone_task_for_test(0);
        let child_task2 = current_task.clone_task_for_test(0);
        let execd_child_task = current_task.clone_task_for_test(0);
        execd_child_task.thread_group.write().did_exec = true;
        let other_session_child_task = current_task.clone_task_for_test(0);
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
        assert_eq!(child_task1.thread_group.read().process_group.session.leader, current_task.id);
        assert_eq!(child_task1.thread_group.read().process_group.leader, child_task1.id);

        let old_process_group = child_task2.thread_group.read().process_group.clone();
        assert_eq!(current_task.thread_group.setpgid(&child_task2, child_task1.id), Ok(()));
        assert_eq!(child_task2.thread_group.read().process_group.leader, child_task1.id);
        assert!(!old_process_group.thread_groups.read().contains(&child_task2.thread_group.leader));
    }

    #[::fuchsia::test]
    fn test_adopt_children() {
        let (_kernel, current_task) = create_kernel_and_task();
        let task1 = current_task
            .clone_task(
                0,
                UserRef::new(UserAddress::default()),
                UserRef::new(UserAddress::default()),
            )
            .expect("clone process");
        let task2 = task1
            .clone_task(
                0,
                UserRef::new(UserAddress::default()),
                UserRef::new(UserAddress::default()),
            )
            .expect("clone process");
        let task3 = task2
            .clone_task(
                0,
                UserRef::new(UserAddress::default()),
                UserRef::new(UserAddress::default()),
            )
            .expect("clone process");

        assert_eq!(task3.thread_group.read().parent, task2.id);

        task2.thread_group.exit(ExitStatus::Exit(0));
        std::mem::drop(task2);

        // Task3 parent should be current_task.
        assert_eq!(task3.thread_group.read().parent, current_task.id);
    }
}

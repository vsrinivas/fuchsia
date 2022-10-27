// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon as zx;
use itertools::Itertools;
use std::collections::BTreeMap;
use std::fmt;
use std::sync::{Arc, Weak};

use crate::auth::Credentials;
use crate::device::terminal::*;
use crate::lock::RwLock;
use crate::mutable_state::*;
use crate::signals::syscalls::WaitingOptions;
use crate::signals::*;
use crate::task::*;
use crate::types::*;

/// The mutable state of the ThreadGroup.
pub struct ThreadGroupMutableState {
    /// The parent thread group.
    ///
    /// The value needs to be writable so that it can be re-parent to the correct subreaper if the
    /// parent ends before the child.
    pub parent: Option<Arc<ThreadGroup>>,

    /// The tasks in the thread group.
    ///
    /// The references to Task is weak to prevent cycles as Task have a Arc reference to their
    /// thread group.
    /// It is still expected that these weak references are always valid, as tasks must unregister
    /// themselves before they are deleted.
    pub tasks: BTreeMap<pid_t, Weak<Task>>,

    /// The children of this thread group.
    ///
    /// The references to ThreadGroup is weak to prevent cycles as ThreadGroup have a Arc reference
    /// to their parent.
    /// It is still expected that these weak references are always valid, as thread groups must unregister
    /// themselves before they are deleted.
    pub children: BTreeMap<pid_t, Weak<ThreadGroup>>,

    /// Child tasks that have exited, but not yet been waited for.
    ///
    /// TODO(tbodt): Storing zombies this way is problematic since zombies take up space in the pid
    /// table and prevent their pid from being reallocated. We should eventually stop using
    /// ZombieProcess for returning waitpid() results and instead put the information in the pid
    /// table.
    pub zombie_children: Vec<ZombieProcess>,

    /// WaitQueue for updates to the WaitResults of tasks in this group.
    pub child_status_waiters: WaitQueue,

    /// Whether this thread group will inherit from children of dying processes in its descendant
    /// tree.
    pub is_child_subreaper: bool,

    /// The IDs used to perform shell job control.
    pub process_group: Arc<ProcessGroup>,

    /// The itimers for this thread group.
    pub itimers: [itimerval; 3],

    pub did_exec: bool,

    /// Whether the process is currently stopped.
    pub stopped: bool,

    /// Wait queue for updates to `stopped`.
    pub stopped_waiters: WaitQueue,

    /// Whether the process is currently waitable via waitid and waitpid for either WSTOPPED or
    /// WCONTINUED, depending on the value of `stopped`. If not None, contains the SignalInfo to
    /// return.
    pub waitable: Option<SignalInfo>,

    pub zombie_leader: Option<ZombieProcess>,

    pub terminating: bool,

    /// The priority of the process, a value between 1 and 40 (inclusive). Higher value means
    /// higher priority. Defaults to 20.
    pub priority: u8,
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

impl fmt::Debug for ThreadGroup {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", self.leader)
    }
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

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ZombieProcess {
    pub pid: pid_t,
    pub pgid: pid_t,
    pub uid: uid_t,
    pub exit_status: ExitStatus,
}

impl ZombieProcess {
    pub fn new(
        thread_group: ThreadGroupStateRef<'_>,
        credentials: &Credentials,
        exit_status: ExitStatus,
    ) -> Self {
        ZombieProcess {
            pid: thread_group.base.leader,
            pgid: thread_group.process_group.leader,
            uid: credentials.uid,
            exit_status,
        }
    }

    pub fn as_signal_info(&self) -> SignalInfo {
        SignalInfo::new(
            SIGCHLD,
            self.exit_status.signal_info_code(),
            SignalDetail::SigChld {
                pid: self.pid,
                uid: self.uid,
                status: self.exit_status.signal_info_status(),
            },
        )
    }
}

impl ThreadGroup {
    pub fn new(
        kernel: Arc<Kernel>,
        process: zx::Process,
        parent: Option<ThreadGroupWriteGuard<'_>>,
        leader: pid_t,
        process_group: Arc<ProcessGroup>,
        signal_actions: Arc<SignalActions>,
    ) -> Arc<ThreadGroup> {
        let thread_group = Arc::new(ThreadGroup {
            kernel,
            process,
            leader,
            signal_actions,
            mutable_state: RwLock::new(ThreadGroupMutableState {
                parent: parent.as_ref().map(|p| Arc::clone(p.base)),
                tasks: BTreeMap::new(),
                children: BTreeMap::new(),
                zombie_children: vec![],
                child_status_waiters: WaitQueue::default(),
                is_child_subreaper: false,
                process_group: Arc::clone(&process_group),
                itimers: Default::default(),
                did_exec: false,
                stopped: false,
                stopped_waiters: WaitQueue::default(),
                waitable: None,
                zombie_leader: None,
                terminating: false,
                priority: 20,
            }),
        });

        if let Some(mut parent) = parent {
            parent.children.insert(leader, Arc::downgrade(&thread_group));
            process_group.insert(&thread_group);
        }
        thread_group
    }

    state_accessor!(ThreadGroup, mutable_state);

    pub fn exit(self: &Arc<Self>, exit_status: ExitStatus) {
        let mut state = self.write();
        if state.terminating {
            // The thread group is already terminating and all threads in the thread group have
            // already been interrupted.
            return;
        }
        state.terminating = true;

        // Interrupt each task. Unlock the group because send_signal will lock the group in order
        // to call set_stopped.
        let tasks = state.tasks().collect::<Vec<_>>();
        drop(state);
        for task in tasks {
            task.write().exit_status = Some(exit_status.clone());
            send_signal(&task, SignalInfo::default(SIGKILL));
        }
    }

    pub fn add(self: &Arc<Self>, task: &Arc<Task>) -> Result<(), Errno> {
        let mut state = self.write();
        if state.terminating {
            return error!(EINVAL);
        }
        state.tasks.insert(task.id, Arc::downgrade(task));
        Ok(())
    }

    pub fn remove(self: &Arc<Self>, task: &Arc<Task>) {
        let mut pids = self.kernel.pids.write();
        let mut state = self.write();

        state.tasks.remove(&task.id);
        pids.remove_task(task.id);

        if task.id == state.leader() {
            let exit_status = task.read().exit_status.clone().unwrap_or_else(|| {
                tracing::error!("Process {:?} exiting without an exit code.", task.id);
                ExitStatus::Exit(u8::MAX)
            });
            state.zombie_leader = Some(ZombieProcess {
                pid: state.leader(),
                pgid: state.process_group.leader,
                uid: task.creds().uid,
                exit_status,
            });
        }

        if state.tasks.is_empty() {
            state.terminating = true;

            // Unregister this object.
            pids.remove_thread_group(state.leader());
            state.leave_process_group(&mut pids);

            // I have no idea if dropping the lock here is correct, and I don't want to think about
            // it. If problems do turn up with another thread observing an intermediate state of
            // this exit operation, the solution is to unify locks. It should be sensible and
            // possible for there to be a single lock that protects all (or nearly all) of the
            // data accessed by both exit and wait. In gvisor and linux this is the lock on the
            // equivalent of the PidTable. This is made more difficult by rust locks being
            // containers that only lock the data they contain, but see
            // https://docs.google.com/document/d/1YHrhBqNhU1WcrsYgGAu3JwwlVmFXPlwWHTJLAbwRebY/edit
            // for an idea.
            std::mem::drop(state);

            // We will need the immediate parent and the reaper. Once we have them, we can make
            // sure to take the locks in the right order: parent before child.
            let parent = self.read().parent.clone();
            let reaper = self.find_reaper();

            // Reparent the children.
            if let Some(reaper) = reaper {
                let mut reaper = reaper.write();
                let mut state = self.write();
                for (_pid, child) in std::mem::take(&mut state.children) {
                    if let Some(child) = child.upgrade() {
                        child.write().parent = Some(Arc::clone(reaper.base));
                        reaper.children.insert(child.leader, Arc::downgrade(&child));
                    }
                }
                reaper.zombie_children.append(&mut state.zombie_children);
            }

            if let Some(ref parent) = parent {
                let mut parent = parent.write();
                let mut state = self.write();
                let zombie = state.zombie_leader.take().expect("Failed to capture zombie leader.");

                let signal_info = zombie.as_signal_info();
                parent.children.remove(&state.leader());
                parent.zombie_children.push(zombie);

                // Send signals
                // TODO: This should be the designated exit_signal of the leader
                if let Some(signal_target) = parent.get_signal_target(&SIGCHLD.into()) {
                    send_signal(&signal_target, signal_info);
                }
                parent.child_status_waiters.notify_all();
            }

            // TODO: Set the error_code on the Zircon process object. Currently missing a way
            // to do this in Zircon. Might be easier in the new execution model.

            // Once the last zircon thread stops, the zircon process will also stop executing.

            if let Some(parent) = parent {
                parent.check_orphans();
            }
        }
    }

    /// Find the task which will adopt our children after we die.
    fn find_reaper(self: &Arc<Self>) -> Option<Arc<Self>> {
        let mut parent = Arc::clone(self.read().parent.as_ref()?);
        loop {
            parent = {
                let parent_state = parent.read();
                if parent_state.is_child_subreaper {
                    break;
                }
                match parent_state.parent {
                    Some(ref next_parent) => Arc::clone(next_parent),
                    None => break,
                }
            }
        }
        Some(parent)
    }

    pub fn setsid(self: &Arc<Self>) -> Result<(), Errno> {
        {
            let mut pids = self.kernel.pids.write();
            if pids.get_process_group(self.leader).is_some() {
                return error!(EPERM);
            }
            let process_group = ProcessGroup::new(self.leader, None);
            pids.add_process_group(&process_group);
            self.write().set_process_group(process_group, &mut pids);
        }
        self.check_orphans();

        Ok(())
    }

    pub fn setpgid(self: &Arc<Self>, target: &Task, pgid: pid_t) -> Result<(), Errno> {
        {
            let mut pids = self.kernel.pids.write();

            let current_process_group = Arc::clone(&self.read().process_group);

            // The target process must be either the current process of a child of the current process
            let mut target_thread_group = target.thread_group.write();
            let is_target_current_process_child =
                target_thread_group.parent.as_ref().map(|tg| tg.leader) == Some(self.leader);
            if target_thread_group.leader() != self.leader && !is_target_current_process_child {
                return error!(ESRCH);
            }

            // If the target process is a child of the current task, it must not have executed one of the exec
            // function.
            if is_target_current_process_child && target_thread_group.did_exec {
                return error!(EACCES);
            }

            let new_process_group;
            {
                let target_process_group = &target_thread_group.process_group;

                // The target process must not be a session leader and must be in the same session as the current process.
                if target_thread_group.leader() == target_process_group.session.leader
                    || current_process_group.session != target_process_group.session
                {
                    return error!(EPERM);
                }

                let target_pgid = if pgid == 0 { target_thread_group.leader() } else { pgid };
                if target_pgid < 0 {
                    return error!(EINVAL);
                }

                if target_pgid == target_process_group.leader {
                    return Ok(());
                }

                // If pgid is not equal to the target process id, the associated process group must exist
                // and be in the same session as the target process.
                if target_pgid != target_thread_group.leader() {
                    new_process_group =
                        pids.get_process_group(target_pgid).ok_or_else(|| errno!(EPERM))?;
                    if new_process_group.session != target_process_group.session {
                        return error!(EPERM);
                    }
                } else {
                    // Create a new process group
                    new_process_group =
                        ProcessGroup::new(target_pgid, Some(target_process_group.session.clone()));
                    pids.add_process_group(&new_process_group);
                }
            }

            target_thread_group.set_process_group(new_process_group, &mut pids);
        }
        target.thread_group.check_orphans();

        Ok(())
    }

    pub fn set_itimer(self: &Arc<Self>, which: u32, value: itimerval) -> Result<itimerval, Errno> {
        let mut state = self.write();
        let timer = state.itimers.get_mut(which as usize).ok_or_else(|| errno!(EINVAL))?;
        let old_value = *timer;
        *timer = value;
        Ok(old_value)
    }

    /// Set the stop status of the process.
    pub fn set_stopped(self: &Arc<Self>, stopped: bool, siginfo: SignalInfo) {
        let mut state = self.write();
        if stopped != state.stopped {
            // TODO(qsr): When task can be stopped inside user code, task will need to be
            // either restarted or stopped here.
            state.stopped = stopped;
            state.waitable = Some(siginfo);
            if !stopped {
                state.stopped_waiters.notify_all();
            }
            if let Some(parent) = state.parent.clone() {
                // OK to drop state here since we don't access it anymore.
                std::mem::drop(state);
                parent.write().child_status_waiters.notify_all();
            }
        }
    }

    /// Ensures |session| is the controlling session inside of |controlling_session|, and returns a
    /// reference to the |ControllingSession|.
    fn check_controlling_session<'a>(
        session: &Arc<Session>,
        controlling_session: &'a Option<ControllingSession>,
    ) -> Result<&'a ControllingSession, Errno> {
        if let Some(controlling_session) = controlling_session {
            if controlling_session.session.as_ptr() == Arc::as_ptr(session) {
                return Ok(controlling_session);
            }
        }
        error!(ENOTTY)
    }

    pub fn get_foreground_process_group(
        self: &Arc<Self>,
        terminal: &Arc<Terminal>,
        is_main: bool,
    ) -> Result<pid_t, Errno> {
        let state = self.read();
        let process_group = &state.process_group;
        let terminal_state = terminal.read();
        let controlling_session = terminal_state.get_controlling_session(is_main);

        // "When fd does not refer to the controlling terminal of the calling
        // process, -1 is returned" - tcgetpgrp(3)
        let cs = Self::check_controlling_session(&process_group.session, controlling_session)?;
        Ok(cs.foregound_process_group_leader)
    }

    pub fn set_foreground_process_group(
        self: &Arc<Self>,
        current_task: &CurrentTask,
        terminal: &Arc<Terminal>,
        is_main: bool,
        pgid: pid_t,
    ) -> Result<(), Errno> {
        let process_group;
        let send_ttou;
        {
            // Keep locks to ensure atomicity.
            let pids = self.kernel.pids.read();
            let state = self.read();
            process_group = Arc::clone(&state.process_group);
            let mut terminal_state = terminal.write();
            let controlling_session = terminal_state.get_controlling_session_mut(is_main);
            let cs = Self::check_controlling_session(&process_group.session, controlling_session)?;

            // pgid must be positive.
            if pgid < 0 {
                return error!(EINVAL);
            }

            let new_process_group = pids.get_process_group(pgid).ok_or_else(|| errno!(ESRCH))?;
            if new_process_group.session != process_group.session {
                return error!(EPERM);
            }

            // If the calling process is a member of a background group and not ignoring SIGTTOU, a
            // SIGTTOU signal is sent to all members of this background process group.
            send_ttou = process_group.leader != cs.foregound_process_group_leader
                && !SIGTTOU.is_in_set(current_task.read().signals.mask())
                && self.signal_actions.get(SIGTTOU).sa_handler != SIG_IGN;

            *controlling_session = controlling_session
                .as_ref()
                .unwrap()
                .set_foregound_process_group(&new_process_group);
        }

        // Locks must not be held when sending signals.
        if send_ttou {
            process_group.send_signals(&[SIGTTOU]);
        }

        Ok(())
    }

    pub fn set_controlling_terminal(
        self: &Arc<Self>,
        current_task: &CurrentTask,
        terminal: &Arc<Terminal>,
        is_main: bool,
        steal: bool,
        is_readable: bool,
    ) -> Result<(), Errno> {
        // Keep locks to ensure atomicity.
        let state = self.read();
        let process_group = &state.process_group;
        let mut terminal_state = terminal.write();
        let controlling_session = terminal_state.get_controlling_session_mut(is_main);
        let mut session_writer = process_group.session.write();

        // "The calling process must be a session leader and not have a
        // controlling terminal already." - tty_ioctl(4)
        if process_group.session.leader != self.leader
            || session_writer.controlling_terminal.is_some()
        {
            return error!(EINVAL);
        }

        let has_admin = current_task.creds().has_capability(CAP_SYS_ADMIN);

        // "If this terminal is already the controlling terminal of a different
        // session group, then the ioctl fails with EPERM, unless the caller
        // has the CAP_SYS_ADMIN capability and arg equals 1, in which case the
        // terminal is stolen, and all processes that had it as controlling
        // terminal lose it." - tty_ioctl(4)
        if let Some(other_session) =
            controlling_session.as_ref().and_then(|cs| cs.session.upgrade())
        {
            if other_session != process_group.session {
                if !has_admin || !steal {
                    return error!(EPERM);
                }

                // Steal the TTY away. Unlike TIOCNOTTY, don't send signals.
                other_session.write().controlling_terminal = None;
            }
        }

        if !is_readable && !has_admin {
            return error!(EPERM);
        }

        session_writer.controlling_terminal =
            Some(ControllingTerminal::new(terminal.clone(), is_main));
        *controlling_session = ControllingSession::new(process_group);
        Ok(())
    }

    pub fn release_controlling_terminal(
        self: &Arc<Self>,
        _current_task: &CurrentTask,
        terminal: &Arc<Terminal>,
        is_main: bool,
    ) -> Result<(), Errno> {
        let process_group;
        {
            // Keep locks to ensure atomicity.
            let state = self.read();
            process_group = Arc::clone(&state.process_group);
            let mut terminal_state = terminal.write();
            let controlling_session = terminal_state.get_controlling_session_mut(is_main);
            let mut session_writer = process_group.session.write();

            // tty must be the controlling terminal.
            Self::check_controlling_session(&process_group.session, controlling_session)?;

            // "If the process was session leader, then send SIGHUP and SIGCONT to the foreground
            // process group and all processes in the current session lose their controlling terminal."
            // - tty_ioctl(4)

            // Remove tty as the controlling tty for each process in the session, then
            // send them SIGHUP and SIGCONT.

            session_writer.controlling_terminal = None;
            *controlling_session = None;
        }

        if process_group.session.leader == self.leader {
            process_group.send_signals(&[SIGHUP, SIGCONT]);
        }

        Ok(())
    }

    fn check_orphans(self: &Arc<Self>) {
        let mut thread_groups = self.read().children().collect::<Vec<_>>();
        thread_groups.push(Arc::clone(self));
        let process_groups =
            thread_groups.iter().map(|tg| Arc::clone(&tg.read().process_group)).unique();
        for pg in process_groups {
            pg.check_orphaned();
        }
    }
}

state_implementation!(ThreadGroup, ThreadGroupMutableState, {
    pub fn leader(&self) -> pid_t {
        self.base.leader
    }

    pub fn children(&self) -> Box<dyn Iterator<Item = Arc<ThreadGroup>> + '_> {
        Box::new(self.children.values().map(|v| {
            v.upgrade().expect("Weak references to processes in ThreadGroup must always be valid")
        }))
    }

    pub fn tasks(&self) -> Box<dyn Iterator<Item = Arc<Task>> + '_> {
        Box::new(self.tasks.values().map(|v| {
            v.upgrade().expect("Weak references to task in ThreadGroup must always be valid")
        }))
    }

    pub fn get_ppid(&self) -> pid_t {
        match &self.parent {
            Some(parent) => parent.leader,
            None => self.leader(),
        }
    }

    fn set_process_group(&mut self, process_group: Arc<ProcessGroup>, pids: &mut PidTable) {
        if self.process_group == process_group {
            return;
        }
        self.leave_process_group(pids);
        self.process_group = process_group;
        self.process_group.insert(self.base);
    }

    fn leave_process_group(&mut self, pids: &mut PidTable) {
        if self.process_group.remove(self.base) {
            self.process_group.session.write().remove(self.process_group.leader);
            pids.remove_process_group(self.process_group.leader);
        }
    }

    /// Returns any waitable child matching the given `selector` and `options`. Returns None if no
    /// child matching the selector is waitable. Returns ECHILD if no child matches the selector at
    /// all.
    ///
    /// Will remove the waitable status from the child depending on `options`.
    pub fn get_waitable_child(
        &mut self,
        selector: ProcessSelector,
        options: &WaitingOptions,
        pids: &PidTable,
    ) -> Result<Option<ZombieProcess>, Errno> {
        if options.wait_for_exited {
            if let Some(child) = match selector {
                ProcessSelector::Any => {
                    if !self.zombie_children.is_empty() {
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
                return Ok(Some(child));
            }
        }

        // TODO(tbodt): Try and unify .zombie_children with .waitable. This function is essentially
        // the same code twice. See also the TODO on zombie_children.

        // The vector of potential matches.
        let children_filter = |child: &Arc<ThreadGroup>| match selector {
            ProcessSelector::Any => true,
            ProcessSelector::Pid(pid) => child.leader == pid,
            ProcessSelector::Pgid(pgid) => {
                pids.get_process_group(pgid).as_ref() == Some(&child.read().process_group)
            }
        };

        let mut selected_children =
            self.children.values().map(|t| t.upgrade().unwrap()).filter(children_filter).peekable();
        if selected_children.peek().is_none() {
            return error!(ECHILD);
        }
        for child in selected_children {
            let mut child = child.write();
            if child.waitable.is_some() {
                if !child.stopped && options.wait_for_continued {
                    let siginfo = if options.keep_waitable_state {
                        child.waitable.clone().unwrap()
                    } else {
                        child.waitable.take().unwrap()
                    };
                    return Ok(Some(ZombieProcess::new(
                        child.as_ref(),
                        &child.get_task()?.creds(),
                        ExitStatus::Continue(siginfo),
                    )));
                }
                if child.stopped && options.wait_for_stopped {
                    let siginfo = if options.keep_waitable_state {
                        child.waitable.clone().unwrap()
                    } else {
                        child.waitable.take().unwrap()
                    };
                    return Ok(Some(ZombieProcess::new(
                        child.as_ref(),
                        &child.get_task()?.creds(),
                        ExitStatus::Stop(siginfo),
                    )));
                }
            }
        }

        Ok(None)
    }

    /// Returns a task in the current thread group.
    fn get_task(&self) -> Result<Arc<Task>, Errno> {
        self.tasks
            .get(&self.leader())
            .map(|t| {
                t.upgrade().expect("Weak references to task in ThreadGroup must always be valid")
            })
            .or_else(|| self.tasks().next())
            .ok_or_else(|| errno!(ESRCH))
    }

    /// Return the appropriate task in |thread_group| to send the given signal.
    pub fn get_signal_target(&self, _signal: &UncheckedSignal) -> Option<Arc<Task>> {
        // TODO(fxb/96632): Consider more than the main thread or the first thread in the thread group
        // to dispatch the signal.
        self.get_task().ok()
    }
});

#[cfg(test)]
mod test {
    use super::*;
    use crate::testing::*;
    use itertools::Itertools;

    #[::fuchsia::test]
    fn test_setsid() {
        fn get_process_group(task: &Task) -> Arc<ProcessGroup> {
            Arc::clone(&task.thread_group.read().process_group)
        }
        let (_kernel, current_task) = create_kernel_and_task();
        assert_eq!(current_task.thread_group.setsid(), error!(EPERM));

        let child_task = current_task.clone_task_for_test(0);
        assert_eq!(get_process_group(&current_task), get_process_group(&child_task));

        let old_process_group = child_task.thread_group.read().process_group.clone();
        assert_eq!(child_task.thread_group.setsid(), Ok(()));
        assert_eq!(
            child_task.thread_group.read().process_group.session.leader,
            child_task.get_pid()
        );
        assert!(!old_process_group.read().thread_groups().contains(&child_task.thread_group));
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
        assert!(!old_process_group.read().thread_groups().contains(&child_task2.thread_group));
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

        assert_eq!(task3.thread_group.read().get_ppid(), task2.id);

        task2.thread_group.exit(ExitStatus::Exit(0));
        std::mem::drop(task2);

        // Task3 parent should be current_task.
        assert_eq!(task3.thread_group.read().get_ppid(), current_task.id);
    }
}

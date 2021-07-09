// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use bitflags::bitflags;
use fuchsia_zircon::{self as zx, AsHandleRef, HandleBased, Task as zxTask};
use log::warn;
use parking_lot::{Condvar, Mutex, RwLock};
use std::cmp;
use std::collections::{HashMap, HashSet};
use std::convert::TryFrom;
use std::ffi::{CStr, CString};
use std::fmt;
use std::ops;
use std::sync::{Arc, Weak};

pub mod syscalls;
mod waiter;

pub use waiter::*;

use crate::auth::{Credentials, ShellJobControl};
use crate::devices::DeviceRegistry;
use crate::fs::*;
use crate::loader::*;
use crate::logging::*;
use crate::mm::MemoryManager;
use crate::not_implemented;
use crate::signals::types::*;
use crate::types::*;

bitflags! {
    pub struct OpenFlags: u32 {
      const CREATE = O_CREAT;
    }
}

pub struct Kernel {
    /// The Zircon job object that holds the processes running in this kernel.
    pub job: zx::Job,

    /// The processes and threads running in this kernel, organized by pid_t.
    pub pids: RwLock<PidTable>,

    /// The scheduler associated with this kernel. The scheduler stores state like suspended tasks,
    /// pending signals, etc.
    pub scheduler: RwLock<Scheduler>,

    /// The devices that exist in this kernel.
    pub devices: DeviceRegistry,
}

impl Kernel {
    pub fn new(name: &CString) -> Result<Arc<Kernel>, zx::Status> {
        let job = fuchsia_runtime::job_default().create_child_job()?;
        job.set_name(&name)?;
        let kernel = Kernel {
            job,
            pids: RwLock::new(PidTable::new()),
            scheduler: RwLock::new(Scheduler::new()),
            devices: DeviceRegistry::new(),
        };
        Ok(Arc::new(kernel))
    }

    #[cfg(test)]
    pub fn new_for_testing() -> Arc<Kernel> {
        Arc::new(Kernel {
            job: zx::Job::from_handle(zx::Handle::invalid()),
            pids: RwLock::new(PidTable::new()),
            scheduler: RwLock::new(Scheduler::new()),
            devices: DeviceRegistry::new(),
        })
    }
}

pub struct Scheduler {
    /// The condvars that suspended tasks are waiting on, organized by pid_t of the suspended task.
    pub suspended_tasks: HashMap<pid_t, Arc<Condvar>>,

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
        Scheduler { suspended_tasks: HashMap::new(), pending_signals: HashMap::new() }
    }

    /// Adds a task to the set of tasks currently suspended via `rt_sigsuspend`.
    ///
    /// Attempting to add a task that already exists is an error, and will panic.
    ///
    /// The suspended task will wait on the condition variable, and will be notified when it is
    /// the target of an appropriate signal.
    pub fn add_suspended_task(&mut self, pid: pid_t) -> Arc<Condvar> {
        assert!(!self.is_task_suspended(pid));
        let condvar = Arc::new(Condvar::new());
        self.suspended_tasks.insert(pid, condvar.clone());
        condvar
    }

    /// Returns true if the Task associated with `pid` is currently suspended in `rt_sigsuspend`.
    pub fn is_task_suspended(&self, pid: pid_t) -> bool {
        self.suspended_tasks.contains_key(&pid)
    }

    /// Removes the condition variable that `pid` is waiting on.
    ///
    /// The returned condition variable is meant to be notified before it is dropped in order
    /// for the task to resume operation in `rt_sigsuspend`.
    pub fn remove_suspended_task(&mut self, pid: pid_t) -> Option<Arc<Condvar>> {
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

pub struct PidTable {
    /// The most-recently allocated pid in this table.
    last_pid: pid_t,

    /// The tasks in this table, organized by pid_t.
    ///
    /// This reference is the primary reference keeping the tasks alive.
    tasks: HashMap<pid_t, Weak<Task>>,

    /// The thread groups that are present in this table.
    thread_groups: HashMap<pid_t, Weak<ThreadGroup>>,
}

impl PidTable {
    pub fn new() -> PidTable {
        PidTable { last_pid: 0, tasks: HashMap::new(), thread_groups: HashMap::new() }
    }

    pub fn get_task(&self, pid: pid_t) -> Option<Arc<Task>> {
        self.tasks.get(&pid).and_then(|task| task.upgrade())
    }

    pub fn get_thread_groups(&self) -> Vec<Arc<ThreadGroup>> {
        self.thread_groups.iter().flat_map(|(_pid, thread_group)| thread_group.upgrade()).collect()
    }

    fn allocate_pid(&mut self) -> pid_t {
        self.last_pid += 1;
        return self.last_pid;
    }

    fn add_task(&mut self, task: &Arc<Task>) {
        assert!(!self.tasks.contains_key(&task.id));
        self.tasks.insert(task.id, Arc::downgrade(task));
    }

    fn add_thread_group(&mut self, thread_group: &Arc<ThreadGroup>) {
        assert!(!self.thread_groups.contains_key(&thread_group.leader));
        self.thread_groups.insert(thread_group.leader, Arc::downgrade(thread_group));
    }

    fn remove_task(&mut self, pid: pid_t) {
        self.tasks.remove(&pid);
    }

    fn remove_thread_group(&mut self, pid: pid_t) {
        self.thread_groups.remove(&pid);
    }
}

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
    fn new(kernel: Arc<Kernel>, process: zx::Process, leader: pid_t) -> ThreadGroup {
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

    fn add(&self, task: &Task) {
        let mut tasks = self.tasks.write();
        tasks.insert(task.id);
    }

    fn remove(&self, task: &Task) {
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

#[derive(Debug, Eq, PartialEq)]
pub struct TaskOwner {
    pub task: Arc<Task>,
}

impl ops::Drop for TaskOwner {
    fn drop(&mut self) {
        self.task.destroy();
    }
}

pub struct Task {
    pub id: pid_t,

    // The command of this task.
    pub command: RwLock<CString>,

    /// The thread group to which this task belongs.
    pub thread_group: Arc<ThreadGroup>,

    /// The parent task, if any.
    pub parent: pid_t,

    /// The children of this task.
    pub children: RwLock<HashSet<pid_t>>,

    // TODO: The children of this task.
    /// A handle to the underlying Zircon thread object.
    pub thread: zx::Thread,

    /// The file descriptor table for this task.
    pub files: Arc<FdTable>,

    /// The memory manager for this task.
    pub mm: Arc<MemoryManager>,

    /// The file system for this task.
    pub fs: Arc<FsContext>,

    /// The security credentials for this task.
    pub creds: Credentials,

    /// The IDs used to perform shell job control.
    pub shell_job_control: ShellJobControl,

    // See https://man7.org/linux/man-pages/man2/set_tid_address.2.html
    pub clear_child_tid: Mutex<UserRef<pid_t>>,

    // See https://man7.org/linux/man-pages/man2/sigaltstack.2.html
    pub signal_stack: Mutex<Option<sigaltstack_t>>,

    /// The signal mask of the task.
    // See https://man7.org/linux/man-pages/man2/rt_sigprocmask.2.html
    pub signal_mask: Mutex<sigset_t>,

    /// The object this task sleeps upon.
    pub waiter: Arc<Waiter>,

    /// The signal this task generates on exit.
    pub exit_signal: Option<Signal>,

    /// The exit code that this task exited with.
    pub exit_code: Mutex<Option<i32>>,

    /// Child tasks that have exited, but not yet been `waited` on.
    pub zombie_tasks: RwLock<Vec<TaskOwner>>,
}

impl Task {
    /// Internal function for creating a Task object.
    ///
    /// Useful for sharing the default initialization of members between
    /// create_process and create_thread.
    ///
    /// Consider using create_process or create_thread instead of calling this
    /// function directly.
    fn new(
        id: pid_t,
        comm: CString,
        thread_group: Arc<ThreadGroup>,
        parent: pid_t,
        thread: zx::Thread,
        files: Arc<FdTable>,
        mm: Arc<MemoryManager>,
        fs: Arc<FsContext>,
        creds: Credentials,
        sjc: ShellJobControl,
        exit_signal: Option<Signal>,
    ) -> TaskOwner {
        TaskOwner {
            task: Arc::new(Task {
                id,
                command: RwLock::new(comm),
                thread_group,
                parent,
                children: RwLock::new(HashSet::new()),
                thread,
                files,
                mm,
                fs,
                creds,
                shell_job_control: sjc,
                clear_child_tid: Mutex::new(UserRef::default()),
                signal_stack: Mutex::new(None),
                signal_mask: Mutex::new(sigset_t::default()),
                waiter: Waiter::new(),
                exit_signal,
                exit_code: Mutex::new(None),
                zombie_tasks: RwLock::new(vec![]),
            }),
        }
    }

    /// Create a task that is the leader of a new thread group.
    ///
    /// This function creates an underlying Zircon process to host the new
    /// task.
    pub fn create_process(
        kernel: &Arc<Kernel>,
        comm: &CString,
        parent: pid_t,
        files: Arc<FdTable>,
        fs: Arc<FsContext>,
        creds: Credentials,
        exit_signal: Option<Signal>,
    ) -> Result<TaskOwner, Errno> {
        let (process, root_vmar) = kernel
            .job
            .create_child_process(comm.as_bytes())
            .map_err(Errno::from_status_like_fdio)?;
        let thread =
            process.create_thread(comm.as_bytes()).map_err(Errno::from_status_like_fdio)?;

        // TODO: Stop giving MemoryManager a duplicate of the process handle once a process
        // handle is not needed to implement read_memory or write_memory.
        let duplicate_process =
            process.duplicate_handle(zx::Rights::SAME_RIGHTS).map_err(impossible_error)?;

        let mut pids = kernel.pids.write();
        let id = pids.allocate_pid();

        let task_owner = Self::new(
            id,
            comm.clone(),
            Arc::new(ThreadGroup::new(kernel.clone(), process, id)),
            parent,
            thread,
            files,
            Arc::new(
                MemoryManager::new(duplicate_process, root_vmar)
                    .map_err(Errno::from_status_like_fdio)?,
            ),
            fs,
            creds,
            ShellJobControl::new(id),
            exit_signal,
        );
        pids.add_task(&task_owner.task);
        pids.add_thread_group(&task_owner.task.thread_group);
        Ok(task_owner)
    }

    /// Create a task that is a member of an existing thread group.
    ///
    /// The task is added to |self.thread_group|.
    ///
    /// This function creates an underlying Zircon thread to run the new
    /// task.
    pub fn create_thread(
        &self,
        files: Arc<FdTable>,
        fs: Arc<FsContext>,
        creds: Credentials,
    ) -> Result<TaskOwner, Errno> {
        let thread = self
            .thread_group
            .process
            .create_thread(self.command.read().as_bytes())
            .map_err(Errno::from_status_like_fdio)?;

        let mut pids = self.thread_group.kernel.pids.write();
        let id = pids.allocate_pid();
        let task_owner = Self::new(
            id,
            self.command.read().clone(),
            Arc::clone(&self.thread_group),
            self.parent,
            thread,
            files,
            Arc::clone(&self.mm),
            fs,
            creds,
            ShellJobControl::new(self.shell_job_control.sid),
            None,
        );
        pids.add_task(&task_owner.task);
        self.thread_group.add(&task_owner.task);
        Ok(task_owner)
    }

    /// Clone this task.
    ///
    /// Creates a new task object that shares some state with this task
    /// according to the given flags.
    ///
    /// Used by the clone() syscall to create both processes and threads.
    pub fn clone_task(
        &self,
        flags: u64,
        user_parent_tid: UserRef<pid_t>,
        user_child_tid: UserRef<pid_t>,
    ) -> Result<TaskOwner, Errno> {
        // TODO: Implement more flags.
        const IMPLEMENTED_FLAGS: u64 = (CLONE_VM
            | CLONE_FS
            | CLONE_FILES
            | CLONE_SIGHAND
            | CLONE_THREAD
            | CLONE_SYSVSEM
            | CLONE_SETTLS
            | CLONE_PARENT_SETTID
            | CLONE_CHILD_CLEARTID
            | CLONE_CHILD_SETTID
            | CSIGNAL) as u64;

        // CLONE_SETTLS is implemented by sys_clone.

        let clone_thread = flags & (CLONE_THREAD as u64) != 0;
        let clone_vm = flags & (CLONE_VM as u64) != 0;
        let clone_sighand = flags & (CLONE_SIGHAND as u64) != 0;

        if clone_thread != clone_vm || clone_thread != clone_sighand {
            not_implemented!(
                "clone requires CLONE_THREAD, CLONE_VM, and CLONE_SIGHAND to all be set or unset"
            );
            return Err(ENOSYS);
        }

        if flags & !IMPLEMENTED_FLAGS != 0 {
            not_implemented!("clone does not implement flags: 0x{:x}", flags & !IMPLEMENTED_FLAGS);
            return Err(ENOSYS);
        }

        let raw_child_exist_signal = flags & (CSIGNAL as u64);
        let child_exit_signal = if raw_child_exist_signal == 0 {
            None
        } else {
            Some(Signal::try_from(UncheckedSignal::new(raw_child_exist_signal))?)
        };

        let fs = if flags & (CLONE_FS as u64) != 0 { self.fs.clone() } else { self.fs.fork() };

        let files =
            if flags & (CLONE_FILES as u64) != 0 { self.files.clone() } else { self.files.fork() };

        let creds = self.creds.clone();

        let child;
        if clone_thread {
            child = self.create_thread(files, fs, creds)?;
        } else {
            child = Self::create_process(
                &self.thread_group.kernel,
                &self.command.read(),
                self.id,
                files,
                fs,
                creds,
                child_exit_signal,
            )?;
            self.mm.snapshot_to(&child.task.mm)?;
        }

        if flags & (CLONE_PARENT_SETTID as u64) != 0 {
            self.mm.write_object(user_parent_tid, &child.task.id)?;
        }

        if flags & (CLONE_CHILD_CLEARTID as u64) != 0 {
            *child.task.clear_child_tid.lock() = user_child_tid;
        }

        if flags & (CLONE_CHILD_SETTID as u64) != 0 {
            child.task.mm.write_object(user_child_tid, &child.task.id)?;
        }

        self.children.write().insert(child.task.id);

        Ok(child)
    }

    pub fn exec(
        &self,
        path: &CStr,
        argv: &Vec<CString>,
        environ: &Vec<CString>,
    ) -> Result<ThreadStartInfo, Errno> {
        let executable_file = self.open_file(path.to_bytes())?;
        let executable = executable_file
            .get_vmo(self, zx::VmarFlags::PERM_READ | zx::VmarFlags::PERM_EXECUTE)?;

        // TODO: Implement #!interpreter [optional-arg]

        // TODO: All threads other than the calling thread are destroyed.

        self.mm.exec().map_err(Errno::from_status_like_fdio)?;

        // TODO: The file descriptor table is unshared, undoing the effect of
        //       the CLONE_FILES flag of clone(2).
        //
        // To make this work, we can put the files in an RwLock and then cache
        // a reference to the files on the SyscallContext. That will let
        // functions that have SyscallContext access the FdTable without
        // needing to grab the read-lock.
        //
        // For now, we do not implement that behavior.
        self.files.exec();

        // TODO: The termination signal is reset to SIGCHLD.

        self.thread_group.set_name(path)?;
        *self.command.write() = path.to_owned();
        Ok(load_executable(self, executable, argv, environ)?)
    }

    /// If needed, clear the child tid for this task.
    ///
    /// Userspace can ask us to clear the child tid and issue a futex wake at
    /// the child tid address when we tear down a task. For example, bionic
    /// uses this mechanism to implement pthread_join. The thread that calls
    /// pthread_join sleeps using FUTEX_WAIT on the child tid address. We wake
    /// them up here to let them know the thread is done.
    fn clear_child_tid_if_needed(&self) -> Result<(), Errno> {
        let mut clear_child_tid = self.clear_child_tid.lock();
        let user_tid = *clear_child_tid;
        if !user_tid.is_null() {
            let zero: pid_t = 0;
            self.mm.write_object(user_tid, &zero)?;
            self.mm.futex.wake(user_tid.addr(), usize::MAX, FUTEX_BITSET_MATCH_ANY);
            *clear_child_tid = UserRef::default();
        }
        Ok(())
    }

    /// Called by the Drop trait on TaskOwner.
    fn destroy(&self) {
        let _ignored = self.clear_child_tid_if_needed();
        self.thread_group.remove(self);
        if let Some(parent) = self.get_task(self.parent) {
            parent.remove_child(self.id);
        }
    }

    pub fn open_file(&self, path: &FsStr) -> Result<FileHandle, Errno> {
        self.open_file_at(FdNumber::AT_FDCWD, path, OpenFlags::empty())
    }

    pub fn open_file_at(
        &self,
        dir_fd: FdNumber,
        mut path: &FsStr,
        flags: OpenFlags,
    ) -> Result<FileHandle, Errno> {
        let dir = if path[0] == b'/' {
            path = &path[1..];
            self.fs.root.clone()
        } else if dir_fd == FdNumber::AT_FDCWD {
            self.fs.cwd()
        } else {
            let file = self.files.get(dir_fd)?;
            file.name().clone()
        };

        if flags.contains(OpenFlags::CREATE) {
            // TODO put path manipulations into a library
            let mut parent_path: Vec<u8> = Vec::new();
            let mut path_elts = path.split(|c| *c == b'/');
            let mut file_name = path_elts.next().ok_or(ENOENT)?;
            for elt in path_elts {
                parent_path.push(b'/');
                for b in file_name {
                    parent_path.push(*b);
                }
                file_name = elt;
            }

            let parent_dir = self.fs.lookup_node(dir, parent_path.as_slice())?;
            parent_dir.create(file_name)?.open()
        } else {
            self.fs.lookup_node(dir, path)?.open()
        }
    }

    pub fn get_task(&self, pid: pid_t) -> Option<Arc<Task>> {
        self.thread_group.kernel.pids.read().get_task(pid)
    }

    pub fn get_pid(&self) -> pid_t {
        // This is set to 1 because Bionic skips referencing /dev if getpid() == 1, under the
        // assumption that anything running after init will have access to /dev.
        1.into()
    }

    pub fn get_sid(&self) -> pid_t {
        self.shell_job_control.sid
    }

    pub fn get_tid(&self) -> pid_t {
        self.id
    }

    pub fn get_pgrp(&self) -> pid_t {
        // TODO: Implement process groups.
        1
    }

    /// Returns whether or not the task has the given `capability`.
    ///
    // TODO(lindkvist): This should do a proper check for the capability in the namespace.
    // TODO(lindkvist): `capability` should be a type, just like we do for signals.
    pub fn has_capability(&self, _capability: u32) -> bool {
        false
    }

    pub fn can_signal(&self, target: &Task, unchecked_signal: &UncheckedSignal) -> bool {
        // If both the tasks share a thread group the signal can be sent. This is not documented
        // in kill(2) because kill does not support task-level granularity in signal sending.
        if self.thread_group == target.thread_group {
            return true;
        }

        if self.has_capability(CAP_KILL) {
            return true;
        }

        if self.creds.has_same_uid(&target.creds) {
            return true;
        }

        // TODO(lindkvist): This check should also verify that the sessions are the same.
        if Signal::try_from(unchecked_signal) == Ok(Signal::SIGCONT) {
            return true;
        }

        false
    }

    fn remove_child(&self, pid: pid_t) {
        self.children.write().remove(&pid);
    }
}

impl fmt::Debug for Task {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "task({})", self.id)
    }
}

impl cmp::PartialEq for Task {
    fn eq(&self, other: &Self) -> bool {
        let ptr: *const Task = self;
        let other_ptr: *const Task = other;
        return ptr == other_ptr;
    }
}

impl cmp::Eq for Task {}

#[cfg(test)]
mod test {
    use fuchsia_async as fasync;

    use crate::testing::*;

    #[fasync::run_singlethreaded(test)]
    async fn test_tid_allocation() {
        let (kernel, task_owner) = create_kernel_and_task();

        let task = &task_owner.task;
        assert_eq!(task.get_tid(), 1);
        let another_task_owner = create_task(&kernel, "another-task");
        let another_task = &another_task_owner.task;
        assert_eq!(another_task.get_tid(), 2);

        let pids = kernel.pids.read();
        assert_eq!(pids.get_task(1).unwrap().get_tid(), 1);
        assert_eq!(pids.get_task(2).unwrap().get_tid(), 2);
        assert!(pids.get_task(3).is_none());
    }
}

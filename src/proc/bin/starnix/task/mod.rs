// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon::{self as zx, AsHandleRef, HandleBased, Task as zxTask};
use log::warn;
use parking_lot::{Condvar, Mutex, RwLock};
use std::cmp;
use std::collections::{HashMap, HashSet};
use std::convert::TryFrom;
use std::ffi::CString;
use std::fmt;
use std::ops;
use std::sync::{Arc, Weak};

pub mod syscalls;

use crate::auth::Credentials;
use crate::fs::{FdTable, FileSystem};
use crate::logging::*;
use crate::mm::MemoryManager;
use crate::not_implemented;
use crate::signals::types::*;
use crate::types::*;

// # Ownership structure
//
// The Kernel object is the root object of the task hierarchy.
//
// The Kernel owns the PidTable, which has the owning reference to each task in the
// kernel via its |tasks| field.
//
// Each task holds a reference to its ThreadGroup and an a reference to the objects
// for each major subsystem (e.g., file system, memory manager).
//
// # Back pointers
//
// Each ThreadGroup has weak pointers to its threads and to the kernel to which its
// threads belong.

pub struct Kernel {
    /// The Zircon job object that holds the processes running in this kernel.
    pub job: zx::Job,

    /// The processes and threads running in this kernel, organized by pid_t.
    pub pids: RwLock<PidTable>,
}

impl Kernel {
    pub fn new(name: &CString) -> Result<Arc<Kernel>, zx::Status> {
        let job = fuchsia_runtime::job_default().create_child_job()?;
        job.set_name(&name)?;
        let kernel = Kernel { job, pids: RwLock::new(PidTable::new()) };
        Ok(Arc::new(kernel))
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

    /// The condvars that suspended tasks are waiting on, organized by pid_t of the suspended task.
    suspended_tasks: HashMap<pid_t, Arc<Condvar>>,
}

impl PidTable {
    pub fn new() -> PidTable {
        PidTable {
            last_pid: 0,
            tasks: HashMap::new(),
            thread_groups: HashMap::new(),
            suspended_tasks: HashMap::new(),
        }
    }

    pub fn get_task(&self, pid: pid_t) -> Option<Arc<Task>> {
        self.tasks.get(&pid).and_then(|task| task.upgrade())
    }

    pub fn get_thread_groups(&self) -> Vec<Arc<ThreadGroup>> {
        self.thread_groups.iter().flat_map(|(_pid, thread_group)| thread_group.upgrade()).collect()
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

    /// The signal actions that are registered for `tasks`. All `tasks` share the same `sigaction`
    /// for a given signal.
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
            signal_actions: RwLock::new(SignalActions::default()),
        }
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

    /// The thread group to which this task belongs.
    pub thread_group: Arc<ThreadGroup>,

    /// The parent task, if any.
    pub parent: pid_t,

    // TODO: The children of this task.
    /// A handle to the underlying Zircon thread object.
    pub thread: zx::Thread,

    /// The file descriptor table for this task.
    pub files: Arc<FdTable>,

    /// The memory manager for this task.
    pub mm: Arc<MemoryManager>,

    /// The file system for this task.
    pub fs: Arc<FileSystem>,

    /// The security credentials for this task.
    pub creds: Credentials,

    // See https://man7.org/linux/man-pages/man2/set_tid_address.2.html
    pub set_child_tid: Mutex<UserRef<pid_t>>,
    pub clear_child_tid: Mutex<UserRef<pid_t>>,

    // See https://man7.org/linux/man-pages/man2/sigaltstack.2.html
    pub signal_stack: Mutex<Option<sigaltstack_t>>,

    /// The signal mask of the task.
    // See https://man7.org/linux/man-pages/man2/rt_sigprocmask.2.html
    pub signal_mask: Mutex<sigset_t>,

    /// The signal this task generates on exit.
    pub exit_signal: Option<Signal>,
}

impl Task {
    pub fn new(
        kernel: &Arc<Kernel>,
        name: &CString,
        files: Arc<FdTable>,
        fs: Arc<FileSystem>,
        creds: Credentials,
        exit_signal: Option<Signal>,
    ) -> Result<TaskOwner, Errno> {
        let (process, root_vmar) = kernel
            .job
            .create_child_process(name.as_bytes())
            .map_err(Errno::from_status_like_fdio)?;
        let thread = process
            .create_thread("initial-thread".as_bytes())
            .map_err(Errno::from_status_like_fdio)?;

        // TODO: Stop giving MemoryManager a duplicate of the process handle once a process
        // handle is not needed to implement read_memory or write_memory.
        let duplicate_process =
            process.duplicate_handle(zx::Rights::SAME_RIGHTS).map_err(impossible_error)?;

        let mut pids = kernel.pids.write();
        let id = pids.allocate_pid();
        let task = Arc::new(Task {
            id,
            thread_group: Arc::new(ThreadGroup::new(kernel.clone(), process, id)),
            parent: 0,
            thread,
            files,
            mm: Arc::new(MemoryManager::new(duplicate_process, root_vmar)),
            fs,
            creds: creds,
            set_child_tid: Mutex::new(UserRef::default()),
            clear_child_tid: Mutex::new(UserRef::default()),
            signal_stack: Mutex::new(None),
            signal_mask: Mutex::new(sigset_t::default()),
            exit_signal,
        });
        pids.add_task(&task);
        pids.add_thread_group(&task.thread_group);

        Ok(TaskOwner { task })
    }

    pub fn clone_task(
        &self,
        flags: u64,
        user_stack: UserAddress,
        _user_parent_tid: UserRef<pid_t>,
        user_child_tid: UserRef<pid_t>,
        _user_tls: UserAddress,
    ) -> Result<TaskOwner, Errno> {
        // TODO: Implement more flags.
        const IMPLEMENTED_FLAGS: u64 =
            (CLONE_FS | CLONE_FILES | CLONE_CHILD_CLEARTID | CLONE_CHILD_SETTID | CSIGNAL) as u64;

        if flags & !IMPLEMENTED_FLAGS != 0 {
            not_implemented!("clone does not implement flags: {}", flags & !IMPLEMENTED_FLAGS);
            return Err(ENOSYS);
        }

        if !user_stack.is_null() {
            not_implemented!("clone does not implement non-zero stack: {}", user_stack);
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

        let child = Self::new(
            &self.thread_group.kernel,
            &CString::new("cloned-child").unwrap(),
            files,
            fs,
            creds,
            child_exit_signal,
        )?;
        self.mm.snapshot_to(&child.task.mm)?;

        if flags & (CLONE_CHILD_CLEARTID as u64) != 0 {
            *child.task.clear_child_tid.lock() = user_child_tid;
            let zero: pid_t = 0;
            child.task.mm.write_object(user_child_tid, &zero)?;
            // TODO: Issue a FUTEX_WAKE at this address.
        }

        if flags & (CLONE_CHILD_SETTID as u64) != 0 {
            *child.task.set_child_tid.lock() = user_child_tid;
            child.task.mm.write_object(user_child_tid, &child.task.id)?;
        }

        Ok(child)
    }

    /// Called by the Drop trait on TaskOwner.
    fn destroy(&self) {
        self.thread_group.remove(self);
    }

    pub fn get_task(&self, pid: pid_t) -> Option<Arc<Task>> {
        self.thread_group.kernel.pids.read().get_task(pid)
    }

    pub fn get_pid(&self) -> pid_t {
        // This is set to 1 because Bionic skips referencing /dev if getpid() == 1, under the
        // assumption that anything running after init will have access to /dev.
        1.into()
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

    // TODO(lindkvist): Implement proper signal sending.
    pub fn send_signal(&self, unchecked_signal: &UncheckedSignal) -> Result<(), Errno> {
        // 0 is a sentinel value used to do permission checks.
        let sentinel_signal = UncheckedSignal::from(0);
        if *unchecked_signal == sentinel_signal {
            return Ok(());
        }

        let signal = Signal::try_from(unchecked_signal)?;
        if signal.mask() & *self.signal_mask.lock() != 0 {
            if let Some(waiter_condvar) =
                self.thread_group.kernel.pids.write().remove_suspended_task(self.id)
            {
                waiter_condvar.notify_all();
            }
        }
        Ok(())
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

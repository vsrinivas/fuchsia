// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon::{self as zx, AsHandleRef, HandleBased};
use parking_lot::{Mutex, RwLock};
use std::collections::HashMap;
use std::ffi::CString;
use std::sync::{Arc, Weak};

use crate::auth::Credentials;
use crate::fs::{FdTable, FileSystem};
use crate::mm::MemoryManager;
use crate::uapi::*;

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
    tasks: HashMap<pid_t, Arc<Task>>,
}

impl PidTable {
    pub fn new() -> PidTable {
        PidTable { last_pid: 0, tasks: HashMap::new() }
    }

    #[cfg(test)] // Currently used only by tests.
    pub fn get_task(&self, pid: pid_t) -> Option<Arc<Task>> {
        return self.tasks.get(&pid).map(|task| task.clone());
    }

    fn allocate_pid(&mut self) -> pid_t {
        self.last_pid += 1;
        return self.last_pid;
    }

    fn add_task(&mut self, task: Arc<Task>) {
        assert!(!self.tasks.contains_key(&task.id));
        self.tasks.insert(task.id, task);
    }
}

pub struct ThreadGroup {
    /// The kernel to which this thread group belongs.
    pub kernel: Weak<Kernel>,

    /// The lead task of this thread group.
    ///
    /// The lead task is typically the initial thread created in the thread group.
    pub leader: Weak<Task>,

    /// The tasks in the thread group.
    pub tasks: Vec<Weak<Task>>,

    /// A handle to the underlying Zircon process object.
    ///
    /// Currently, we have a 1-to-1 mapping between thread groups and zx::process
    /// objects. This approach might break down if/when we implement CLONE_VM
    /// without CLONE_THREAD because that creates a situation where two thread
    /// groups share an address space. To implement that situation, we might
    /// need to break the 1-to-1 mapping between thread groups and zx::process
    /// or teach zx::process to share address spaces.
    pub process: zx::Process,
}

impl ThreadGroup {
    fn new(kernel: Weak<Kernel>, process: zx::Process) -> ThreadGroup {
        ThreadGroup { kernel, leader: Weak::new(), tasks: Vec::new(), process }
    }

    fn add_leader(&mut self, leader: Weak<Task>) {
        self.leader = leader.clone();
        self.tasks.push(leader);
    }
}

pub struct Task {
    pub id: pid_t,

    /// The thread group to which this task belongs.
    pub thread_group: Arc<RwLock<ThreadGroup>>,

    /// The parent task, if any.
    pub parent: Option<Weak<Task>>,

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
    pub set_child_tid: Mutex<UserAddress>,
    pub clear_child_tid: Mutex<UserAddress>,
}

impl Task {
    pub fn new(
        kernel: &Arc<Kernel>,
        name: &CString,
        fs: Arc<FileSystem>,
        creds: Credentials,
    ) -> Result<Arc<Task>, zx::Status> {
        let (process, root_vmar) = kernel.job.create_child_process(name.as_bytes())?;
        let thread = process.create_thread("initial-thread".as_bytes())?;
        // TODO: Stop giving MemoryManager a duplicate of the process handle once a process
        // handle is not needed to implement read_memory or write_memory.
        let duplicate_process = process.duplicate_handle(zx::Rights::SAME_RIGHTS)?;
        return Task::create_task(kernel, fs, creds, process, root_vmar, thread, duplicate_process);
    }

    #[cfg(test)]
    pub fn new_mock(kernel: &Arc<Kernel>, fs: Arc<FileSystem>) -> Result<Arc<Task>, zx::Status> {
        return Task::create_task(
            kernel,
            fs,
            Credentials::root(),
            zx::Process::from(zx::Handle::invalid()),
            zx::Vmar::from(zx::Handle::invalid()),
            zx::Thread::from(zx::Handle::invalid()),
            zx::Process::from(zx::Handle::invalid()),
        );
    }

    fn create_task(
        kernel: &Arc<Kernel>,
        fs: Arc<FileSystem>,
        creds: Credentials,
        process: zx::Process,
        root_vmar: zx::Vmar,
        thread: zx::Thread,
        duplicate_process: zx::Process,
    ) -> Result<Arc<Task>, zx::Status> {
        let mut pids = kernel.pids.write();
        let task = Arc::new(Task {
            id: pids.allocate_pid(),
            thread_group: Arc::new(RwLock::new(ThreadGroup::new(Arc::downgrade(&kernel), process))),
            parent: None,
            thread,
            files: Arc::new(FdTable::new()),
            mm: Arc::new(MemoryManager::new(duplicate_process, root_vmar)),
            fs,
            creds: creds,
            set_child_tid: Mutex::new(UserAddress::default()),
            clear_child_tid: Mutex::new(UserAddress::default()),
        });
        task.thread_group.write().add_leader(Arc::downgrade(&task));
        pids.add_task(task.clone());

        Ok(task)
    }

    pub fn get_tid(&self) -> pid_t {
        self.id
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use fuchsia_async as fasync;
    use io_util::directory;

    #[fasync::run_singlethreaded(test)]
    async fn test_tid_allocation() {
        let kernel =
            Kernel::new(&CString::new("test-kernel").unwrap()).expect("failed to create kernel");

        let root = directory::open_in_namespace("/pkg", fidl_fuchsia_io::OPEN_RIGHT_READABLE)
            .expect("failed to open /pkg");
        let fs = Arc::new(FileSystem::new(root));

        let task = Task::new_mock(&kernel, fs.clone()).expect("failed to create first task");
        assert_eq!(task.get_tid(), 1);
        let another_task =
            Task::new_mock(&kernel, fs.clone()).expect("failed to create second task");
        assert_eq!(another_task.get_tid(), 2);

        let pids = kernel.pids.read();
        assert_eq!(pids.get_task(1).unwrap().get_tid(), 1);
        assert_eq!(pids.get_task(2).unwrap().get_tid(), 2);
        assert!(pids.get_task(3).is_none());
    }
}

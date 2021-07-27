// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon::{self as zx, HandleBased};
use parking_lot::{Mutex, RwLock};
use std::cmp;
use std::collections::HashSet;
use std::convert::TryFrom;
use std::ffi::{CStr, CString};
use std::fmt;
use std::ops;
use std::sync::Arc;

use crate::auth::{Credentials, ShellJobControl};
use crate::fs::*;
use crate::loader::*;
use crate::logging::*;
use crate::mm::MemoryManager;
use crate::not_implemented;
use crate::signals::types::*;
use crate::task::*;
use crate::types::*;

#[derive(Debug, Eq, PartialEq)]
pub struct TaskOwner {
    pub task: Arc<Task>,
}

impl ops::Drop for TaskOwner {
    fn drop(&mut self) {
        self.task.destroy();
    }
}

pub struct ZombieTask {
    pub id: pid_t,
    pub parent: pid_t,
    pub exit_code: Option<i32>,
    // TODO: Do we need exit_signal?
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

    /// The saved signal mask of the task. This mask is set when a task wakes in `sys_rt_sigsuspend`
    /// so that the signal mask can be restored properly after the signal handler has executed.
    pub saved_signal_mask: Mutex<Option<sigset_t>>,

    /// The object this task sleeps upon.
    pub waiter: Arc<Waiter>,

    /// The signal this task generates on exit.
    pub exit_signal: Option<Signal>,

    /// The exit code that this task exited with.
    pub exit_code: Mutex<Option<i32>>,

    /// Child tasks that have exited, but not yet been `waited` on.
    pub zombie_tasks: RwLock<Vec<ZombieTask>>,
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
                saved_signal_mask: Mutex::new(None),
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
            *child.task.signal_stack.lock() = *self.signal_stack.lock();
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
        let executable_file = self.open_file(path.to_bytes(), OpenFlags::RDONLY)?;
        let executable = executable_file
            .get_vmo(self, zx::VmarFlags::PERM_READ | zx::VmarFlags::PERM_EXECUTE)?;

        // TODO: Implement #!interpreter [optional-arg]

        // TODO: All threads other than the calling thread are destroyed.

        // TODO: The dispositions of any signals that are being caught are
        //       reset to the default.

        *self.signal_stack.lock() = None;

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

        // TODO: POSIX timers are not preserved.

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
    fn destroy(self: &Arc<Self>) {
        let _ignored = self.clear_child_tid_if_needed();
        self.thread_group.remove(self);
        if let Some(parent) = self.get_task(self.parent) {
            parent.remove_child(self.id);
        }
    }

    fn resolve_dir_fd<'a>(
        &self,
        dir_fd: FdNumber,
        mut path: &'a FsStr,
    ) -> Result<(NamespaceNode, &'a FsStr), Errno> {
        let dir = if !path.is_empty() && path[0] == b'/' {
            path = &path[1..];
            self.fs.root.clone()
        } else if dir_fd == FdNumber::AT_FDCWD {
            self.fs.cwd()
        } else {
            let file = self.files.get(dir_fd)?;
            file.name().clone()
        };
        Ok((dir, path))
    }

    /// A convenient wrapper for opening files relative to FdNumber::AT_FDCWD.
    ///
    /// Returns a FileHandle but does not install the FileHandle in the FdTable
    /// for this task.
    pub fn open_file(&self, path: &FsStr, flags: OpenFlags) -> Result<FileHandle, Errno> {
        if flags.contains(OpenFlags::CREAT) {
            // In order to support OpenFlags::CREAT we would need to take a
            // FileMode argument.
            return Err(EINVAL);
        }
        self.open_file_at(FdNumber::AT_FDCWD, path, flags, FileMode::default())
    }

    /// The primary entry point for opening files relative to a task.
    ///
    /// Absolute paths are resolve relative to the root of the FsContext for
    /// this task. Relative paths are resolve relative to dir_fd. To resolve
    /// relative to the current working directory, pass FdNumber::AT_FDCWD for
    /// dir_fd.
    ///
    /// Returns a FileHandle but does not install the FileHandle in the FdTable
    /// for this task.
    pub fn open_file_at(
        &self,
        dir_fd: FdNumber,
        path: &FsStr,
        flags: OpenFlags,
        mode: FileMode,
    ) -> Result<FileHandle, Errno> {
        let (dir, path) = self.resolve_dir_fd(dir_fd, path)?;
        let (parent, basename) = self.fs.lookup_parent(dir, path)?;
        let node = match parent.lookup(&self.fs, basename, SymlinkFollowing::Enabled) {
            Ok(node) => node,
            Err(errno) => {
                if !flags.contains(OpenFlags::CREAT) {
                    return Err(errno);
                }
                let access = self.fs.apply_umask(mode & FileMode::ALLOW_ALL);
                // TODO: Do we need to check the errno and attempt to create
                // the file only when we get ENOENT?
                parent.mknod(basename, FileMode::IFREG | access, 0)?
            }
        };

        let file = node.open(flags)?;

        if flags.contains(OpenFlags::TRUNC) {
            let node = file.node();
            let mode = node.info().mode;
            match mode.fmt() {
                FileMode::IFREG => {
                    // You might think we should check file.can_write() at this
                    // point, which is what the docs suggest, but apparently we
                    // are supposed to truncate the file if this task can write
                    // to the underlying node, even if we are opening the file
                    // as read-only. See OpenTest.CanTruncateReadOnly.

                    // TODO(security): We should really do an access check for whether
                    // this task can write to this file.
                    if mode.contains(FileMode::IWUSR) {
                        node.truncate(0)?;
                    }
                }
                FileMode::IFDIR => return Err(EISDIR),
                _ => (),
            }
        }

        Ok(file)
    }

    /// A wrapper for FsContext::lookup_parent_at that resolves the given
    /// dir_fd to a NamespaceNode.
    ///
    /// Absolute paths are resolve relative to the root of the FsContext for
    /// this task. Relative paths are resolve relative to dir_fd. To resolve
    /// relative to the current working directory, pass FdNumber::AT_FDCWD for
    /// dir_fd.
    pub fn lookup_parent_at<'a>(
        &self,
        dir_fd: FdNumber,
        path: &'a FsStr,
    ) -> Result<(NamespaceNode, &'a FsStr), Errno> {
        let (dir, path) = self.resolve_dir_fd(dir_fd, path)?;
        self.fs.lookup_parent(dir, path)
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

    pub fn as_zombie(&self) -> ZombieTask {
        ZombieTask { id: self.id, parent: self.parent, exit_code: *self.exit_code.lock() }
    }

    /// Removes and returns any zombie task with the specified `pid`, if such a zombie exists.
    ///
    /// If pid == -1, an arbitrary zombie task is returned.
    pub fn get_zombie_task(&self, pid: pid_t) -> Option<ZombieTask> {
        let mut zombie_tasks = self.zombie_tasks.write();
        if pid == -1 {
            zombie_tasks.pop()
        } else {
            if let Some(position) = zombie_tasks.iter().position(|zombie| zombie.id == pid) {
                Some(zombie_tasks.remove(position))
            } else {
                None
            }
        }
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

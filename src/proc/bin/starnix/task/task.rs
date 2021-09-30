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
use crate::errno;
use crate::error;
use crate::from_status_like_fdio;
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

pub struct Task {
    pub id: pid_t,

    // The command of this task.
    pub command: RwLock<CString>,

    /// The namespace node that represents the executable associated with this task.
    pub executable_node: RwLock<Option<NamespaceNode>>,

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
    pub creds: RwLock<Credentials>,

    /// The namespace for abstract AF_UNIX sockets for this task.
    pub abstract_socket_namespace: Arc<AbstractSocketNamespace>,

    /// The IDs used to perform shell job control.
    pub shell_job_control: ShellJobControl,

    // See https://man7.org/linux/man-pages/man2/set_tid_address.2.html
    pub clear_child_tid: Mutex<UserRef<pid_t>>,

    /// The signal actions that are registered for this task.
    pub signal_actions: Arc<SignalActions>,

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

    /// Child tasks that have exited, but not yet been waited for.
    pub zombie_children: Mutex<Vec<ZombieTask>>,
}

pub struct ZombieTask {
    pub id: pid_t,
    pub parent: pid_t,
    pub exit_code: Option<i32>,
    // TODO: Do we need exit_signal?
}

/// A selector that can match a task. Works as a representation of the pid argument to syscalls
/// like wait and kill.
#[derive(Clone, Copy)]
pub enum TaskSelector {
    /// Matches any process at all.
    Any,
    /// Matches only the process with the specified pid
    Pid(pid_t),
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
        executable_node: Option<NamespaceNode>,
        thread_group: Arc<ThreadGroup>,
        parent: pid_t,
        thread: zx::Thread,
        files: Arc<FdTable>,
        mm: Arc<MemoryManager>,
        fs: Arc<FsContext>,
        signal_actions: Arc<SignalActions>,
        creds: Credentials,
        abstract_socket_namespace: Arc<AbstractSocketNamespace>,
        sjc: ShellJobControl,
        exit_signal: Option<Signal>,
    ) -> TaskOwner {
        TaskOwner {
            task: Arc::new(Task {
                id,
                command: RwLock::new(comm),
                executable_node: RwLock::new(executable_node),
                thread_group,
                parent,
                children: RwLock::new(HashSet::new()),
                thread,
                files,
                mm,
                fs,
                creds: RwLock::new(creds),
                abstract_socket_namespace,
                shell_job_control: sjc,
                clear_child_tid: Mutex::new(UserRef::default()),
                signal_actions,
                signal_stack: Mutex::new(None),
                signal_mask: Mutex::new(sigset_t::default()),
                saved_signal_mask: Mutex::new(None),
                waiter: Waiter::new(),
                exit_signal,
                exit_code: Mutex::new(None),
                zombie_children: Mutex::new(vec![]),
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
        signal_actions: Arc<SignalActions>,
        creds: Credentials,
        abstract_socket_namespace: Arc<AbstractSocketNamespace>,
        exit_signal: Option<Signal>,
    ) -> Result<TaskOwner, Errno> {
        let (process, root_vmar) = kernel
            .job
            .create_child_process(comm.as_bytes())
            .map_err(|status| from_status_like_fdio!(status))?;
        let thread = process
            .create_thread(comm.as_bytes())
            .map_err(|status| from_status_like_fdio!(status))?;

        // TODO: Stop giving MemoryManager a duplicate of the process handle once a process
        // handle is not needed to implement read_memory or write_memory.
        let duplicate_process =
            process.duplicate_handle(zx::Rights::SAME_RIGHTS).map_err(impossible_error)?;

        let mut pids = kernel.pids.write();
        let id = pids.allocate_pid();

        let task_owner = Self::new(
            id,
            comm.clone(),
            None,
            Arc::new(ThreadGroup::new(kernel.clone(), process, id)),
            parent,
            thread,
            files,
            Arc::new(
                MemoryManager::new(duplicate_process, root_vmar)
                    .map_err(|status| from_status_like_fdio!(status))?,
            ),
            fs,
            signal_actions,
            creds,
            abstract_socket_namespace,
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
        signal_actions: Arc<SignalActions>,
        creds: Credentials,
    ) -> Result<TaskOwner, Errno> {
        let thread = self
            .thread_group
            .process
            .create_thread(self.command.read().as_bytes())
            .map_err(|status| from_status_like_fdio!(status))?;

        let mut pids = self.thread_group.kernel.pids.write();
        let id = pids.allocate_pid();
        let task_owner = Self::new(
            id,
            self.command.read().clone(),
            self.executable_node.read().clone(),
            Arc::clone(&self.thread_group),
            self.parent,
            thread,
            files,
            Arc::clone(&self.mm),
            fs,
            signal_actions,
            creds,
            Arc::clone(&self.abstract_socket_namespace),
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

        if clone_sighand && !clone_vm {
            return error!(EINVAL);
        }
        if clone_thread && !clone_sighand {
            return error!(EINVAL);
        }

        if clone_thread != clone_vm {
            not_implemented!("CLONE_VM without CLONE_THRAED is not implemented");
            return error!(ENOSYS);
        }

        if flags & !IMPLEMENTED_FLAGS != 0 {
            not_implemented!("clone does not implement flags: 0x{:x}", flags & !IMPLEMENTED_FLAGS);
            return error!(ENOSYS);
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
        let signal_actions = if flags & (CLONE_SIGHAND as u64) != 0 {
            self.signal_actions.clone()
        } else {
            self.signal_actions.fork()
        };
        let creds = self.creds.read().clone();

        let child;
        if clone_thread {
            child = self.create_thread(files, fs, signal_actions, creds)?;
        } else {
            child = Self::create_process(
                &self.thread_group.kernel,
                &self.command.read(),
                self.id,
                files,
                fs,
                signal_actions,
                creds,
                Arc::clone(&self.abstract_socket_namespace),
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
        let executable = self.open_file(path.to_bytes(), OpenFlags::RDONLY)?;
        *self.executable_node.write() = Some(executable.name.clone());

        // TODO: Implement #!interpreter [optional-arg]

        // TODO: All threads other than the calling thread are destroyed.

        // TODO: The dispositions of any signals that are being caught are
        //       reset to the default.

        *self.signal_stack.lock() = None;

        self.mm.exec().map_err(|status| from_status_like_fdio!(status))?;

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

    /// Determine namespace node indicated by the dir_fd.
    ///
    /// Returns the namespace node and the path to use relative to that node.
    pub fn resolve_dir_fd<'a>(
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
            file.name.clone()
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
            return error!(EINVAL);
        }
        self.open_file_at(FdNumber::AT_FDCWD, path, flags, FileMode::default())
    }

    /// Resolves a path for open.
    ///
    /// If the final path component points to a symlink, the symlink is followed (as long as
    /// the symlink traversal limit has not been reached).
    ///
    /// If the final path component (after following any symlinks, if enabled) does not exist,
    /// and `flags` contains `OpenFlags::CREAT`, a new node is created at the location of the
    /// final path component.
    fn resolve_open_path(
        &self,
        context: &mut LookupContext,
        dir: NamespaceNode,
        path: &FsStr,
        mode: FileMode,
        flags: OpenFlags,
    ) -> Result<NamespaceNode, Errno> {
        let path = context.update_for_path(&path);
        let mut parent_content = context.with(SymlinkMode::Follow);
        let (parent, basename) = self.lookup_parent(&mut parent_content, dir.clone(), path)?;
        context.remaining_follows = parent_content.remaining_follows;

        let must_create = flags.contains(OpenFlags::CREAT) && flags.contains(OpenFlags::EXCL);

        let mut child_context = context.with(SymlinkMode::NoFollow);
        match parent.lookup_child(&mut child_context, self, basename) {
            Ok(name) => {
                if name.entry.node.info().mode.is_lnk() {
                    if context.symlink_mode == SymlinkMode::NoFollow
                        || context.remaining_follows == 0
                    {
                        if must_create {
                            // Since `must_create` is set, and a node was found, this returns EEXIST
                            // instead of ELOOP.
                            return error!(EEXIST);
                        }
                        // A symlink was found, but too many symlink traversals have been
                        // attempted.
                        return error!(ELOOP);
                    }

                    context.remaining_follows -= 1;
                    match name.entry.node.readlink(self)? {
                        SymlinkTarget::Path(path) => {
                            let dir = if path[0] == b'/' { self.fs.root.clone() } else { parent };
                            self.resolve_open_path(context, dir, &path, mode, flags)
                        }
                        SymlinkTarget::Node(node) => Ok(node),
                    }
                } else {
                    if must_create {
                        return error!(EEXIST);
                    }
                    Ok(name)
                }
            }
            Err(e) if e == errno!(ENOENT) && flags.contains(OpenFlags::CREAT) => {
                if context.must_be_directory {
                    return error!(EISDIR);
                }
                let access = self.fs.apply_umask(mode & FileMode::ALLOW_ALL);
                parent.create_node(&basename, FileMode::IFREG | access, DeviceType::NONE)
            }
            error => error,
        }
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
        let mut flags = flags;
        if flags.contains(OpenFlags::PATH) {
            // When O_PATH is specified in flags, flag bits other than O_CLOEXEC,
            // O_DIRECTORY, and O_NOFOLLOW are ignored.
            const ALLOWED_FLAGS: OpenFlags = OpenFlags::from_bits_truncate(
                OpenFlags::PATH.bits()
                    | OpenFlags::CLOEXEC.bits()
                    | OpenFlags::DIRECTORY.bits()
                    | OpenFlags::NOFOLLOW.bits(),
            );
            flags = flags & ALLOWED_FLAGS;
        }

        let nofollow = flags.contains(OpenFlags::NOFOLLOW);
        let must_create = flags.contains(OpenFlags::CREAT) && flags.contains(OpenFlags::EXCL);

        let symlink_mode =
            if nofollow || must_create { SymlinkMode::NoFollow } else { SymlinkMode::Follow };

        if path.is_empty() {
            return error!(ENOENT);
        }

        let (dir, path) = self.resolve_dir_fd(dir_fd, path)?;
        let mut context = LookupContext::new(symlink_mode);
        context.must_be_directory = flags.contains(OpenFlags::DIRECTORY);
        let name = self.resolve_open_path(&mut context, dir, path, mode, flags)?;

        // Be sure not to reference the mode argument after this point.
        // Below, we shadow the mode argument with the mode of the file we are
        // opening. This line of code will hopefully catch future bugs if we
        // refactor this function.
        std::mem::drop(mode);

        let mode = name.entry.node.info().mode;
        if nofollow && mode.is_lnk() {
            return error!(ELOOP);
        }

        if mode.is_dir() {
            if flags.can_write()
                || flags.contains(OpenFlags::CREAT)
                || flags.contains(OpenFlags::TRUNC)
            {
                return error!(EISDIR);
            }
        } else if context.must_be_directory {
            return error!(ENOTDIR);
        }

        if flags.contains(OpenFlags::TRUNC) && mode.is_reg() {
            // You might think we should check file.can_write() at this
            // point, which is what the docs suggest, but apparently we
            // are supposed to truncate the file if this task can write
            // to the underlying node, even if we are opening the file
            // as read-only. See OpenTest.CanTruncateReadOnly.

            // TODO(security): We should really do an access check for whether
            // this task can write to this file.
            if mode.contains(FileMode::IWUSR) {
                name.entry.node.truncate(0)?;
            }
        }

        name.open(flags)
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
        let mut context = LookupContext::default();
        self.lookup_parent(&mut context, dir, path)
    }

    /// Lookup the parent of a namespace node.
    ///
    /// Consider using Task::open_file_at or Task::lookup_parent_at rather than
    /// calling this function directly.
    ///
    /// This function resolves all but the last component of the given path.
    /// The function returns the parent directory of the last component as well
    /// as the last component.
    ///
    /// If path is empty, this function returns dir and an empty path.
    /// Similarly, if path ends with "." or "..", these components will be
    /// returned along with the parent.
    ///
    /// The returned parent might not be a directory.
    pub fn lookup_parent<'a>(
        &self,
        context: &mut LookupContext,
        dir: NamespaceNode,
        path: &'a FsStr,
    ) -> Result<(NamespaceNode, &'a FsStr), Errno> {
        let mut current_node = dir;
        let mut it = path.split(|c| *c == b'/');
        let mut current_path_component = it.next().unwrap_or(b"");
        while let Some(next_path_component) = it.next() {
            current_node = current_node.lookup_child(context, self, current_path_component)?;
            current_path_component = next_path_component;
        }
        Ok((current_node, current_path_component))
    }

    /// Lookup a namespace node.
    ///
    /// Consider using Task::open_file_at or Task::lookup_parent_at rather than
    /// calling this function directly.
    ///
    /// This function resolves the component of the given path.
    pub fn lookup_path(
        &self,
        context: &mut LookupContext,
        dir: NamespaceNode,
        path: &FsStr,
    ) -> Result<NamespaceNode, Errno> {
        let (parent, basename) = self.lookup_parent(context, dir, path)?;
        parent.lookup_child(context, self, basename)
    }

    /// Lookup a namespace node starting at the root directory.
    ///
    /// Resolves symlinks.
    pub fn lookup_path_from_root(&self, path: &FsStr) -> Result<NamespaceNode, Errno> {
        let mut context = LookupContext::default();
        self.lookup_path(&mut context, self.fs.root.clone(), path)
    }

    pub fn get_task(&self, pid: pid_t) -> Option<Arc<Task>> {
        self.thread_group.kernel.pids.read().get_task(pid)
    }

    pub fn get_pid(&self) -> pid_t {
        self.thread_group.leader
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

        if self.creds.read().has_same_uid(&target.creds.read()) {
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
    pub fn get_zombie_child(&self, selector: TaskSelector) -> Option<ZombieTask> {
        let mut zombie_children = self.zombie_children.lock();
        match selector {
            TaskSelector::Any => zombie_children.pop(),
            TaskSelector::Pid(pid) => zombie_children
                .iter()
                .position(|zombie| zombie.id == pid)
                .map(|pos| zombie_children.remove(pos)),
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

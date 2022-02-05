// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon as zx;
use parking_lot::{Mutex, RwLock};
use std::cmp;
use std::collections::HashSet;
use std::convert::TryFrom;
use std::ffi::CString;
use std::fmt;
use std::marker::PhantomData;
use std::sync::Arc;

use crate::auth::{Credentials, ShellJobControl};
use crate::errno;
use crate::error;
use crate::execution::*;
use crate::from_status_like_fdio;
use crate::fs::*;
use crate::loader::*;
use crate::mm::MemoryManager;
use crate::not_implemented;
use crate::signals::signal_handling::dequeue_signal;
use crate::signals::types::*;
use crate::task::*;
use crate::types::*;

pub struct CurrentTask {
    task: Arc<Task>,

    /// A copy of the registers associated with the Zircon thread. Up-to-date values can be read
    /// from `self.handle.read_state_general_regs()`. To write these values back to the thread, call
    /// `self.handle.write_state_general_regs(self.registers)`.
    pub registers: zx::sys::zx_thread_state_general_regs_t,

    /// The address of the DT_DEBUG entry in the process' ELF file.
    ///
    /// The value of the DT_DEBUG entry is a pointer to the `r_debug` symbol in the dynamic linker.
    /// This struct contains a link map, which the debug agent can use to determine which shared
    /// objects have been loaded.
    ///
    /// The lifecycle of this value is as follows (assuming the tag is present):
    ///   1. Starnix finds the address of the DT_DEBUG entry and sets `debug_address`.
    ///   2. The dynamic linker sets the value of the DT_DEBUG entry to the address of its `r_debug`
    ///      symbol.
    ///   3. Starnix reads the address from the DT_DEBUG entry, writes the address to the process'
    ///      debug address property, and sets `debug_address` to `None`.
    pub dt_debug_address: Option<UserAddress>,

    /// Exists only to prevent Sync from being implemented, since CurrentTask should only be used
    /// on the thread that runs the task. impl !Sync is a compiler error, the message is confusing
    /// but I think it might be nightly only.
    _not_sync: std::marker::PhantomData<std::cell::UnsafeCell<()>>,
}

impl std::ops::Drop for CurrentTask {
    fn drop(&mut self) {
        self.task.destroy();
    }
}

impl std::ops::Deref for CurrentTask {
    type Target = Task;
    fn deref(&self) -> &Self::Target {
        &self.task
    }
}

pub struct Task {
    pub id: pid_t,

    // The command of this task.
    pub command: RwLock<CString>,

    // The arguments with which this task was started.
    pub argv: RwLock<Vec<CString>>,

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

    /// Signal handler related state. This is grouped together for when atomicity is needed during
    /// signal sending and delivery.
    pub signals: RwLock<SignalState>,

    /// The signal this task generates on exit.
    pub exit_signal: Option<Signal>,

    /// The exit code that this task exited with.
    pub exit_code: Mutex<Option<i32>>,

    /// Child tasks that have exited, but not yet been waited for.
    pub zombie_children: Mutex<Vec<ZombieTask>>,
}

#[derive(Clone, Debug, PartialEq)]
pub struct ZombieTask {
    pub id: pid_t,
    pub uid: uid_t,
    pub parent: pid_t,
    pub exit_code: i32,
    // TODO: Do we need exit_signal?
}

impl ZombieTask {
    /// Converts the given exit code to a status code suitable for returning from wait syscalls.
    pub fn wait_status(&self) -> i32 {
        let exit_code = self.exit_code;
        (exit_code & 0xff) << 8
    }

    pub fn as_signal_info(&self) -> SignalInfo {
        SignalInfo::new(
            SIGCHLD,
            CLD_EXITED,
            SignalDetail::SigChld { pid: self.id, uid: self.uid, status: self.wait_status() },
        )
    }
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
    /// Internal function for creating a Task object. Useful when you need to specify the value of
    /// every field. create_process and create_thread are more likely to be what you want.
    ///
    /// Any fields that should be initialized fresh for every task, even if the task was created
    /// with fork, are initialized to their defaults inside this function. All other fields are
    /// passed as parameters.
    fn new(
        id: pid_t,
        comm: CString,
        argv: Vec<CString>,
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
    ) -> CurrentTask {
        CurrentTask::new(Task {
            id,
            command: RwLock::new(comm),
            argv: RwLock::new(argv),
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
            signals: Default::default(),
            exit_signal,
            exit_code: Mutex::new(None),
            zombie_children: Mutex::new(vec![]),
        })
    }

    /// Create a task that is the leader of a new thread group.
    ///
    /// This function creates an underlying Zircon process to host the new
    /// task.
    pub fn create_process_without_parent(
        kernel: &Arc<Kernel>,
        initial_name: CString,
        root_fs: Arc<FsContext>,
    ) -> Result<CurrentTask, Errno> {
        let mut pids = kernel.pids.write();
        let pid = pids.allocate_pid();

        let (thread, thread_group, mm) = create_zircon_process(kernel, pid, &initial_name)?;

        let task = Self::new(
            pid,
            initial_name,
            Vec::new(),
            thread_group,
            1,
            thread,
            FdTable::new(),
            mm,
            root_fs,
            SignalActions::default(),
            Credentials::default(),
            Arc::clone(&kernel.default_abstract_socket_namespace),
            ShellJobControl { sid: pid, pgrp: pid },
            None,
        );

        pids.add_task(&task.task);
        pids.add_thread_group(&task.thread_group);
        Ok(task)
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
    ) -> Result<CurrentTask, Errno> {
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

        let kernel = &self.thread_group.kernel;
        let mut pids = kernel.pids.write();
        let pid = pids.allocate_pid();
        let comm = self.command.read();
        let (thread, thread_group, mm) = if clone_thread {
            create_zircon_thread(self)?
        } else {
            create_zircon_process(kernel, pid, &comm)?
        };

        let child = Self::new(
            pid,
            comm.clone(),
            self.argv.read().clone(),
            thread_group,
            self.id,
            thread,
            files,
            mm,
            fs,
            signal_actions,
            self.creds.read().clone(),
            self.abstract_socket_namespace.clone(),
            self.shell_job_control.clone(),
            child_exit_signal,
        );
        pids.add_task(&child.task);

        if clone_thread {
            self.thread_group.add(&child);
        } else {
            pids.add_thread_group(&child.thread_group);
            child.signals.write().alt_stack = self.signals.read().alt_stack;
            self.mm.snapshot_to(&child.mm)?;
        }

        if flags & (CLONE_PARENT_SETTID as u64) != 0 {
            self.mm.write_object(user_parent_tid, &child.id)?;
        }

        if flags & (CLONE_CHILD_CLEARTID as u64) != 0 {
            *child.clear_child_tid.lock() = user_child_tid;
        }

        if flags & (CLONE_CHILD_SETTID as u64) != 0 {
            child.mm.write_object(user_child_tid, &child.id)?;
        }

        self.children.write().insert(child.id);

        Ok(child)
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

    /// Called by the Drop trait on CurrentTask.
    fn destroy(self: &Arc<Self>) {
        let _ignored = self.clear_child_tid_if_needed();
        self.thread_group.remove(self);
        if let Some(parent) = self.get_task(self.parent) {
            parent.remove_child(self.id);
        }
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

    pub fn as_ucred(&self) -> ucred {
        let creds = self.creds.read();
        ucred { pid: self.get_pid(), uid: creds.uid, gid: creds.gid }
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
        if Signal::try_from(unchecked_signal) == Ok(SIGCONT) {
            return true;
        }

        false
    }

    pub fn as_zombie(&self) -> ZombieTask {
        ZombieTask {
            id: self.id,
            uid: self.creds.read().uid,
            parent: self.parent,
            exit_code: self
                .exit_code
                .lock()
                .expect("a process should not be exiting without an exit code"),
            // TODO(tbodt): This expect condition can currently trip if the process crashes.
            // There needs to be an exit code of some kind in this case too.
        }
    }

    /// Removes and returns any zombie task with the specified `pid`, if such a zombie exists.
    pub fn get_zombie_child(
        &self,
        selector: TaskSelector,
        should_remove_zombie: bool,
    ) -> Option<ZombieTask> {
        let mut zombie_children = self.zombie_children.lock();
        match selector {
            TaskSelector::Any => {
                if zombie_children.len() > 0 {
                    Some(zombie_children.len() - 1)
                } else {
                    None
                }
            }
            TaskSelector::Pid(pid) => zombie_children.iter().position(|zombie| zombie.id == pid),
        }
        .map(|pos| {
            if should_remove_zombie {
                zombie_children.remove(pos)
            } else {
                zombie_children[pos].clone()
            }
        })
    }

    fn remove_child(&self, pid: pid_t) {
        self.children.write().remove(&pid);
    }
}

impl CurrentTask {
    fn new(task: Task) -> CurrentTask {
        CurrentTask {
            task: Arc::new(task),
            dt_debug_address: None,
            registers: zx::sys::zx_thread_state_general_regs_t::default(),
            _not_sync: PhantomData,
        }
    }

    pub fn kernel(&self) -> &Arc<Kernel> {
        &self.thread_group.kernel
    }

    pub fn task_arc_clone(&self) -> Arc<Task> {
        Arc::clone(&self.task)
    }

    /// Sets the task's signal mask to `signal_mask` and runs `wait_function`.
    ///
    /// Signals are dequeued prior to the original signal mask being restored.
    ///
    /// The returned result is the result returned from the wait function.
    pub fn wait_with_temporary_mask<F, T>(
        &mut self,
        signal_mask: u64,
        wait_function: F,
    ) -> Result<T, Errno>
    where
        F: FnOnce(&CurrentTask) -> Result<T, Errno>,
    {
        let old_mask = self.signals.write().set_signal_mask(signal_mask);
        let wait_result = wait_function(self);
        dequeue_signal(self);
        self.signals.write().set_signal_mask(old_mask);
        wait_result
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
        match parent.lookup_child(self, &mut child_context, basename) {
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

        name.open(&*self.kernel(), flags)
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
    fn lookup_parent<'a>(
        &self,
        context: &mut LookupContext,
        dir: NamespaceNode,
        path: &'a FsStr,
    ) -> Result<(NamespaceNode, &'a FsStr), Errno> {
        let mut current_node = dir;
        let mut it = path.split(|c| *c == b'/');
        let mut current_path_component = it.next().unwrap_or(b"");
        while let Some(next_path_component) = it.next() {
            current_node = current_node.lookup_child(self, context, current_path_component)?;
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
        parent.lookup_child(self, context, basename)
    }

    /// Lookup a namespace node starting at the root directory.
    ///
    /// Resolves symlinks.
    pub fn lookup_path_from_root(&self, path: &FsStr) -> Result<NamespaceNode, Errno> {
        let mut context = LookupContext::default();
        self.lookup_path(&mut context, self.fs.root.clone(), path)
    }

    pub fn exec(
        &mut self,
        path: CString,
        argv: Vec<CString>,
        environ: Vec<CString>,
    ) -> Result<(), Errno> {
        let executable = self.open_file(path.to_bytes(), OpenFlags::RDONLY)?;
        let resolved_elf = resolve_executable(self, executable, path.clone(), argv, environ)?;
        if let Err(err) = self.finish_exec(path, resolved_elf) {
            // TODO(tbodt): Replace this panic with a log and force a SIGSEGV.
            panic!("{:?} unrecoverable error in exec: {}", self, err);
        }
        Ok(())
    }

    /// After the memory is unmapped, any failure in exec is unrecoverable and results in the
    /// process crashing. This function is for that second half; any error returned from this
    /// function will be considered unrecoverable.
    fn finish_exec(&mut self, path: CString, resolved_elf: ResolvedElf) -> Result<(), Errno> {
        *self.argv.write() = resolved_elf.argv.clone();
        self.mm
            .exec(resolved_elf.file.name.clone())
            .map_err(|status| from_status_like_fdio!(status))?;
        let start_info = load_executable(self, resolved_elf, &path)?;
        self.registers = start_info.to_registers();
        self.dt_debug_address = start_info.dt_debug_address;

        self.signals.write().alt_stack = None;

        // TODO: All threads other than the calling thread are destroyed.

        // TODO: The file descriptor table is unshared, undoing the effect of
        //       the CLONE_FILES flag of clone(2).
        //
        // To make this work, we can put the files in an RwLock and then cache
        // a reference to the files on the CurrentTask. That will let
        // functions that have CurrentTask access the FdTable without
        // needing to grab the read-lock.
        //
        // For now, we do not implement that behavior.
        self.files.exec();

        // TODO: The dispositions of any signals that are being caught are
        //       reset to the default.

        // TODO: POSIX timers are not preserved.

        // TODO: The termination signal is reset to SIGCHLD.

        self.thread_group.set_name(&path)?;

        // Get the basename of the path, which will be used as the name displayed with
        // `prtcl(PR_GET_NAME)` and `/proc/self/stat`
        let basename = if let Some(idx) = memchr::memrchr(b'/', path.to_bytes()) {
            // SAFETY: Substring of a CString will contain no null bytes.
            CString::new(&path.to_bytes()[idx + 1..]).unwrap()
        } else {
            path
        };
        self.set_command_name(basename);
        Ok(())
    }

    pub fn set_command_name(&mut self, name: CString) {
        // Truncate to 16 bytes, including null byte.
        let bytes = name.to_bytes();
        *self.command.write() = if bytes.len() > 15 {
            // SAFETY: Substring of a CString will contain no null bytes.
            CString::new(&bytes[..15]).unwrap()
        } else {
            name
        };
    }
}

impl fmt::Debug for Task {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}[{}]", self.id, self.command.read().to_string_lossy())
    }
}

impl fmt::Debug for CurrentTask {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        self.task.fmt(f)
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
        let (kernel, current_task) = create_kernel_and_task();

        assert_eq!(current_task.get_tid(), 1);
        let another_current = create_task(&kernel, "another-task");
        assert_eq!(another_current.get_tid(), 2);

        let pids = kernel.pids.read();
        assert_eq!(pids.get_task(1).unwrap().get_tid(), 1);
        assert_eq!(pids.get_task(2).unwrap().get_tid(), 2);
        assert!(pids.get_task(3).is_none());
    }
}

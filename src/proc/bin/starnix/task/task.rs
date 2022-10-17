// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon as zx;
use once_cell::sync::OnceCell;
use std::cmp;
use std::convert::TryFrom;
use std::ffi::CString;
use std::fmt;
use std::marker::PhantomData;
use std::sync::Arc;

use crate::auth::*;
use crate::execution::*;
use crate::fs::*;
use crate::loader::*;
use crate::lock::{RwLock, RwLockReadGuard, RwLockWriteGuard};
use crate::logging::{not_implemented, set_zx_name};
use crate::mm::MemoryManager;
use crate::signals::signal_handling::dequeue_signal;
use crate::signals::types::*;
use crate::syscalls::SyscallResult;
use crate::task::*;
use crate::types::*;

pub struct CurrentTask {
    pub task: Arc<Task>,

    /// A copy of the registers associated with the Zircon thread. Up-to-date values can be read
    /// from `self.handle.read_state_general_regs()`. To write these values back to the thread, call
    /// `self.handle.write_state_general_regs(self.registers.into())`.
    pub registers: RegisterState,

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

    /// A custom function to resume a syscall that has been interrupted by SIGSTOP.
    /// To use, call set_syscall_restart_func and return ERESTART_RESTARTBLOCK. sys_restart_syscall
    /// will eventually call it.
    pub syscall_restart_func: Option<Box<SyscallRestartFunc>>,

    /// Exists only to prevent Sync from being implemented, since CurrentTask should only be used
    /// on the thread that runs the task. impl !Sync is a compiler error, the message is confusing
    /// but I think it might be nightly only.
    _not_sync: std::marker::PhantomData<std::cell::UnsafeCell<()>>,
}

type SyscallRestartFunc = dyn FnOnce(&mut CurrentTask) -> Result<SyscallResult, Errno> + Send;

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

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum ExitStatus {
    Exit(u8),
    Kill(SignalInfo),
    CoreDump(SignalInfo),
    Stop(SignalInfo),
    Continue(SignalInfo),
}
impl ExitStatus {
    /// Converts the given exit status to a status code suitable for returning from wait syscalls.
    pub fn wait_status(&self) -> i32 {
        match self {
            ExitStatus::Exit(status) => (*status as i32) << 8,
            ExitStatus::Kill(siginfo) => siginfo.signal.number() as i32,
            ExitStatus::CoreDump(siginfo) => (siginfo.signal.number() as i32) | 0x80,
            ExitStatus::Continue(_) => 0xffff,
            ExitStatus::Stop(siginfo) => 0x7f + ((siginfo.signal.number() as i32) << 8),
        }
    }

    pub fn signal_info_code(&self) -> u32 {
        match self {
            ExitStatus::Exit(_) => CLD_EXITED,
            ExitStatus::Kill(_) => CLD_KILLED,
            ExitStatus::CoreDump(_) => CLD_DUMPED,
            ExitStatus::Stop(_) => CLD_STOPPED,
            ExitStatus::Continue(_) => CLD_CONTINUED,
        }
    }

    pub fn signal_info_status(&self) -> i32 {
        match self {
            ExitStatus::Exit(status) => *status as i32,
            ExitStatus::Kill(siginfo)
            | ExitStatus::CoreDump(siginfo)
            | ExitStatus::Continue(siginfo)
            | ExitStatus::Stop(siginfo) => siginfo.signal.number() as i32,
        }
    }
}

pub struct TaskMutableState {
    /// The arguments with which this task was started.
    pub argv: Vec<CString>,

    // See https://man7.org/linux/man-pages/man2/set_tid_address.2.html
    pub clear_child_tid: UserRef<pid_t>,

    /// Signal handler related state. This is grouped together for when atomicity is needed during
    /// signal sending and delivery.
    pub signals: SignalState,

    /// The exit status that this task exited with.
    pub exit_status: Option<ExitStatus>,

    /// Whether the executor should dump the stack of this task when it exits. Currently used to
    /// implement ExitStatus::CoreDump.
    pub dump_on_exit: bool,
}

pub struct Task {
    pub id: pid_t,

    /// The thread group to which this task belongs.
    pub thread_group: Arc<ThreadGroup>,

    /// A handle to the underlying Zircon thread object.
    pub thread: RwLock<Option<zx::Thread>>,

    /// The file descriptor table for this task.
    pub files: Arc<FdTable>,

    /// The memory manager for this task.
    pub mm: Arc<MemoryManager>,

    /// The file system for this task.
    ///
    /// The only case when this is not set is for the initial task while the FsContext is built.
    fs: OnceCell<Arc<FsContext>>,

    /// The namespace for abstract AF_UNIX sockets for this task.
    pub abstract_socket_namespace: Arc<AbstractUnixSocketNamespace>,

    /// The namespace for AF_VSOCK for this task.
    pub abstract_vsock_namespace: Arc<AbstractVsockSocketNamespace>,

    /// The signal this task generates on exit.
    pub exit_signal: Option<Signal>,

    /// The mutable state of the Task.
    mutable_state: RwLock<TaskMutableState>,

    /// The command of this task.
    ///
    /// This is guarded by its own lock because it is use to display the task description, and
    /// other locks may be taken at the time. No other lock may be held while this lock is taken.
    command: RwLock<CString>,

    /// The security credentials for this task.
    ///
    /// This is necessary because credentials need to be read from operations on Node, and other
    /// locks may be taken at the time. No other lock may be held while this lock is taken.
    creds: RwLock<Credentials>,
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
        command: CString,
        argv: Vec<CString>,
        thread_group: Arc<ThreadGroup>,
        thread: Option<zx::Thread>,
        files: Arc<FdTable>,
        mm: Arc<MemoryManager>,
        // The only case where fs should be None if when building the initial task that is the
        // used to build the initial FsContext.
        fs: Option<Arc<FsContext>>,
        creds: Credentials,
        abstract_socket_namespace: Arc<AbstractUnixSocketNamespace>,
        abstract_vsock_namespace: Arc<AbstractVsockSocketNamespace>,
        exit_signal: Option<Signal>,
    ) -> Self {
        let fs = {
            let result = OnceCell::new();
            if let Some(fs) = fs {
                result.get_or_init(|| fs);
            }
            result
        };
        let task = Task {
            id,
            thread_group,
            thread: RwLock::new(thread),
            files,
            mm,
            fs,
            abstract_socket_namespace,
            abstract_vsock_namespace,
            exit_signal,
            command: RwLock::new(command),
            creds: RwLock::new(creds),
            mutable_state: RwLock::new(TaskMutableState {
                argv,
                clear_child_tid: UserRef::default(),
                signals: Default::default(),
                exit_status: None,
                dump_on_exit: false,
            }),
        };
        #[cfg(any(test, debug_assertions))]
        {
            let _l1 = task.read();
            let _l2 = task.command.read();
            let _l3 = task.creds.read();
        }
        task
    }

    /// Access mutable state with a read lock.
    pub fn read(&self) -> RwLockReadGuard<'_, TaskMutableState> {
        self.mutable_state.read()
    }

    /// Access mutable state with a write lock.
    pub fn write(&self) -> RwLockWriteGuard<'_, TaskMutableState> {
        self.mutable_state.write()
    }

    pub fn kernel(&self) -> &Arc<Kernel> {
        &self.thread_group.kernel
    }

    pub fn creds(&self) -> Credentials {
        self.creds.read().clone()
    }

    pub fn set_creds(&self, creds: Credentials) {
        *self.creds.write() = creds;
    }

    pub fn fs(&self) -> &Arc<FsContext> {
        self.fs.get().unwrap()
    }

    pub fn set_fs(&self, fs: Arc<FsContext>) {
        self.fs.set(fs).map_err(|_| "Cannot set fs multiple times").unwrap();
    }

    /// Create a task that is the leader of a new thread group.
    ///
    /// This function creates an underlying Zircon process to host the new
    /// task.
    ///
    /// root_fs should only be None for the init task, and set_fs should be called as soon as the
    /// FsContext is build.
    pub fn create_process_without_parent(
        kernel: &Arc<Kernel>,
        initial_name: CString,
        root_fs: Option<Arc<FsContext>>,
    ) -> Result<CurrentTask, Errno> {
        let mut pids = kernel.pids.write();
        let pid = pids.allocate_pid();

        let process_group = ProcessGroup::new(pid, None);
        pids.add_process_group(&process_group);

        let TaskInfo { thread, thread_group, memory_manager } = create_zircon_process(
            kernel,
            None,
            pid,
            process_group.clone(),
            SignalActions::default(),
            &initial_name,
        )?;
        process_group.insert(&thread_group);

        let current_task = CurrentTask::new(Self::new(
            pid,
            initial_name,
            Vec::new(),
            thread_group,
            thread,
            FdTable::new(),
            memory_manager,
            root_fs,
            Credentials::root(),
            Arc::clone(&kernel.default_abstract_socket_namespace),
            Arc::clone(&kernel.default_abstract_vsock_namespace),
            None,
        ));
        current_task.thread_group.add(&current_task.task)?;

        pids.add_task(&current_task.task);
        pids.add_thread_group(&current_task.thread_group);
        Ok(current_task)
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
            not_implemented!(self, "CLONE_VM without CLONE_THREAD is not implemented");
            return error!(ENOSYS);
        }

        if flags & !IMPLEMENTED_FLAGS != 0 {
            not_implemented!(
                self,
                "clone does not implement flags: 0x{:x}",
                flags & !IMPLEMENTED_FLAGS
            );
            return error!(ENOSYS);
        }

        let raw_child_exist_signal = flags & (CSIGNAL as u64);
        let child_exit_signal = if raw_child_exist_signal == 0 {
            None
        } else {
            Some(Signal::try_from(UncheckedSignal::new(raw_child_exist_signal))?)
        };

        let fs = if flags & (CLONE_FS as u64) != 0 { self.fs().clone() } else { self.fs().fork() };
        let files =
            if flags & (CLONE_FILES as u64) != 0 { self.files.clone() } else { self.files.fork() };

        let kernel = &self.thread_group.kernel;
        let mut pids = kernel.pids.write();

        let pid;
        let command;
        let argv;
        let creds;
        let TaskInfo { thread, thread_group, memory_manager } = {
            // Make sure to drop these locks ASAP to avoid inversion
            let thread_group_state = self.thread_group.write();
            let state = self.read();

            pid = pids.allocate_pid();
            command = self.command();
            argv = state.argv.clone();
            creds = self.creds();
            if clone_thread {
                create_zircon_thread(self)?
            } else {
                // Drop the lock on this task before entering `create_zircon_process`, because it will
                // take a lock on the new thread group, and locks on thread groups have a higher
                // priority than locks on the task in the thread group.
                std::mem::drop(state);
                let signal_actions = if clone_sighand {
                    self.thread_group.signal_actions.clone()
                } else {
                    self.thread_group.signal_actions.fork()
                };
                let process_group = thread_group_state.process_group.clone();
                create_zircon_process(
                    kernel,
                    Some(thread_group_state),
                    pid,
                    process_group,
                    signal_actions,
                    &command,
                )?
            }
        };

        let child = CurrentTask::new(Self::new(
            pid,
            command,
            argv,
            thread_group,
            thread,
            files,
            memory_manager,
            Some(fs),
            creds,
            self.abstract_socket_namespace.clone(),
            self.abstract_vsock_namespace.clone(),
            child_exit_signal,
        ));

        // Drop the pids lock as soon as possible after creating the child. Destroying the child
        // and removing it from the pids table itself requires the pids lock, so if an early exit
        // takes place we have a self deadlock.
        pids.add_task(&child.task);
        if !clone_thread {
            pids.add_thread_group(&child.thread_group);
        }
        std::mem::drop(pids);

        // Child lock must be taken before this lock. Drop the lock on the task, take a writable
        // lock on the child and take the current state back.

        #[cfg(any(test, debug_assertions))]
        {
            // Take the lock on the thread group and its child in the correct order to ensure any wrong ordering
            // will trigger the tracing-mutex at the right call site.
            if !clone_thread {
                let _l1 = self.thread_group.read();
                let _l2 = child.thread_group.read();
            }
        }

        if clone_thread {
            self.thread_group.add(&child.task)?;
        } else {
            child.thread_group.add(&child.task)?;
            let mut child_state = child.write();
            let state = self.read();
            child_state.signals.alt_stack = state.signals.alt_stack;
            child_state.signals.mask = state.signals.mask;
            self.mm.snapshot_to(&child.mm)?;
            copy_process_debug_addr(&self.thread_group.process, &child.thread_group.process)?;
        }

        if flags & (CLONE_PARENT_SETTID as u64) != 0 {
            self.mm.write_object(user_parent_tid, &child.id)?;
        }

        if flags & (CLONE_CHILD_CLEARTID as u64) != 0 {
            child.write().clear_child_tid = user_child_tid;
        }

        if flags & (CLONE_CHILD_SETTID as u64) != 0 {
            child.mm.write_object(user_child_tid, &child.id)?;
        }

        Ok(child)
    }

    #[cfg(test)]
    pub fn clone_task_for_test(&self, flags: u64) -> CurrentTask {
        let result = self
            .clone_task(flags, UserRef::default(), UserRef::default())
            .expect("failed to create task in test");

        // Take the lock on thread group and task in the correct order to ensure any wrong ordering
        // will trigger the tracing-mutex at the right call site.
        {
            let _l1 = result.thread_group.read();
            let _l2 = result.read();
        }

        result
    }

    /// If needed, clear the child tid for this task.
    ///
    /// Userspace can ask us to clear the child tid and issue a futex wake at
    /// the child tid address when we tear down a task. For example, bionic
    /// uses this mechanism to implement pthread_join. The thread that calls
    /// pthread_join sleeps using FUTEX_WAIT on the child tid address. We wake
    /// them up here to let them know the thread is done.
    fn clear_child_tid_if_needed(&self) -> Result<(), Errno> {
        let mut state = self.write();
        let user_tid = state.clear_child_tid;
        if !user_tid.is_null() {
            let zero: pid_t = 0;
            self.mm.write_object(user_tid, &zero)?;
            self.mm.futex.wake(user_tid.addr(), usize::MAX, FUTEX_BITSET_MATCH_ANY);
            state.clear_child_tid = UserRef::default();
        }
        Ok(())
    }

    /// Called by the Drop trait on CurrentTask.
    fn destroy(self: &Arc<Self>) {
        let _ignored = self.clear_child_tid_if_needed();
        self.thread_group.remove(self);
    }

    pub fn get_task(&self, pid: pid_t) -> Option<Arc<Task>> {
        self.thread_group.kernel.pids.read().get_task(pid)
    }

    pub fn get_pid(&self) -> pid_t {
        self.thread_group.leader
    }

    pub fn get_tid(&self) -> pid_t {
        self.id
    }

    pub fn as_ucred(&self) -> ucred {
        let creds = self.creds();
        ucred { pid: self.get_pid(), uid: creds.uid, gid: creds.gid }
    }

    pub fn as_fscred(&self) -> FsCred {
        let creds = self.creds();
        FsCred { uid: creds.euid, gid: creds.egid }
    }

    pub fn can_signal(&self, target: &Task, unchecked_signal: &UncheckedSignal) -> bool {
        // If both the tasks share a thread group the signal can be sent. This is not documented
        // in kill(2) because kill does not support task-level granularity in signal sending.
        if self.thread_group == target.thread_group {
            return true;
        }

        let self_creds = self.creds();

        if self_creds.has_capability(CAP_KILL) {
            return true;
        }

        if self_creds.has_same_uid(&target.creds()) {
            return true;
        }

        // TODO(lindkvist): This check should also verify that the sessions are the same.
        if Signal::try_from(unchecked_signal) == Ok(SIGCONT) {
            return true;
        }

        false
    }

    /// Interrupts the current task.
    ///
    /// This will interrupt any blocking syscalls if the task is blocked on one.
    /// The signal_state of the task must not be locked.
    ///
    /// TODO(qsr): This should also interrupt any running code.
    pub fn interrupt(&self) {
        self.read().signals.waiter.access(|waiter| {
            if let Some(waiter) = waiter {
                waiter.interrupt()
            }
        })
    }

    pub fn command(&self) -> CString {
        self.command.read().clone()
    }

    pub fn set_command_name(&self, name: CString) {
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

impl CurrentTask {
    fn new(task: Task) -> CurrentTask {
        CurrentTask {
            task: Arc::new(task),
            dt_debug_address: None,
            registers: RegisterState::default(),
            syscall_restart_func: None,
            _not_sync: PhantomData,
        }
    }

    pub fn task_arc_clone(&self) -> Arc<Task> {
        Arc::clone(&self.task)
    }

    pub fn set_syscall_restart_func<R: Into<SyscallResult>>(
        &mut self,
        f: impl FnOnce(&mut CurrentTask) -> Result<R, Errno> + Send + 'static,
    ) {
        self.syscall_restart_func = Some(Box::new(|current_task| Ok(f(current_task)?.into())));
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
        let old_mask = self.write().signals.set_signal_mask(signal_mask);
        let wait_result = wait_function(self);
        dequeue_signal(self);
        self.write().signals.set_signal_mask(old_mask);
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
            self.fs().root()
        } else if dir_fd == FdNumber::AT_FDCWD {
            self.fs().cwd()
        } else {
            let file = self.files.get(dir_fd)?;
            file.name.clone()
        };
        if !path.is_empty() {
            if !dir.entry.node.is_dir() {
                return error!(ENOTDIR);
            }
            dir.entry.node.check_access(self, Access::EXEC)?;
        }
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
    ///
    /// This returns the resolved node, and a boolean indicating whether the node has been created.
    fn resolve_open_path(
        &self,
        context: &mut LookupContext,
        dir: NamespaceNode,
        path: &FsStr,
        mode: FileMode,
        flags: OpenFlags,
    ) -> Result<(NamespaceNode, bool), Errno> {
        let path = context.update_for_path(path);
        let mut parent_content = context.with(SymlinkMode::Follow);
        let (parent, basename) = self.lookup_parent(&mut parent_content, dir, path)?;
        context.remaining_follows = parent_content.remaining_follows;

        let must_create = flags.contains(OpenFlags::CREAT) && flags.contains(OpenFlags::EXCL);

        let mut child_context = context.with(SymlinkMode::NoFollow);
        match parent.lookup_child(self, &mut child_context, basename) {
            Ok(name) => {
                if name.entry.node.is_lnk() {
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
                            let dir = if path[0] == b'/' { self.fs().root() } else { parent };
                            self.resolve_open_path(context, dir, &path, mode, flags)
                        }
                        SymlinkTarget::Node(node) => Ok((node, false)),
                    }
                } else {
                    if must_create {
                        return error!(EEXIST);
                    }
                    Ok((name, false))
                }
            }
            Err(e) if e == errno!(ENOENT) && flags.contains(OpenFlags::CREAT) => {
                if context.must_be_directory {
                    return error!(EISDIR);
                }
                Ok((
                    parent.create_node(
                        self,
                        basename,
                        mode.with_type(FileMode::IFREG),
                        DeviceType::NONE,
                    )?,
                    true,
                ))
            }
            Err(e) => Err(e),
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
            flags &= ALLOWED_FLAGS;
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
        let (name, created) = self.resolve_open_path(&mut context, dir, path, mode, flags)?;

        // Be sure not to reference the mode argument after this point.
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

        if flags.contains(OpenFlags::TRUNC) && mode.is_reg() && !created {
            // You might think we should check file.can_write() at this
            // point, which is what the docs suggest, but apparently we
            // are supposed to truncate the file if this task can write
            // to the underlying node, even if we are opening the file
            // as read-only. See OpenTest.CanTruncateReadOnly.
            name.entry.node.truncate(self, 0)?;
        }

        // If the node has been created, the open operation should not verify access right:
        // From <https://man7.org/linux/man-pages/man2/open.2.html>
        //
        // > Note that mode applies only to future accesses of the newly created file; the
        // > open() call that creates a read-only file may well return a  read/write  file
        // > descriptor.

        name.open(self, flags, !created)
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
        for next_path_component in it {
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
        self.lookup_path(&mut context, self.fs().root(), path)
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
            panic!("{:?} unrecoverable error in exec: {:?}", self, err);
        }
        Ok(())
    }

    /// After the memory is unmapped, any failure in exec is unrecoverable and results in the
    /// process crashing. This function is for that second half; any error returned from this
    /// function will be considered unrecoverable.
    fn finish_exec(&mut self, path: CString, resolved_elf: ResolvedElf) -> Result<(), Errno> {
        self.mm
            .exec(resolved_elf.file.name.clone())
            .map_err(|status| from_status_like_fdio!(status))?;
        self.write().argv = resolved_elf.argv.clone();
        let start_info = load_executable(self, resolved_elf, &path)?;
        self.registers = start_info.to_registers().into();
        self.dt_debug_address = start_info.dt_debug_address;

        {
            let mut state = self.write();
            let mut creds = self.creds.write();
            state.signals.alt_stack = None;

            // TODO(tbodt): Check whether capability xattrs are set on the file, and grant/limit
            // capabilities accordingly.
            creds.exec();
        }

        self.thread_group.signal_actions.reset_for_exec();

        // TODO: The termination signal is reset to SIGCHLD.

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

        // TODO: POSIX timers are not preserved.

        self.thread_group.write().did_exec = true;
        set_zx_name(&self.thread_group.process, path.as_bytes());
        set_zx_name(&fuchsia_runtime::thread_self(), path.as_bytes());

        // Get the basename of the path, which will be used as the name displayed with
        // `prctl(PR_GET_NAME)` and `/proc/self/stat`
        let basename = if let Some(idx) = memchr::memrchr(b'/', path.to_bytes()) {
            // SAFETY: Substring of a CString will contain no null bytes.
            CString::new(&path.to_bytes()[idx + 1..]).unwrap()
        } else {
            path
        };
        self.set_command_name(basename);
        Ok(())
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
        ptr == other_ptr
    }
}

impl cmp::Eq for Task {}

/// The state of the task's registers when the thread of execution entered the kernel.
/// This is a thin wrapper around [`zx::sys::zx_thread_state_general_regs_t`] that also stores
/// a copy of `rax` in `orig_rax`.
///
/// Implements [`std::ops::Deref`] and [`std::ops::DerefMut`] as a way to get at the underlying
/// [`zx::sys::zx_thread_state_general_regs_t`] that this type wraps.
#[cfg(target_arch = "x86_64")]
#[derive(Default, Debug, Clone, Copy, Eq, PartialEq)]
pub struct RegisterState {
    real_registers: zx::sys::zx_thread_state_general_regs_t,

    /// A copy of the `rax` register at the time of the `syscall` instruction. This is important to
    /// store, as the return value of a syscall overwrites `rax`, making it impossible to recover
    /// the original syscall number in the case of syscall restart and strace output.
    pub orig_rax: u64,
}

impl std::ops::Deref for RegisterState {
    type Target = zx::sys::zx_thread_state_general_regs_t;

    fn deref(&self) -> &Self::Target {
        &self.real_registers
    }
}

impl std::ops::DerefMut for RegisterState {
    fn deref_mut(&mut self) -> &mut Self::Target {
        &mut self.real_registers
    }
}

impl From<zx::sys::zx_thread_state_general_regs_t> for RegisterState {
    fn from(regs: zx::sys::zx_thread_state_general_regs_t) -> Self {
        RegisterState { real_registers: regs, orig_rax: 0 }
    }
}

impl From<RegisterState> for zx::sys::zx_thread_state_general_regs_t {
    fn from(register_state: RegisterState) -> Self {
        register_state.real_registers
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::testing::*;

    #[::fuchsia::test]
    fn test_tid_allocation() {
        let (kernel, current_task) = create_kernel_and_task();

        assert_eq!(current_task.get_tid(), 1);
        let another_current = create_task(&kernel, "another-task");
        assert_eq!(another_current.get_tid(), 2);

        let pids = kernel.pids.read();
        assert_eq!(pids.get_task(1).unwrap().get_tid(), 1);
        assert_eq!(pids.get_task(2).unwrap().get_tid(), 2);
        assert!(pids.get_task(3).is_none());
    }

    #[::fuchsia::test]
    fn test_clone_pid_and_parent_pid() {
        let (_kernel, current_task) = create_kernel_and_task();
        let thread =
            current_task.clone_task_for_test((CLONE_THREAD | CLONE_VM | CLONE_SIGHAND) as u64);
        assert_eq!(current_task.get_pid(), thread.get_pid());
        assert_ne!(current_task.get_tid(), thread.get_tid());
        assert_eq!(current_task.thread_group.leader, thread.thread_group.leader);

        let child_task = current_task.clone_task_for_test(0);
        assert_ne!(current_task.get_pid(), child_task.get_pid());
        assert_ne!(current_task.get_tid(), child_task.get_tid());
        assert_eq!(current_task.get_pid(), child_task.thread_group.read().get_ppid());
    }

    #[::fuchsia::test]
    fn test_root_capabilities() {
        let (_kernel, current_task) = create_kernel_and_task();
        assert!(current_task.creds().has_capability(CAP_SYS_ADMIN));
        current_task
            .set_creds(Credentials::from_passwd("foo:x:1:1").expect("Credentials::from_passwd"));
        assert!(!current_task.creds().has_capability(CAP_SYS_ADMIN));
    }
}

// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(not(feature = "restricted_mode"))]

use anyhow::Error;
use fuchsia_zircon as zx;
use fuchsia_zircon::sys::{
    zx_exception_info_t, ZX_EXCEPTION_STATE_HANDLED, ZX_EXCEPTION_STATE_THREAD_EXIT,
    ZX_EXCEPTION_STATE_TRY_NEXT, ZX_EXCP_FATAL_PAGE_FAULT, ZX_EXCP_POLICY_CODE_BAD_SYSCALL,
    ZX_EXCP_POLICY_ERROR,
};
use fuchsia_zircon::{AsHandleRef, Task as zxTask};
use std::ffi::CString;
use std::mem;
use std::sync::Arc;

use super::shared::*;
use crate::logging::set_zx_name;
use crate::logging::strace;
use crate::mm::MemoryManager;
use crate::signals::*;
use crate::syscalls::decls::{Syscall, SyscallDecl};
use crate::syscalls::table::dispatch_syscall;
use crate::task::*;
use crate::types::*;

/// Contains context to track the most recently failing system call.
///
/// When a task exits with a non-zero exit code, this context is logged to help debugging which
/// system call may have triggered the failure.
struct ErrorContext {
    /// The system call that failed.
    syscall: Syscall,

    /// The error that was returned for the system call.
    error: Errno,
}

/// Spawns a thread that executes `current_task`.
///
/// The `current_task` is expected to be initialized before calling `execute_task`. This means
/// the process or thread associated with the task must have already been created, but not started.
///
/// # Parameters
/// - `current_task`: The task to start executing.
/// - `task_complete`: A function that is called when the task finishes executing. The result
///                    contains the exit status of the task.
pub fn execute_task<F>(mut current_task: CurrentTask, task_complete: F)
where
    F: FnOnce(Result<ExitStatus, Error>) + Send + Sync + 'static,
{
    std::thread::spawn(move || {
        task_complete(|| -> Result<ExitStatus, Error> {
            let exceptions = start_task_thread(&current_task)?;
            // Unwrap the error because if we don't, we'll panic anyway from destroying the task
            // without having previous called sys_exit(), and that will swallow the actual error.
            match run_exception_loop(&mut current_task, exceptions) {
                Err(error) => {
                    tracing::warn!(
                        "process {:?} died unexpectedly from {:?}! treating as SIGKILL",
                        current_task,
                        error
                    );
                    let exit_status = ExitStatus::Kill(SignalInfo::default(SIGKILL));
                    current_task.write().exit_status = Some(exit_status.clone());
                    Ok(exit_status)
                }
                ok => ok,
            }
        }());
    });
}

/// Writes the registers from `current_task` to `current_task.thread` and starts executing the
/// thread.
///
/// # Parameters
/// - `current_task`: The task to start the thread for. If the task is the thread group leader, the
///                   process starts instead.
///
/// Returns an exception channel for the thread. This channel is used to monitor, and handle,
/// "bad syscall" exceptions.
fn start_task_thread(current_task: &CurrentTask) -> Result<zx::Channel, zx::Status> {
    let exceptions = current_task.thread.create_exception_channel()?;
    let suspend_token = current_task.thread.suspend()?;
    if current_task.id == current_task.thread_group.leader {
        current_task.thread_group.process.start(
            &current_task.thread,
            0,
            0,
            zx::Handle::invalid(),
            0,
        )?;
    } else {
        current_task.thread.start(0, 0, 0, 0)?;
    }
    current_task.thread.wait_handle(zx::Signals::THREAD_SUSPENDED, zx::Time::INFINITE)?;
    current_task.thread.write_state_general_regs(current_task.registers.into())?;
    mem::drop(suspend_token);
    set_zx_name(&fuchsia_runtime::thread_self(), current_task.command().as_bytes());
    Ok(exceptions)
}

/// Block the exception handler as long as the task is stopped and not terminated.
fn block_while_stopped(task: &Task) {
    loop {
        if task.read().exit_status.is_some() {
            return;
        }
        if !task.thread_group.read().stopped {
            return;
        }
        // Result is not needed, as this is not in a syscall.
        let _: Result<(), Errno> = Waiter::new_for_stopped_thread().wait(task);
    }
}

/// Runs an exception handling loop for the given task.
///
/// The task is expected to already have been started. This function listens to
/// the exception channel for the process (`exceptions`) and handles each
///  exception by:
///
///   - verifying that the exception represents a `ZX_EXCP_POLICY_CODE_BAD_SYSCALL`
///   - reading the thread's registers
///   - executing the appropriate syscall
///   - setting the thread's registers to their post-syscall values
///   - setting the exception state to `ZX_EXCEPTION_STATE_HANDLED`
///
/// Once this function has completed, the process' exit status is returned and can be read from
/// `process_context.exit_status`.
fn run_exception_loop(
    current_task: &mut CurrentTask,
    exceptions: zx::Channel,
) -> Result<ExitStatus, Error> {
    let mut buffer = zx::MessageBuf::new();
    // This tracks the last failing system call to aid in debugging task failures.
    let mut error_context = None;

    loop {
        read_channel_sync(&exceptions, &mut buffer)?;

        let info = as_exception_info(&buffer);
        assert!(buffer.n_handles() == 1);
        let exception = zx::Exception::from(buffer.take_handle(0).unwrap());

        let thread = exception.get_thread()?;
        assert!(
            thread.get_koid() == current_task.thread.get_koid(),
            "Exception thread did not match task thread."
        );

        let report = thread.get_exception_report()?;
        current_task.registers = thread.read_state_general_regs()?.into();

        let syscall_decl = SyscallDecl::from_number(report.context.synth_data as u64);
        // The `rax` register read from the thread's state is clobbered by zircon with
        // ZX_ERR_BAD_SYSCALL, but it really should be the syscall number.
        current_task.registers.rax = syscall_decl.number;

        // `orig_rax` should hold the original value loaded into `rax` by the userspace process.
        current_task.registers.orig_rax = syscall_decl.number;

        let regs = &current_task.registers;
        match info.type_ {
            ZX_EXCP_POLICY_ERROR
                if report.context.synth_code == ZX_EXCP_POLICY_CODE_BAD_SYSCALL =>
            {
                let start_time = zx::Time::get_monotonic();
                let args = (regs.rdi, regs.rsi, regs.rdx, regs.r10, regs.r8, regs.r9);

                let syscall = Syscall {
                    decl: syscall_decl,
                    arg0: args.0,
                    arg1: args.1,
                    arg2: args.2,
                    arg3: args.3,
                    arg4: args.4,
                    arg5: args.5,
                };
                strace!(current_task, "{:?}", syscall);

                match dispatch_syscall(current_task, syscall.decl.number, args) {
                    Ok(return_value) => {
                        strace!(
                            current_task,
                            "-> {:#x} ({}ms)",
                            return_value.value(),
                            (zx::Time::get_monotonic() - start_time).into_millis()
                        );
                        current_task.registers.rax = return_value.value();
                    }
                    Err(errno) => {
                        strace!(
                            current_task,
                            "!-> {:?} ({}ms)",
                            errno,
                            (zx::Time::get_monotonic() - start_time).into_millis()
                        );
                        current_task.registers.rax = errno.return_value();
                        error_context = Some(ErrorContext { error: errno, syscall });
                    }
                }
            }

            ZX_EXCP_FATAL_PAGE_FAULT => {
                #[cfg(target_arch = "x86_64")]
                let fault_addr = unsafe { report.context.arch.x86_64.cr2 };
                tracing::debug!(
                    "{:?} page fault, ip={:#x}, sp={:#x}, fault={:#x}",
                    current_task,
                    current_task.registers.rip,
                    current_task.registers.rsp,
                    fault_addr
                );
                force_signal(&current_task, SignalInfo::default(SIGSEGV));
            }

            _ => {
                tracing::warn!("unhandled exception. info={:?} report.header={:?} synth_code={:?} synth_data={:?}", info, report.header, report.context.synth_code, report.context.synth_data);
                exception.set_exception_state(&ZX_EXCEPTION_STATE_TRY_NEXT)?;
                continue;
            }
        }

        block_while_stopped(current_task);

        // Checking for a signal might cause the task to exit, so check before processing exit
        if current_task.read().exit_status.is_none() {
            dequeue_signal(current_task);
        }

        {
            let task_state = current_task.read();
            if let Some(exit_status) = task_state.exit_status.as_ref() {
                let exit_status = exit_status.clone();
                match exit_status {
                    ExitStatus::CoreDump(_) => {
                        exception.set_exception_state(&ZX_EXCEPTION_STATE_TRY_NEXT)?
                    }
                    _ => exception.set_exception_state(&ZX_EXCEPTION_STATE_THREAD_EXIT)?,
                }

                // `strace!` acquires a read lock on `current_task`'s state, so drop the lock to
                // avoid re-entrancy.
                drop(task_state);
                tracing::debug!("{:?} exiting with status {:?}", current_task, exit_status);
                if let Some(error_context) = error_context {
                    match exit_status {
                        ExitStatus::Exit(value) if value == 0 => {}
                        _ => {
                            tracing::debug!(
                                "{:?} last failing syscall before exit: {:?}, failed with {:?}",
                                current_task,
                                error_context.syscall,
                                error_context.error
                            );
                        }
                    };
                }
                return Ok(exit_status);
            }
        }

        // Handle the debug address after the thread is set up to continue, because
        // `set_process_debug_addr` expects the register state to be in a post-syscall state (most
        // importantly the instruction pointer needs to be "correct").
        set_process_debug_addr(current_task)?;

        thread.write_state_general_regs(current_task.registers.into())?;
        exception.set_exception_state(&ZX_EXCEPTION_STATE_HANDLED)?;
    }
}

/// Creates a thread for a task with the given `parent`.
///
/// The `parent` is used to determine which process to create the thread in.
///
/// Returns the created thread, the thread group to which it belongs, and a memory manager to use
/// for the created thread.
pub fn create_zircon_thread(
    parent: &Task,
) -> Result<(zx::Thread, Arc<ThreadGroup>, Arc<MemoryManager>), Errno> {
    let thread = parent
        .thread_group
        .process
        .create_thread(parent.command().as_bytes())
        .map_err(|status| from_status_like_fdio!(status))?;

    let thread_group = parent.thread_group.clone();
    let mm = parent.mm.clone();
    Ok((thread, thread_group, mm))
}

/// Creates a new process in the job associated with `kernel`.
///
/// # Parameters
/// - `kernel`: The kernel that is used to fetch the appropriate job to creat the process in.
/// - `pid`: The pid to give the created process' thread group.
/// - `name`: The name of the process.
///
/// Returns a thread in the created process, the thread group that contains the process, and a
/// memory manager to use for the returned thread.
pub fn create_zircon_process(
    kernel: &Arc<Kernel>,
    parent: Option<ThreadGroupWriteGuard<'_>>,
    pid: pid_t,
    process_group: Arc<ProcessGroup>,
    signal_actions: Arc<SignalActions>,
    name: &CString,
) -> Result<(zx::Thread, Arc<ThreadGroup>, Arc<MemoryManager>), Errno> {
    let (process, root_vmar) = kernel
        .job
        .create_child_process(name.as_bytes())
        .map_err(|status| from_status_like_fdio!(status))?;
    let thread =
        process.create_thread(name.as_bytes()).map_err(|status| from_status_like_fdio!(status))?;

    let mm =
        Arc::new(MemoryManager::new(root_vmar).map_err(|status| from_status_like_fdio!(status))?);

    let thread_group =
        ThreadGroup::new(kernel.clone(), process, parent, pid, process_group, signal_actions);

    Ok((thread, thread_group, mm))
}

/// Converts a `zx::MessageBuf` into an exception info by transmuting a copy of the bytes.
// TODO: Should we move this code into fuchsia_zircon? It seems like part of a better abstraction
// for exception channels.
fn as_exception_info(buffer: &zx::MessageBuf) -> zx_exception_info_t {
    let mut tmp = [0; mem::size_of::<zx_exception_info_t>()];
    tmp.clone_from_slice(buffer.bytes());
    unsafe { mem::transmute(tmp) }
}

/// Reads from `chan` into `buf`.
///
/// If the initial read returns `SHOULD_WAIT`, the function waits for the channel to be readable and
/// tries again (once).
fn read_channel_sync(chan: &zx::Channel, buf: &mut zx::MessageBuf) -> Result<(), zx::Status> {
    let res = chan.read(buf);
    if let Err(zx::Status::SHOULD_WAIT) = res {
        chan.wait_handle(
            zx::Signals::CHANNEL_READABLE | zx::Signals::CHANNEL_PEER_CLOSED,
            zx::Time::INFINITE,
        )?;
        chan.read(buf)
    } else {
        res
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::testing::*;

    #[::fuchsia::test]
    fn test_block_while_stopped_stop_and_continue() {
        let (_kernel, task) = create_kernel_and_task();

        // block_while_stopped must immediately returned if the task is not stopped.
        block_while_stopped(&task);

        // Stop the task.
        task.thread_group.set_stopped(true, SignalInfo::default(SIGSTOP));

        let cloned_task = task.task_arc_clone();
        let thread = std::thread::spawn(move || {
            // Block until continued.
            block_while_stopped(&cloned_task);
        });

        // Wait for the task to have a waiter.
        while !task.read().signals.waiter.is_valid() {
            std::thread::sleep(std::time::Duration::from_millis(10));
        }

        // Continue the task.
        task.thread_group.set_stopped(false, SignalInfo::default(SIGCONT));

        // Join the task, which will ensure block_while_stopped terminated.
        thread.join().expect("joined");

        // The task should not be blocked anymore.
        block_while_stopped(&task);
    }

    #[::fuchsia::test]
    fn test_block_while_stopped_stop_and_exit() {
        let (_kernel, task) = create_kernel_and_task();

        // block_while_stopped must immediately returned if the task is neither stopped nor exited.
        block_while_stopped(&task);

        // Stop the task.
        task.thread_group.set_stopped(true, SignalInfo::default(SIGSTOP));

        let cloned_task = task.task_arc_clone();
        let thread = std::thread::spawn(move || {
            // Block until continued.
            block_while_stopped(&cloned_task);
        });

        // Wait for the task to have a waiter.
        while !task.read().signals.waiter.is_valid() {
            std::thread::sleep(std::time::Duration::from_millis(10));
        }

        // exit the task.
        task.thread_group.exit(ExitStatus::Exit(1));

        // Join the task, which will ensure block_while_stopped terminated.
        thread.join().expect("joined");

        // The task should not be blocked because it is stopped.
        block_while_stopped(&task);
    }
}

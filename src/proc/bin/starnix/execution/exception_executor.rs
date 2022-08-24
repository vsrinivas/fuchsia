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
use crate::mm::MemoryManager;
use crate::signals::*;
use crate::syscalls::decls::SyscallDecl;
use crate::task::*;
use crate::types::*;

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
    let thread_lock = current_task.thread.read();
    let thread = thread_lock.as_ref().ok_or(zx::Status::BAD_HANDLE)?;
    let exceptions = thread.create_exception_channel()?;
    let suspend_token = thread.suspend()?;
    if current_task.id == current_task.thread_group.leader {
        current_task.thread_group.process.start(&thread, 0, 0, zx::Handle::invalid(), 0)?;
    } else {
        thread.start(0, 0, 0, 0)?;
    }
    thread.wait_handle(zx::Signals::THREAD_SUSPENDED, zx::Time::INFINITE)?;
    thread.write_state_general_regs(current_task.registers.into())?;
    mem::drop(suspend_token);
    set_zx_name(&fuchsia_runtime::thread_self(), current_task.command().as_bytes());
    Ok(exceptions)
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
            // The thread is always `Some` at this point.
            thread.get_koid() == current_task.thread.read().as_ref().unwrap().get_koid(),
            "Exception thread did not match task thread."
        );

        let report = thread.get_exception_report()?;
        current_task.registers = thread.read_state_general_regs()?.into();

        let syscall_decl = SyscallDecl::from_number(report.context.synth_data as u64);

        match info.type_ {
            ZX_EXCP_POLICY_ERROR
                if report.context.synth_code == ZX_EXCP_POLICY_CODE_BAD_SYSCALL =>
            {
                if let Some(new_error_context) = execute_syscall(current_task, syscall_decl) {
                    error_context = Some(new_error_context);
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

        if let Some(exit_status) = process_completed_syscall(current_task, &error_context)? {
            match exit_status {
                ExitStatus::CoreDump(_) => {
                    exception.set_exception_state(&ZX_EXCEPTION_STATE_TRY_NEXT)?
                }
                _ => exception.set_exception_state(&ZX_EXCEPTION_STATE_THREAD_EXIT)?,
            }

            return Ok(exit_status);
        }

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
) -> Result<(Option<zx::Thread>, Arc<ThreadGroup>, Arc<MemoryManager>), Errno> {
    let thread = parent
        .thread_group
        .process
        .create_thread(parent.command().as_bytes())
        .map_err(|status| from_status_like_fdio!(status))?;

    let thread_group = parent.thread_group.clone();
    let mm = parent.mm.clone();
    Ok((Some(thread), thread_group, mm))
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
) -> Result<(Option<zx::Thread>, Arc<ThreadGroup>, Arc<MemoryManager>), Errno> {
    let (process, root_vmar) = kernel
        .job
        .create_child_process(zx::ProcessOptions::empty(), name.as_bytes())
        .map_err(|status| from_status_like_fdio!(status))?;
    let thread =
        process.create_thread(name.as_bytes()).map_err(|status| from_status_like_fdio!(status))?;

    let mm =
        Arc::new(MemoryManager::new(root_vmar).map_err(|status| from_status_like_fdio!(status))?);

    let thread_group =
        ThreadGroup::new(kernel.clone(), process, parent, pid, process_group, signal_actions);

    Ok((Some(thread), thread_group, mm))
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
            // Wait for the task to have a waiter.
            while !cloned_task.read().signals.waiter.is_valid() {
                std::thread::sleep(std::time::Duration::from_millis(10));
            }

            // Continue the task.
            cloned_task.thread_group.set_stopped(false, SignalInfo::default(SIGCONT));
        });

        // Block until continued.
        block_while_stopped(&task);

        // Join the thread, which will ensure set_stopped terminated.
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
            // Wait for the task to have a waiter.
            while !cloned_task.read().signals.waiter.is_valid() {
                std::thread::sleep(std::time::Duration::from_millis(10));
            }

            // exit the task.
            cloned_task.thread_group.exit(ExitStatus::Exit(1));
        });

        // Block until continued.
        block_while_stopped(&task);

        // Join the task, which will ensure thread_group.exit terminated.
        thread.join().expect("joined");

        // The task should not be blocked because it is stopped.
        block_while_stopped(&task);
    }
}

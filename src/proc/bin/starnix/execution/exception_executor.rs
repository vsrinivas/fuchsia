// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(not(feature = "restricted_mode"))]

use anyhow::Error;
use fuchsia_zircon::{
    self as zx, sys::zx_exception_info_t, sys::ZX_EXCEPTION_STATE_HANDLED,
    sys::ZX_EXCEPTION_STATE_THREAD_EXIT, sys::ZX_EXCEPTION_STATE_TRY_NEXT,
    sys::ZX_EXCP_POLICY_CODE_BAD_SYSCALL, sys::ZX_EXCP_POLICY_ERROR, AsHandleRef, Task as zxTask,
};
use log::info;
use std::ffi::CString;
use std::mem;
use std::sync::Arc;

use super::shared::*;
use crate::errno;
use crate::from_status_like_fdio;
use crate::mm::MemoryManager;
use crate::signals::signal_handling::*;
use crate::strace;
use crate::syscalls::decls::SyscallDecl;
use crate::syscalls::table::dispatch_syscall;
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
///                    contains the exit code of the task.
pub fn execute_task<F>(mut current_task: CurrentTask, task_complete: F)
where
    F: FnOnce(Result<i32, Error>) + Send + Sync + 'static,
{
    std::thread::spawn(move || {
        task_complete(|| -> Result<i32, Error> {
            let exceptions = start_task_thread(&current_task)?;
            // Unwrap the error because if we don't, we'll panic anyway from destroying the task
            // without having previous called sys_exit(), and that will swallow the actual error.
            Ok(run_exception_loop(&mut current_task, exceptions).unwrap())
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
    current_task.thread.write_state_general_regs(current_task.registers)?;
    mem::drop(suspend_token);
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
/// Once this function has completed, the process' exit code (if one is available) can be read from
/// `process_context.exit_code`.
fn run_exception_loop(
    current_task: &mut CurrentTask,
    exceptions: zx::Channel,
) -> Result<i32, Error> {
    let mut buffer = zx::MessageBuf::new();
    loop {
        read_channel_sync(&exceptions, &mut buffer)?;

        let info = as_exception_info(&buffer);
        assert!(buffer.n_handles() == 1);
        let exception = zx::Exception::from(buffer.take_handle(0).unwrap());

        if info.type_ != ZX_EXCP_POLICY_ERROR {
            info!("exception type: 0x{:x}", info.type_);
            exception.set_exception_state(&ZX_EXCEPTION_STATE_TRY_NEXT)?;
            continue;
        }

        let thread = exception.get_thread()?;
        assert!(
            thread.get_koid() == current_task.thread.get_koid(),
            "Exception thread did not match task thread."
        );

        let report = thread.get_exception_report()?;
        if report.context.synth_code != ZX_EXCP_POLICY_CODE_BAD_SYSCALL {
            info!("exception synth_code: {}", report.context.synth_code);
            exception.set_exception_state(&ZX_EXCEPTION_STATE_TRY_NEXT)?;
            continue;
        }

        let syscall_number = report.context.synth_data as u64;
        current_task.registers = thread.read_state_general_regs()?;

        let regs = &current_task.registers;
        let args = (regs.rdi, regs.rsi, regs.rdx, regs.r10, regs.r8, regs.r9);
        strace!(
            current_task,
            "{}({:#x}, {:#x}, {:#x}, {:#x}, {:#x}, {:#x})",
            SyscallDecl::from_number(syscall_number).name,
            args.0,
            args.1,
            args.2,
            args.3,
            args.4,
            args.5
        );
        match dispatch_syscall(current_task, syscall_number, args) {
            Ok(return_value) => {
                strace!(current_task, "-> {:#x}", return_value.value());
                current_task.registers.rax = return_value.value();
            }
            Err(errno) => {
                strace!(current_task, "!-> {}", errno);
                current_task.registers.rax = (-errno.value()) as u64;
            }
        }

        dequeue_signal(current_task);

        if let Some(exit_code) = *current_task.exit_code.lock() {
            strace!(current_task, "exiting with status {:#x}", exit_code);
            exception.set_exception_state(&ZX_EXCEPTION_STATE_THREAD_EXIT)?;
            return Ok(exit_code);
        }

        // Handle the debug address after the thread is set up to continue, because
        // `set_process_debug_addr` expects the register state to be in a post-syscall state (most
        // importantly the instruction pointer needs to be "correct").
        set_process_debug_addr(current_task)?;

        thread.write_state_general_regs(current_task.registers)?;
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
        .create_thread(parent.command.read().as_bytes())
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
    pid: pid_t,
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

    let thread_group = Arc::new(ThreadGroup::new(kernel.clone(), process, pid));

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

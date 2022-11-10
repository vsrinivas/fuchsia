// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(non_camel_case_types)]
#![cfg(feature = "restricted_mode")]

use anyhow::{format_err, Error};
use fuchsia_zircon as zx;
use fuchsia_zircon::AsHandleRef;
use std::ffi::CString;
use std::sync::Arc;

use super::shared::{execute_syscall, process_completed_syscall, TaskInfo};
use crate::logging::{log_warn, set_zx_name};
use crate::mm::MemoryManager;
use crate::signals::{SignalActions, SignalInfo};
use crate::syscalls::decls::SyscallDecl;
use crate::task::{
    CurrentTask, ExitStatus, Kernel, ProcessGroup, Task, ThreadGroup, ThreadGroupWriteGuard,
};
use crate::types::*;

extern "C" {
    /// The function which enters restricted mode. This function never technically returns, instead
    /// it saves some register state and calls `zx_restricted_enter`. When the thread traps in
    /// restricted mode, the kernel returns control to `restricted_return`, `restricted_return` then
    /// restores the register state and returns back out to where `restricted_enter` was called
    /// from.
    fn restricted_enter(options: u32, restricted_return: usize) -> zx::sys::zx_status_t;

    /// The function that is used to "return" from restricted mode. See `restricted_enter` for more
    /// information.
    fn restricted_return();

    /// `zx_restricted_write_state` system call.
    fn zx_restricted_write_state(buffer: *const u8, buffer_size: usize) -> zx::sys::zx_status_t;

    /// `zx_restricted_read_state` system call.
    fn zx_restricted_read_state(buffer: *mut u8, buffer_size: usize) -> zx::sys::zx_status_t;

    /// Sets the process handle used to create new threads, for the current thread.
    fn thrd_set_zx_process(handle: zx::sys::zx_handle_t) -> zx::sys::zx_handle_t;
}

/// Runs the `current_task` to completion.
///
/// The high-level flow of this function looks as follows:
///
///   1. Write the restricted state for the current thread to set it up to enter into the restricted
///      (Linux) part of the address space.
///   2. Enter restricted mode.
///   3. Return from restricted mode, reading out the new state of the restricted mode execution.
///      This state contains the thread's restricted register state, which is used to determine
///      which system call to dispatch.
///   4. Dispatch the system call.
///   5. Handle pending signals.
///   6. Goto 1.
fn run_task(current_task: &mut CurrentTask) -> Result<ExitStatus, Error> {
    set_zx_name(&fuchsia_runtime::thread_self(), current_task.command().as_bytes());
    // The task does not yet have a thread associated with it, so associate it with this thread.
    *current_task.thread.write() =
        Some(fuchsia_runtime::thread_self().duplicate(zx::Rights::SAME_RIGHTS).unwrap());

    // This is the pointer that is passed to `restricted_enter`.
    let restricted_return_ptr = restricted_return as *const ();

    // This tracks the last failing system call for debugging purposes.
    let mut error_context = None;

    loop {
        let mut state = zx::sys::zx_restricted_state_t::from(&*current_task.registers);
        match unsafe {
            zx_restricted_write_state(
                restricted_state_as_bytes(&mut state).as_ptr(),
                std::mem::size_of::<zx::sys::zx_restricted_state_t>(),
            )
        } {
            zx::sys::ZX_OK => {
                // Wrote restricted state successfully.
            }
            _ => return Err(format_err!("failed to zx_restricted_write_state: {:?}", state)),
        }

        match unsafe { restricted_enter(0, restricted_return_ptr as usize) } {
            zx::sys::ZX_OK => {
                // Successfully entered and exited restricted mode. At this point the task has
                // trapped back out of restricted mode, so the restricted state contains the
                // information about which system call to dispatch.
            }
            _ => return Err(format_err!("failed to restricted_enter: {:?}", state)),
        }

        match unsafe {
            zx_restricted_read_state(
                restricted_state_as_bytes(&mut state).as_mut_ptr(),
                std::mem::size_of::<zx::sys::zx_restricted_state_t>(),
            )
        } {
            zx::sys::ZX_OK => {
                // Read restricted state successfully.
            }
            _ => return Err(format_err!("failed to zx_restricted_read_state: {:?}", state)),
        }

        // Store the new register state in the current task before dispatching the system call.
        current_task.registers = zx::sys::zx_thread_state_general_regs_t::from(&state).into();
        let syscall_decl = SyscallDecl::from_number(state.rax);

        if let Some(new_error_context) = execute_syscall(current_task, syscall_decl) {
            error_context = Some(new_error_context);
        }

        if let Some(exit_status) = process_completed_syscall(current_task, &error_context)? {
            return Ok(exit_status);
        }
    }
}

/// Returns a mutable reference to `state` as bytes. Used to read and write restricted state from
/// the kernel.
fn restricted_state_as_bytes(state: &mut zx::sys::zx_restricted_state_t) -> &mut [u8] {
    unsafe {
        std::slice::from_raw_parts_mut(
            (state as *mut zx::sys::zx_restricted_state_t) as *mut u8,
            std::mem::size_of::<zx::sys::zx_restricted_state_t>(),
        )
    }
}

/// Note: This does not actually create a Zircon thread. It creates a thread group and memory
/// manager. The exception executor does use this to create an actual thread, but once that executor
/// is removed this function can be renamed/reworked.
pub fn create_zircon_thread(parent: &Task) -> Result<TaskInfo, Errno> {
    let thread_group = parent.thread_group.clone();
    let memory_manager = parent.mm.clone();
    Ok(TaskInfo { thread: None, thread_group, memory_manager })
}

pub fn create_zircon_process(
    kernel: &Arc<Kernel>,
    parent: Option<ThreadGroupWriteGuard<'_>>,
    pid: pid_t,
    process_group: Arc<ProcessGroup>,
    signal_actions: Arc<SignalActions>,
    name: &CString,
) -> Result<TaskInfo, Errno> {
    let (process, root_vmar) = kernel
        .starnix_process
        .create_shared(zx::ProcessOptions::empty(), name.as_bytes())
        .map_err(|status| from_status_like_fdio!(status))?;

    let debug_addr =
        kernel.starnix_process.get_debug_addr().map_err(|status| from_status_like_fdio!(status))?;
    process.set_debug_addr(&debug_addr).map_err(|status| from_status_like_fdio!(status))?;

    let memory_manager =
        Arc::new(MemoryManager::new(root_vmar).map_err(|status| from_status_like_fdio!(status))?);

    let thread_group =
        ThreadGroup::new(kernel.clone(), process, parent, pid, process_group, signal_actions);

    Ok(TaskInfo { thread: None, thread_group, memory_manager })
}

pub fn execute_task<F>(mut current_task: CurrentTask, task_complete: F)
where
    F: FnOnce(Result<ExitStatus, Error>) + Send + Sync + 'static,
{
    // Set the process handle to the new task's process, so the new thread is spawned in that
    // process.
    let process_handle = current_task.thread_group.process.raw_handle();
    let old_process_handle = unsafe { thrd_set_zx_process(process_handle) };

    // Spawn the process' thread. Note, this closure ends up executing in the process referred to by
    // `process_handle`.
    std::thread::spawn(move || {
        let run_result = match run_task(&mut current_task) {
            Err(error) => {
                log_warn!(current_task, "Died unexpectedly from {:?}! treating as SIGKILL", error);
                let exit_status = ExitStatus::Kill(SignalInfo::default(SIGKILL));
                current_task.write().exit_status = Some(exit_status.clone());
                Ok(exit_status)
            }
            ok => ok,
        };

        task_complete(run_result);
    });

    // Reset the process handle used to create threads.
    unsafe {
        thrd_set_zx_process(old_process_handle);
    };
}

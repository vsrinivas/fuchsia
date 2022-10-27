// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{anyhow, Context, Error};
use fidl::endpoints::ServerEnd;
use fidl_fuchsia_io as fio;
use fidl_fuchsia_process as fprocess;
use fidl_fuchsia_starnix_developer as fstardev;
use fuchsia_runtime::{HandleInfo, HandleType};
use fuchsia_zircon::{self as zx, sys::ZX_PROCESS_DEBUG_ADDR_BREAK_ON_SET};
use process_builder::elf_parse;
use std::convert::TryFrom;
use std::sync::Arc;
use zerocopy::AsBytes;

use crate::fs::ext4::ExtFilesystem;
use crate::fs::fuchsia::{create_file_from_handle, RemoteFs, SyslogFile};
use crate::fs::*;
use crate::logging::strace;
use crate::mm::MemoryManager;
use crate::mm::{DesiredAddress, MappingOptions, PAGE_SIZE};
use crate::signals::dequeue_signal;
use crate::syscalls::{
    decls::{Syscall, SyscallDecl},
    table::dispatch_syscall,
};
use crate::task::*;
use crate::types::*;
use crate::vmex_resource::VMEX_RESOURCE;

/// Contains context to track the most recently failing system call.
///
/// When a task exits with a non-zero exit code, this context is logged to help debugging which
/// system call may have triggered the failure.
pub struct ErrorContext {
    /// The system call that failed.
    pub syscall: Syscall,

    /// The error that was returned for the system call.
    pub error: Errno,
}

/// Result returned when creating new Zircon threads and processes for tasks.
pub struct TaskInfo {
    /// The thread that was created for the task.
    pub thread: Option<zx::Thread>,

    /// The thread group that the task should be added to.
    pub thread_group: Arc<ThreadGroup>,

    /// The memory manager to use for the task.
    pub memory_manager: Arc<MemoryManager>,
}

/// Executes the provided `syscall` in `current_task`.
///
/// Returns an `ErrorContext` if the system call returned an error.
pub fn execute_syscall(
    current_task: &mut CurrentTask,
    syscall_decl: &'static SyscallDecl,
) -> Option<ErrorContext> {
    let syscall = Syscall {
        decl: syscall_decl,
        arg0: current_task.registers.rdi,
        arg1: current_task.registers.rsi,
        arg2: current_task.registers.rdx,
        arg3: current_task.registers.r10,
        arg4: current_task.registers.r8,
        arg5: current_task.registers.r9,
    };

    // The `rax` register read from the thread's state is clobbered by zircon with
    // ZX_ERR_BAD_SYSCALL, but it really should be the syscall number.
    current_task.registers.rax = syscall.decl.number;

    // `orig_rax` should hold the original value loaded into `rax` by the userspace process.
    current_task.registers.orig_rax = syscall.decl.number;

    strace!(current_task, "{:?}", syscall);
    match dispatch_syscall(current_task, &syscall) {
        Ok(return_value) => {
            strace!(current_task, "-> {:#x}", return_value.value(),);
            current_task.registers.rax = return_value.value();
            None
        }
        Err(errno) => {
            strace!(current_task, "!-> {:?}", errno,);
            current_task.registers.rax = errno.return_value();
            Some(ErrorContext { error: errno, syscall })
        }
    }
}

/// Finishes `current_task` updates after system call dispatch.
///
/// Returns an `ExitStatus` if the task is meant to exit.
pub fn process_completed_syscall(
    current_task: &mut CurrentTask,
    error_context: &Option<ErrorContext>,
) -> Result<Option<ExitStatus>, Errno> {
    // Checking for a signal might cause the task to exit, so check before processing exit
    if current_task.read().exit_status.is_none() {
        dequeue_signal(current_task);
    }

    if let Some(exit_status) = current_task.read().exit_status.as_ref() {
        tracing::debug!("{:?} exiting with status {:?}", current_task, exit_status);
        if let Some(error_context) = error_context {
            match exit_status {
                ExitStatus::Exit(value) if *value == 0 => {}
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
        return Ok(Some(exit_status.clone()));
    }

    // Block a stopped process after it's had a chance to handle signals, since a signal might
    // cause it to stop.
    block_while_stopped(current_task);

    // Handle the debug address after the thread is set up to continue, because
    // `set_process_debug_addr` expects the register state to be in a post-syscall state (most
    // importantly the instruction pointer needs to be "correct").
    set_process_debug_addr(current_task)?;

    Ok(None)
}

/// Sets the ZX_PROP_PROCESS_DEBUG_ADDR of the process.
///
/// Sets the process property if a valid address is found in the `DT_DEBUG` entry. If the existing
/// value of ZX_PROP_PROCESS_DEBUG_ADDR is set to ZX_PROCESS_DEBUG_ADDR_BREAK_ON_SET, the task will
/// be set up to trigger a software interrupt (for the debugger to catch) before resuming execution
/// at the current instruction pointer.
///
/// If the property is set on the process (i.e., nothing fails and the values are valid),
/// `current_task.debug_address` will be cleared.
///
/// # Parameters:
/// - `current_task`: The task to set the property for. The register's of this task, the instruction
///                   pointer specifically, needs to be set to the value with which the task is
///                   expected to resume.
pub fn set_process_debug_addr(current_task: &mut CurrentTask) -> Result<(), Errno> {
    let dt_debug_address = match current_task.dt_debug_address {
        Some(dt_debug_address) => dt_debug_address,
        // The DT_DEBUG entry does not exist, or has already been read and set on the process.
        None => return Ok(()),
    };

    // The debug_addres is the pointer located at DT_DEBUG.
    let debug_address: elf_parse::Elf64Dyn =
        current_task.mm.read_object(UserRef::new(dt_debug_address))?;
    if debug_address.value == 0 {
        // The DT_DEBUG entry is present, but has not yet been set by the linker, check next time.
        return Ok(());
    }

    let existing_debug_addr = current_task
        .thread_group
        .process
        .get_debug_addr()
        .map_err(|err| from_status_like_fdio!(err))?;
    let debug_addr = current_task
        .kernel()
        .starnix_process
        .get_debug_addr()
        .map_err(|status| from_status_like_fdio!(status))?;

    // If existing_debug_addr != ZX_PROCESS_DEBUG_ADDR_BREAK_ON_SET then there is no reason to
    // insert the interrupt.
    if existing_debug_addr != ZX_PROCESS_DEBUG_ADDR_BREAK_ON_SET as u64 {
        // Still set the debug address, and clear the debug address from `current_task` to avoid
        // entering this function again.
        match current_task.thread_group.process.set_debug_addr(&debug_addr) {
            Err(zx::Status::ACCESS_DENIED) => {}
            status => status.map_err(|err| from_status_like_fdio!(err))?,
        };
        current_task.dt_debug_address = None;
        return Ok(());
    }

    // An executable VMO is mapped into the process, which does two things:
    //   1. Issues a software interrupt caught by the debugger.
    //   2. Jumps back to the current instruction pointer of the thread.
    #[cfg(target_arch = "x86_64")]
    const INTERRUPT_AND_JUMP: [u8; 7] = [
        0xcc, // int 3
        0xff, 0x25, 0x00, 0x00, 0x00, 0x00, // jmp *0x0(%rip)
    ];
    let mut instruction_pointer = current_task.registers.rip.as_bytes().to_owned();
    let mut instructions = INTERRUPT_AND_JUMP.to_vec();
    instructions.append(&mut instruction_pointer);

    let vmo = Arc::new(
        zx::Vmo::create(*PAGE_SIZE)
            .and_then(|vmo| vmo.replace_as_executable(&VMEX_RESOURCE))
            .map_err(|err| from_status_like_fdio!(err))?,
    );
    vmo.write(&instructions, 0).map_err(|e| from_status_like_fdio!(e))?;

    let instruction_pointer = current_task.mm.map(
        DesiredAddress::Hint(UserAddress::default()),
        vmo,
        0,
        instructions.len(),
        zx::VmarFlags::PERM_EXECUTE | zx::VmarFlags::PERM_READ,
        MappingOptions::empty(),
        None,
    )?;

    current_task.registers.rip = instruction_pointer.ptr() as u64;
    current_task
        .thread_group
        .process
        .set_debug_addr(&debug_addr)
        .map_err(|err| from_status_like_fdio!(err))?;
    current_task.dt_debug_address = None;

    Ok(())
}

pub fn copy_process_debug_addr(
    source_process: &zx::Process,
    target_process: &zx::Process,
) -> Result<(), Errno> {
    let source_debug_addr =
        source_process.get_debug_addr().map_err(|err| from_status_like_fdio!(err))?;
    let target_debug_addr =
        target_process.get_debug_addr().map_err(|err| from_status_like_fdio!(err))?;

    // TODO: Handle the case where either of the debug address requires to set an interrupt.
    if source_debug_addr == ZX_PROCESS_DEBUG_ADDR_BREAK_ON_SET as u64 {
        return Ok(());
    }
    if target_debug_addr == ZX_PROCESS_DEBUG_ADDR_BREAK_ON_SET as u64 {
        return Ok(());
    }
    match target_process.set_debug_addr(&source_debug_addr) {
        Err(zx::Status::ACCESS_DENIED) => {}
        status => status.map_err(|err| from_status_like_fdio!(err))?,
    };
    Ok(())
}

pub struct StartupHandles {
    pub shell_controller: Option<ServerEnd<fstardev::ShellControllerMarker>>,
}

/// Creates a `StartupHandles` from the provided handles.
///
/// The `numbered_handles` of type `HandleType::FileDescriptor` are used to
/// create files, and the handles are required to be of type `zx::Socket`.
///
/// If there is a `numbered_handles` of type `HandleType::User0`, that is
/// interpreted as the server end of the ShellController protocol.
pub fn parse_numbered_handles(
    current_task: &CurrentTask,
    numbered_handles: Option<Vec<fprocess::HandleInfo>>,
    files: &Arc<FdTable>,
) -> Result<StartupHandles, Error> {
    let mut shell_controller = None;
    if let Some(numbered_handles) = numbered_handles {
        for numbered_handle in numbered_handles {
            let info = HandleInfo::try_from(numbered_handle.id)?;
            if info.handle_type() == HandleType::FileDescriptor {
                files.insert(
                    FdNumber::from_raw(info.arg().into()),
                    create_file_from_handle(current_task, numbered_handle.handle)?,
                );
            } else if info.handle_type() == HandleType::User0 {
                shell_controller = Some(ServerEnd::<fstardev::ShellControllerMarker>::from(
                    numbered_handle.handle,
                ));
            }
        }
    } else {
        // If no numbered handles are provided default 0, 1, and 2 to a syslog file.
        let stdio = SyslogFile::new_file(current_task);
        files.insert(FdNumber::from_raw(0), stdio.clone());
        files.insert(FdNumber::from_raw(1), stdio.clone());
        files.insert(FdNumber::from_raw(2), stdio);
    }
    Ok(StartupHandles { shell_controller })
}

/// Create a filesystem to access the content of the fuchsia directory available at `fs_src` inside
/// `pkg`.
pub fn create_remotefs_filesystem(
    kernel: &Kernel,
    root: &fio::DirectorySynchronousProxy,
    rights: fio::OpenFlags,
    fs_src: &str,
) -> Result<FileSystemHandle, Error> {
    let root = syncio::directory_open_directory_async(root, fs_src, rights)
        .map_err(|e| anyhow!("Failed to open root: {}", e))?;
    RemoteFs::new_fs(kernel, root.into_channel(), rights).map_err(|e| e.into())
}

/// Returns a hash representing the fuchsia package `pkg`.
///
/// The implementation is hashing /meta/content
pub fn get_pkg_hash(pkg: &fio::DirectorySynchronousProxy) -> Result<String, Error> {
    let buffer = syncio::directory_read_file(pkg, "/meta", zx::Time::INFINITE)?;
    let hash = std::str::from_utf8(&buffer)?;
    Ok(hash.to_string())
}

pub fn create_filesystem_from_spec<'a>(
    task: &CurrentTask,
    pkg: &fio::DirectorySynchronousProxy,
    spec: &'a str,
) -> Result<(&'a [u8], WhatToMount), Error> {
    use WhatToMount::*;
    let mut iter = spec.splitn(3, ':');
    let mount_point =
        iter.next().ok_or_else(|| anyhow!("mount point is missing from {:?}", spec))?;
    let fs_type = iter.next().ok_or_else(|| anyhow!("fs type is missing from {:?}", spec))?;
    let fs_src = iter.next().unwrap_or(".");

    // Default rights for remotefs.
    let rights = fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_EXECUTABLE;

    // The filesystem types handled in this match are the ones that can only be specified in a
    // manifest file, for whatever reason. Anything else is passed to create_filesystem, which is
    // common code that also handles the mount() system call.
    let fs = match fs_type {
        "bind" => Bind(task.lookup_path_from_root(fs_src.as_bytes())?),
        "remotefs" => Fs(create_remotefs_filesystem(task.kernel(), pkg, rights, fs_src)?),
        "ext4" => {
            let vmo =
                syncio::directory_open_vmo(pkg, fs_src, fio::VmoFlags::READ, zx::Time::INFINITE)
                    .context("failed to open EXT4 image file")?;
            Fs(ExtFilesystem::new_fs(task.kernel(), vmo)?)
        }
        _ => create_filesystem(task, fs_src.as_bytes(), fs_type.as_bytes(), b"")?,
    };
    Ok((mount_point.as_bytes(), fs))
}

/// Block the execution of `current_task` as long as the task is stopped and not terminated.
pub fn block_while_stopped(current_task: &CurrentTask) {
    // Early exit test to avoid creating a port when we don't need to sleep. Testing in the loop
    // after adding the waiter to the wait queue is still important to deal with race conditions
    // where the condition becomes true between checking it and starting the wait.
    // TODO(tbodt): Find a less hacky way to do this. There might be some way to create one port
    // per task and use it every time the current task needs to sleep.
    if current_task.read().exit_status.is_some() {
        return;
    }
    if !current_task.thread_group.read().stopped {
        return;
    }

    let waiter = Waiter::new_ignoring_signals();
    loop {
        current_task.thread_group.write().stopped_waiters.wait_async(&waiter);
        if current_task.read().exit_status.is_some() {
            return;
        }
        if !current_task.thread_group.read().stopped {
            return;
        }
        // Result is not needed, as this is not in a syscall.
        let _: Result<(), Errno> = waiter.wait(current_task);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::signals::*;
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

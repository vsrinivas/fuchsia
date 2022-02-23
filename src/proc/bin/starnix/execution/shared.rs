// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{anyhow, Error};
use fidl::endpoints::ServerEnd;
use fidl_fuchsia_io as fio;
use fidl_fuchsia_process as fprocess;
use fidl_fuchsia_starnix_developer as fstardev;
use fuchsia_runtime::{HandleInfo, HandleType};
use fuchsia_zircon::{self as zx, sys::ZX_PROCESS_DEBUG_ADDR_BREAK_ON_SET, HandleBased};
use process_builder::elf_parse;
use std::convert::TryFrom;
use std::sync::Arc;
use zerocopy::AsBytes;

use crate::errno;
use crate::from_status_like_fdio;
use crate::fs::ext4::ExtFilesystem;
use crate::fs::fuchsia::{create_file_from_handle, RemoteFs, SyslogFile};
use crate::fs::*;
use crate::mm::{DesiredAddress, MappingOptions, PAGE_SIZE};
use crate::task::*;
use crate::types::*;
use crate::vmex_resource::VMEX_RESOURCE;

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
    let mut debug_address = elf_parse::Elf64Dyn::default();
    current_task.mm.read_object(UserRef::new(dt_debug_address), &mut debug_address)?;
    if debug_address.value == 0 {
        // The DT_DEBUG entry is present, but has not yet been set by the linker, check next time.
        return Ok(());
    }

    let existing_debug_addr = current_task
        .thread_group
        .process
        .get_debug_addr()
        .map_err(|err| from_status_like_fdio!(err))?;

    // If existing_debug_addr != ZX_PROCESS_DEBUG_ADDR_BREAK_ON_SET then there is no reason to
    // insert the interrupt.
    if existing_debug_addr != ZX_PROCESS_DEBUG_ADDR_BREAK_ON_SET as u64 {
        // Still set the debug address, and clear the debug address from `current_task` to avoid
        // entering this function again.
        match current_task.thread_group.process.set_debug_addr(&debug_address.value) {
            // TODO(tbodt): When a process execs, it will want to set its debug address again, and
            // zircon only allows the debug address to be set once. We should figure out how to get
            // the debugger to handle exec, or maybe kill the process and start a new one on exec.
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
        .set_debug_addr(&debug_address.value)
        .map_err(|err| from_status_like_fdio!(err))?;
    current_task.dt_debug_address = None;

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
    numbered_handles: Option<Vec<fprocess::HandleInfo>>,
    files: &Arc<FdTable>,
    kernel: &Kernel,
) -> Result<StartupHandles, Error> {
    let mut shell_controller = None;
    if let Some(numbered_handles) = numbered_handles {
        for numbered_handle in numbered_handles {
            let info = HandleInfo::try_from(numbered_handle.id)?;
            if info.handle_type() == HandleType::FileDescriptor {
                files.insert(
                    FdNumber::from_raw(info.arg().into()),
                    create_file_from_handle(kernel, numbered_handle.handle)?,
                );
            } else if info.handle_type() == HandleType::User0 {
                shell_controller = Some(ServerEnd::<fstardev::ShellControllerMarker>::from(
                    numbered_handle.handle,
                ));
            }
        }
    } else {
        // If no numbered handles are provided default 0, 1, and 2 to a syslog file.
        let stdio = SyslogFile::new(&kernel);
        files.insert(FdNumber::from_raw(0), stdio.clone());
        files.insert(FdNumber::from_raw(1), stdio.clone());
        files.insert(FdNumber::from_raw(2), stdio);
    }
    Ok(StartupHandles { shell_controller })
}

/// Creates a `HandleInfo` from the provided socket and file descriptor.
///
/// The file descriptor is encoded as a `PA_HND(PA_FD, <file_descriptor>)` before being stored in
/// the `HandleInfo`.
///
/// Returns an error if `socket` is `None`.
pub fn handle_info_from_socket(
    socket: Option<fidl::Socket>,
    file_descriptor: u16,
) -> Result<fprocess::HandleInfo, Error> {
    if let Some(socket) = socket {
        let info = HandleInfo::new(HandleType::FileDescriptor, file_descriptor);
        Ok(fprocess::HandleInfo { handle: socket.into_handle(), id: info.as_raw() })
    } else {
        Err(anyhow!("Failed to create HandleInfo for {}", file_descriptor))
    }
}

pub fn create_filesystem_from_spec<'a>(
    kernel: &Arc<Kernel>,
    task: Option<&CurrentTask>,
    pkg: &fio::DirectorySynchronousProxy,
    spec: &'a str,
) -> Result<(&'a [u8], WhatToMount), Error> {
    use WhatToMount::*;
    let mut iter = spec.splitn(3, ':');
    let mount_point =
        iter.next().ok_or_else(|| anyhow!("mount point is missing from {:?}", spec))?;
    let fs_type = iter.next().ok_or_else(|| anyhow!("fs type is missing from {:?}", spec))?;
    let fs_src = iter.next().unwrap_or("");

    // The filesystem types handled in this match are the ones that can only be specified in a
    // manifest file, for whatever reason. Anything else is passed to create_filesystem, which is
    // common code that also handles the mount() system call.
    let fs = match fs_type {
        "bind" => {
            let task = task.ok_or(errno!(ENOENT))?;
            Dir(task.lookup_path_from_root(fs_src.as_bytes())?.entry)
        }
        "remotefs" => {
            let rights = fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_EXECUTABLE;
            let root = syncio::directory_open_directory_async(&pkg, &fs_src, rights)
                .map_err(|e| anyhow!("Failed to open root: {}", e))?;
            Fs(RemoteFs::new(root.into_channel(), rights)?)
        }
        "ext4" => {
            let vmo =
                syncio::directory_open_vmo(&pkg, &fs_src, fio::VMO_FLAG_READ, zx::Time::INFINITE)?;
            Fs(ExtFilesystem::new(vmo)?)
        }
        _ => create_filesystem(&kernel, fs_src.as_bytes(), fs_type.as_bytes(), b"")?,
    };
    Ok((mount_point.as_bytes(), fs))
}

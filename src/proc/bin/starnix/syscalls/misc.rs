// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon as zx;

use crate::mm::{MemoryAccessor, MemoryAccessorExt};
use crate::syscalls::decls::SyscallDecl;
use crate::syscalls::*;

pub fn sys_uname(current_task: &CurrentTask, name: UserRef<utsname_t>) -> Result<(), Errno> {
    fn init_array(fixed: &mut [u8; 65], init: &'static str) {
        let init_bytes = init.as_bytes();
        let len = init.len();
        fixed[..len].copy_from_slice(init_bytes)
    }

    let mut result = utsname_t {
        sysname: [0; 65],
        nodename: [0; 65],
        release: [0; 65],
        version: [0; 65],
        machine: [0; 65],
    };
    init_array(&mut result.sysname, "Linux");
    init_array(&mut result.nodename, "local");
    init_array(&mut result.release, "5.7.17-starnix");
    init_array(&mut result.version, "starnix");
    init_array(&mut result.machine, "x86_64");
    current_task.mm.write_object(name, &result)?;
    Ok(())
}

pub fn sys_getrandom(
    current_task: &CurrentTask,
    buf_addr: UserAddress,
    size: usize,
    _flags: i32,
) -> Result<usize, Errno> {
    let mut buf = vec![0; size];
    zx::cprng_draw(&mut buf);
    current_task.mm.write_memory(buf_addr, &buf[0..size])?;
    Ok(size)
}

pub fn sys_reboot(
    current_task: &CurrentTask,
    magic: u32,
    magic2: u32,
    cmd: u32,
    _arg: UserAddress,
) -> Result<(), Errno> {
    if magic != LINUX_REBOOT_MAGIC1
        || (magic2 != LINUX_REBOOT_MAGIC2
            && magic2 != LINUX_REBOOT_MAGIC2A
            && magic2 != LINUX_REBOOT_MAGIC2B
            && magic2 != LINUX_REBOOT_MAGIC2C)
    {
        return error!(EINVAL);
    }
    if !current_task.creds().has_capability(CAP_SYS_BOOT) {
        return error!(EPERM);
    }
    // TODO(tbodt): only shut down the current Kernel rather than panicking the entire process
    panic!("starnix reboot({:#x})", cmd);
}

pub fn sys_sched_yield(_current_task: &CurrentTask) -> Result<(), Errno> {
    // SAFETY: This is unsafe because it is a syscall. zx_thread_legacy_yield is always safe.
    let status = unsafe { zx::sys::zx_thread_legacy_yield(0) };
    zx::Status::ok(status).map_err(|status| from_status_like_fdio!(status))
}

pub fn sys_unknown(
    current_task: &CurrentTask,
    syscall_number: u64,
) -> Result<SyscallResult, Errno> {
    not_implemented!(
        current_task,
        "unknown syscall {:?}",
        SyscallDecl::from_number(syscall_number)
    );
    // TODO: We should send SIGSYS once we have signals.
    error!(ENOSYS)
}

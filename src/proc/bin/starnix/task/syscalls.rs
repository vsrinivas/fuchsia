// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use log::info;
use std::ffi::CString;

use crate::mm::*;
use crate::not_implemented;
use crate::runner::*;
use crate::syscalls::*;
use crate::types::*;

pub fn sys_clone(
    ctx: &SyscallContext<'_>,
    flags: u64,
    user_stack: UserAddress,
    user_parent_tid: UserRef<pid_t>,
    user_child_tid: UserRef<pid_t>,
    user_tls: UserAddress,
) -> Result<SyscallResult, Errno> {
    let task_owner =
        ctx.task.clone_task(flags, user_stack, user_parent_tid, user_child_tid, user_tls)?;
    let tid = task_owner.task.id;

    let mut registers = ctx.registers;
    registers.rax = 0;
    spawn_task(task_owner, registers);

    Ok(tid.into())
}

pub fn sys_getpid(ctx: &SyscallContext<'_>) -> Result<SyscallResult, Errno> {
    let _pid = ctx.task.get_pid();
    // This is set to 1 because Bionic skips referencing /dev if getpid() == 1, under the
    // assumption that anything running after init will have access to /dev.
    Ok(1.into())
}

pub fn sys_gettid(ctx: &SyscallContext<'_>) -> Result<SyscallResult, Errno> {
    Ok(ctx.task.get_tid().into())
}

pub fn sys_getppid(ctx: &SyscallContext<'_>) -> Result<SyscallResult, Errno> {
    Ok(ctx.task.parent.into())
}

pub fn sys_getpgrp(ctx: &SyscallContext<'_>) -> Result<SyscallResult, Errno> {
    Ok(ctx.task.get_pgrp().into())
}

pub fn sys_getpgid(ctx: &SyscallContext<'_>, pid: pid_t) -> Result<SyscallResult, Errno> {
    if pid == 0 {
        return Ok(ctx.task.get_pgrp().into());
    }
    Ok(ctx.task.get_task(pid).ok_or(ESRCH)?.get_pgrp().into())
}

pub fn sys_getuid(ctx: &SyscallContext<'_>) -> Result<SyscallResult, Errno> {
    Ok(ctx.task.creds.uid.into())
}

pub fn sys_getgid(ctx: &SyscallContext<'_>) -> Result<SyscallResult, Errno> {
    Ok(ctx.task.creds.gid.into())
}

pub fn sys_geteuid(ctx: &SyscallContext<'_>) -> Result<SyscallResult, Errno> {
    Ok(ctx.task.creds.euid.into())
}

pub fn sys_getegid(ctx: &SyscallContext<'_>) -> Result<SyscallResult, Errno> {
    Ok(ctx.task.creds.egid.into())
}

pub fn sys_exit(ctx: &SyscallContext<'_>, error_code: i32) -> Result<SyscallResult, Errno> {
    info!(target: "exit", "exit: tid={} error_code={}", ctx.task.get_tid(), error_code);
    Ok(SyscallResult::Exit(error_code))
}

pub fn sys_exit_group(ctx: &SyscallContext<'_>, error_code: i32) -> Result<SyscallResult, Errno> {
    info!(target: "exit", "exit_group: pid={} error_code={}", ctx.task.get_pid(), error_code);
    // TODO: Once we have more than one thread in a thread group, we'll need to exit them as well.
    Ok(SyscallResult::Exit(error_code))
}

pub fn sys_sched_getscheduler(
    _ctx: &SyscallContext<'_>,
    _pid: i32,
) -> Result<SyscallResult, Errno> {
    Ok(SCHED_NORMAL.into())
}

pub fn sys_sched_getaffinity(
    ctx: &SyscallContext<'_>,
    _pid: pid_t,
    _cpusetsize: usize,
    user_mask: UserAddress,
) -> Result<SyscallResult, Errno> {
    let result = vec![0xFFu8; _cpusetsize];
    ctx.task.mm.write_memory(user_mask, &result)?;
    Ok(SUCCESS)
}

pub fn sys_sched_setaffinity(
    ctx: &SyscallContext<'_>,
    _pid: pid_t,
    _cpusetsize: usize,
    user_mask: UserAddress,
) -> Result<SyscallResult, Errno> {
    let mut mask = vec![0x0u8; _cpusetsize];
    ctx.task.mm.read_memory(user_mask, &mut mask)?;
    // Currently, we ignore the mask and act as if the system reset the mask
    // immediately to allowing all CPUs.
    Ok(SUCCESS)
}

pub fn sys_prctl(
    ctx: &SyscallContext<'_>,
    option: u32,
    arg2: u64,
    arg3: u64,
    arg4: u64,
    arg5: u64,
) -> Result<SyscallResult, Errno> {
    match option {
        PR_SET_VMA => {
            if arg2 != PR_SET_VMA_ANON_NAME as u64 {
                not_implemented!("prctl: PR_SET_VMA: Unknown arg2: 0x{:x}", arg2);
                return Err(ENOSYS);
            }
            let addr = UserAddress::from(arg3);
            let length = arg4 as usize;
            let name = UserCString::new(UserAddress::from(arg5));
            let mut buf = [0u8; PATH_MAX as usize]; // TODO: How large can these names be?
            let name = ctx.task.mm.read_c_string(name, &mut buf)?;
            let name = CString::new(name).map_err(|_| EINVAL)?;
            ctx.task.mm.set_mapping_name(addr, length, name)?;
            Ok(SUCCESS)
        }
        PR_SET_DUMPABLE => {
            let mut dumpable = ctx.task.mm.dumpable.lock();
            *dumpable = if arg2 == 1 { DumpPolicy::USER } else { DumpPolicy::DISABLE };
            Ok(SUCCESS)
        }
        PR_GET_DUMPABLE => {
            let dumpable = ctx.task.mm.dumpable.lock();
            Ok(match *dumpable {
                DumpPolicy::DISABLE => 0,
                DumpPolicy::USER => 1,
            }
            .into())
        }
        _ => {
            not_implemented!("prctl: Unknown option: 0x{:x}", option);
            Err(ENOSYS)
        }
    }
}

pub fn sys_arch_prctl(
    ctx: &mut SyscallContext<'_>,
    code: u32,
    addr: UserAddress,
) -> Result<SyscallResult, Errno> {
    match code {
        ARCH_SET_FS => {
            ctx.registers.fs_base = addr.ptr() as u64;
            Ok(SUCCESS)
        }
        _ => {
            not_implemented!("arch_prctl: Unknown code: code=0x{:x} addr={}", code, addr);
            Err(ENOSYS)
        }
    }
}

pub fn sys_set_tid_address(
    ctx: &SyscallContext<'_>,
    user_tid: UserRef<pid_t>,
) -> Result<SyscallResult, Errno> {
    *ctx.task.clear_child_tid.lock() = user_tid;
    Ok(ctx.task.get_tid().into())
}

#[cfg(test)]
mod tests {
    use super::*;
    use fuchsia_async as fasync;

    use crate::mm::syscalls::sys_munmap;
    use crate::testing::*;

    #[fasync::run_singlethreaded(test)]
    async fn test_prctl_set_vma_anon_name() {
        let (_kernel, task_owner) = create_kernel_and_task();
        let ctx = SyscallContext::new(&task_owner.task);

        let mapped_address = map_memory(&ctx, UserAddress::default(), *PAGE_SIZE);
        let name_addr = mapped_address + 128u64;
        let name = "test-name\0";
        ctx.task.mm.write_memory(name_addr, name.as_bytes()).expect("failed to write name");
        sys_prctl(
            &ctx,
            PR_SET_VMA,
            PR_SET_VMA_ANON_NAME as u64,
            mapped_address.ptr() as u64,
            32,
            name_addr.ptr() as u64,
        )
        .expect("failed to set name");
        assert_eq!(
            CString::new("test-name").unwrap(),
            ctx.task.mm.get_mapping_name(mapped_address + 24u64).expect("failed to get address")
        );

        sys_munmap(&ctx, mapped_address, *PAGE_SIZE as usize).expect("failed to unmap memory");
        assert_eq!(Err(EFAULT), ctx.task.mm.get_mapping_name(mapped_address + 24u64));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_prctl_get_set_dumpable() {
        let (_kernel, task_owner) = create_kernel_and_task();
        let ctx = SyscallContext::new(&task_owner.task);

        assert_eq!(
            SyscallResult::Success(0),
            sys_prctl(&ctx, PR_GET_DUMPABLE, 0, 0, 0, 0).expect("failed to get dumpable")
        );

        sys_prctl(&ctx, PR_SET_DUMPABLE, 1, 0, 0, 0).expect("failed to set dumpable");
        assert_eq!(
            SyscallResult::Success(1),
            sys_prctl(&ctx, PR_GET_DUMPABLE, 0, 0, 0, 0).expect("failed to get dumpable")
        );

        // SUID_DUMP_ROOT not supported.
        sys_prctl(&ctx, PR_SET_DUMPABLE, 2, 0, 0, 0).expect("failed to set dumpable");
        assert_eq!(
            SyscallResult::Success(0),
            sys_prctl(&ctx, PR_GET_DUMPABLE, 0, 0, 0, 0).expect("failed to get dumpable")
        );
    }
}

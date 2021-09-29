// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon as zx;
use log::info;
use std::ffi::CString;
use zerocopy::AsBytes;

use crate::errno;
use crate::error;
use crate::mm::*;
use crate::not_implemented;
use crate::runner::*;
use crate::strace;
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
    let task_owner = ctx.task.clone_task(flags, user_parent_tid, user_child_tid)?;
    let tid = task_owner.task.id;

    let mut registers = ctx.registers;
    registers.rax = 0;
    if !user_stack.is_null() {
        registers.rsp = user_stack.ptr() as u64;
    }

    if flags & (CLONE_SETTLS as u64) != 0 {
        registers.fs_base = user_tls.ptr() as u64;
    }

    if flags & (CLONE_THREAD as u64) != 0 {
        spawn_task(task_owner, registers, |_| {});
    } else {
        spawn_task(task_owner, registers, |_| {});
    }

    Ok(tid.into())
}

fn read_c_string_vector(
    mm: &MemoryManager,
    user_vector: UserRef<UserCString>,
    buf: &mut [u8],
) -> Result<Vec<CString>, Errno> {
    let mut user_current = user_vector;
    let mut vector: Vec<CString> = vec![];
    loop {
        let mut user_string = UserCString::default();
        mm.read_object(user_current, &mut user_string)?;
        if user_string.is_null() {
            break;
        }
        let string = mm.read_c_string(user_string, buf)?;
        vector.push(CString::new(string).map_err(|_| errno!(EINVAL))?);
        user_current = user_current.next();
    }
    Ok(vector)
}

pub fn sys_execve(
    ctx: &mut SyscallContext<'_>,
    user_path: UserCString,
    user_argv: UserRef<UserCString>,
    user_environ: UserRef<UserCString>,
) -> Result<SyscallResult, Errno> {
    let mut buf = [0u8; PATH_MAX as usize];
    let path = CString::new(ctx.task.mm.read_c_string(user_path, &mut buf)?)
        .map_err(|_| errno!(EINVAL))?;
    // TODO: What is the maximum size for an argument?
    let argv = read_c_string_vector(&ctx.task.mm, user_argv, &mut buf)?;
    let environ = read_c_string_vector(&ctx.task.mm, user_environ, &mut buf)?;
    strace!(ctx.task, "execve({:?}, argv={:?}, environ={:?})", path, argv, environ);
    let start_info = ctx.task.exec(&path, &argv, &environ)?;
    ctx.registers = start_info.to_registers();
    Ok(SUCCESS)
}

pub fn sys_getpid(ctx: &SyscallContext<'_>) -> Result<SyscallResult, Errno> {
    Ok(ctx.task.get_pid().into())
}

pub fn sys_getsid(ctx: &SyscallContext<'_>, pid: pid_t) -> Result<SyscallResult, Errno> {
    if pid == 0 {
        return Ok(ctx.task.get_sid().into());
    }
    Ok(ctx.task.get_task(pid).ok_or(errno!(ESRCH))?.get_sid().into())
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
    Ok(ctx.task.get_task(pid).ok_or(errno!(ESRCH))?.get_pgrp().into())
}

pub fn sys_getuid(ctx: &SyscallContext<'_>) -> Result<SyscallResult, Errno> {
    Ok(ctx.task.creds.read().uid.into())
}

pub fn sys_getgid(ctx: &SyscallContext<'_>) -> Result<SyscallResult, Errno> {
    Ok(ctx.task.creds.read().gid.into())
}

pub fn sys_geteuid(ctx: &SyscallContext<'_>) -> Result<SyscallResult, Errno> {
    Ok(ctx.task.creds.read().euid.into())
}

pub fn sys_getegid(ctx: &SyscallContext<'_>) -> Result<SyscallResult, Errno> {
    Ok(ctx.task.creds.read().egid.into())
}

pub fn sys_getresuid(
    ctx: &SyscallContext<'_>,
    ruid_addr: UserRef<uid_t>,
    euid_addr: UserRef<uid_t>,
    suid_addr: UserRef<uid_t>,
) -> Result<SyscallResult, Errno> {
    let creds = ctx.task.creds.read();
    ctx.task.mm.write_object(ruid_addr, &creds.uid)?;
    ctx.task.mm.write_object(euid_addr, &creds.euid)?;
    ctx.task.mm.write_object(suid_addr, &creds.saved_uid)?;
    Ok(SUCCESS)
}

pub fn sys_getresgid(
    ctx: &SyscallContext<'_>,
    ruid_addr: UserRef<uid_t>,
    euid_addr: UserRef<uid_t>,
    suid_addr: UserRef<uid_t>,
) -> Result<SyscallResult, Errno> {
    let creds = ctx.task.creds.read();
    ctx.task.mm.write_object(ruid_addr, &creds.uid)?;
    ctx.task.mm.write_object(euid_addr, &creds.euid)?;
    ctx.task.mm.write_object(suid_addr, &creds.saved_uid)?;
    Ok(SUCCESS)
}

pub fn sys_exit(ctx: &SyscallContext<'_>, exit_code: i32) -> Result<SyscallResult, Errno> {
    info!(target: "exit", "exit: tid={} exit_code={}", ctx.task.id, exit_code);
    *ctx.task.exit_code.lock() = Some(exit_code);
    Ok(SyscallResult::Exit(exit_code))
}

pub fn sys_exit_group(ctx: &SyscallContext<'_>, exit_code: i32) -> Result<SyscallResult, Errno> {
    info!(target: "exit", "exit_group: pid={} exit_code={}", ctx.task.thread_group.leader, exit_code);
    *ctx.task.exit_code.lock() = Some(exit_code);
    ctx.task.thread_group.exit();
    Ok(SyscallResult::Exit(exit_code))
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

pub fn sys_getitimer(
    ctx: &SyscallContext<'_>,
    which: u32,
    user_curr_value: UserRef<itimerval>,
) -> Result<SyscallResult, Errno> {
    let itimers = ctx.task.thread_group.itimers.read();
    let timer = itimers.get(which as usize).ok_or(errno!(EINVAL))?;
    ctx.task.mm.write_object(user_curr_value, timer)?;
    Ok(SUCCESS)
}

pub fn sys_setitimer(
    ctx: &SyscallContext<'_>,
    which: u32,
    user_new_value: UserRef<itimerval>,
    user_old_value: UserRef<itimerval>,
) -> Result<SyscallResult, Errno> {
    let mut new_value = itimerval::default();
    ctx.task.mm.read_object(user_new_value, &mut new_value)?;

    let mut itimers = ctx.task.thread_group.itimers.write();
    let timer = itimers.get_mut(which as usize).ok_or(errno!(EINVAL))?;
    let old_value = *timer;
    *timer = new_value;

    if !user_old_value.is_null() {
        ctx.task.mm.write_object(user_old_value, &old_value)?;
    }

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
                return error!(ENOSYS);
            }
            let addr = UserAddress::from(arg3);
            let length = arg4 as usize;
            let name = UserCString::new(UserAddress::from(arg5));
            let mut buf = [0u8; PATH_MAX as usize]; // TODO: How large can these names be?
            let name = ctx.task.mm.read_c_string(name, &mut buf)?;
            let name = CString::new(name).map_err(|_| errno!(EINVAL))?;
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
            error!(ENOSYS)
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
            error!(ENOSYS)
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

pub fn sys_getrusage(
    ctx: &SyscallContext<'_>,
    who: i32,
    user_usage: UserRef<rusage>,
) -> Result<SyscallResult, Errno> {
    const RUSAGE_SELF: i32 = crate::types::uapi::RUSAGE_SELF as i32;
    const RUSAGE_THREAD: i32 = crate::types::uapi::RUSAGE_THREAD as i32;
    // TODO(fxb/76811): Implement proper rusage.
    match who {
        RUSAGE_CHILDREN => (),
        RUSAGE_SELF => (),
        RUSAGE_THREAD => (),
        _ => return error!(EINVAL),
    };

    if !user_usage.is_null() {
        let usage = rusage::default();
        ctx.task.mm.write_object(user_usage, &usage)?;
    }

    Ok(SUCCESS)
}

pub fn sys_futex(
    ctx: &SyscallContext<'_>,
    addr: UserAddress,
    op: u32,
    value: u32,
    _utime: UserRef<timespec>,
    _addr2: UserAddress,
    value3: u32,
) -> Result<SyscallResult, Errno> {
    // TODO: Distinguish between public and private futexes.
    let _is_private = op & FUTEX_PRIVATE_FLAG != 0;

    let is_realtime = op & FUTEX_CLOCK_REALTIME != 0;
    if is_realtime {
        not_implemented!("futex: Realtime futex are not implemented.");
        return error!(ENOSYS);
    }

    let cmd = op & (FUTEX_CMD_MASK as u32);
    match cmd {
        FUTEX_WAIT => {
            let deadline = zx::Time::INFINITE;
            ctx.task.mm.futex.wait(&ctx.task, addr, value, FUTEX_BITSET_MATCH_ANY, deadline)?;
        }
        FUTEX_WAKE => {
            ctx.task.mm.futex.wake(addr, value as usize, FUTEX_BITSET_MATCH_ANY);
        }
        FUTEX_WAIT_BITSET => {
            if value3 == 0 {
                return error!(EINVAL);
            }
            let deadline = zx::Time::INFINITE;
            ctx.task.mm.futex.wait(&ctx.task, addr, value, value3, deadline)?;
        }
        FUTEX_WAKE_BITSET => {
            if value3 == 0 {
                return error!(EINVAL);
            }
            ctx.task.mm.futex.wake(addr, value as usize, value3);
        }
        _ => {
            not_implemented!("futex: command 0x{:x} not implemented.", cmd);
            return error!(ENOSYS);
        }
    }
    Ok(SUCCESS)
}

pub fn sys_capget(
    _ctx: &SyscallContext<'_>,
    _user_header: UserRef<__user_cap_header_struct>,
    _user_data: UserRef<__user_cap_data_struct>,
) -> Result<SyscallResult, Errno> {
    not_implemented!("Stubbed capget has no effect.");
    Ok(SUCCESS)
}

pub fn sys_setgroups(
    ctx: &SyscallContext<'_>,
    size: usize,
    groups_addr: UserAddress,
) -> Result<SyscallResult, Errno> {
    if size > NGROUPS_MAX as usize {
        return error!(EINVAL);
    }
    let mut groups: Vec<gid_t> = vec![0; size];
    ctx.task.mm.read_memory(groups_addr, groups.as_mut_slice().as_bytes_mut())?;
    let mut creds = ctx.task.creds.write();
    if !creds.is_superuser() {
        return error!(EPERM);
    }
    creds.groups = groups;
    Ok(SUCCESS)
}

pub fn sys_getgroups(
    ctx: &SyscallContext<'_>,
    size: usize,
    groups_addr: UserAddress,
) -> Result<SyscallResult, Errno> {
    let creds = ctx.task.creds.read();
    let groups = &creds.groups;
    if size != 0 {
        if size < groups.len() {
            return error!(EINVAL);
        }
        ctx.task.mm.write_memory(groups_addr, groups.as_slice().as_bytes())?;
    }
    Ok(groups.len().into())
}

#[cfg(test)]
mod tests {
    use std::u64;

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
        assert_eq!(error!(EFAULT), ctx.task.mm.get_mapping_name(mapped_address + 24u64));
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

    #[fasync::run_singlethreaded(test)]
    async fn test_sys_getsid() {
        let (kernel, task_owner) = create_kernel_and_task();
        let ctx = SyscallContext::new(&task_owner.task);

        assert_eq!(
            SyscallResult::Success(task_owner.task.get_tid() as u64),
            sys_getsid(&ctx, 0).expect("failed to get sid")
        );

        let second_task_owner = create_task(&kernel, "second task");

        assert_eq!(
            SyscallResult::Success(second_task_owner.task.get_tid() as u64),
            sys_getsid(&ctx, second_task_owner.task.get_tid().into()).expect("failed to get sid")
        );
    }
}

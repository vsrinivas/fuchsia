// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_io as fio;
use fuchsia_runtime::utc_time;
use fuchsia_zircon::{self as zx, sys::zx_thread_state_general_regs_t, AsHandleRef};
use log::{info, warn};
use std::convert::TryInto;
use std::ffi::{CStr, CString};
use std::sync::Arc;
use zerocopy::AsBytes;

use crate::fs::*;
use crate::logging::*;
use crate::mm::*;
use crate::not_implemented;
use crate::strace;
use crate::task::*;
use crate::uapi::*;

pub struct SyscallContext<'a> {
    pub task: &'a Arc<Task>,

    /// A copy of the registers associated with the Zircon thread. Up-to-date values can be read
    /// from `self.handle.read_state_general_regs()`. To write these values back to the thread, call
    /// `self.handle.write_state_general_regs(self.registers)`.
    pub registers: zx_thread_state_general_regs_t,
}

impl SyscallContext<'_> {
    #[cfg(test)]
    fn new<'a>(task: &'a Arc<Task>) -> SyscallContext<'a> {
        SyscallContext { task, registers: zx_thread_state_general_regs_t::default() }
    }
}

pub fn sys_write(
    ctx: &SyscallContext<'_>,
    fd: FdNumber,
    buffer: UserAddress,
    count: usize,
) -> Result<SyscallResult, Errno> {
    let file = ctx.task.files.get(fd)?;
    Ok(file.write(&ctx.task, &[iovec_t { iov_base: buffer, iov_len: count }])?.into())
}

pub fn sys_fcntl(
    ctx: &SyscallContext<'_>,
    fd: FdNumber,
    cmd: u32,
    arg: u64,
) -> Result<SyscallResult, Errno> {
    match cmd {
        F_GETOWN => {
            let file = ctx.task.files.get(fd)?;
            Ok(file.get_async_owner().into())
        }
        F_SETOWN => {
            if arg > std::i32::MAX as u64 {
                // Negative values are process groups.
                not_implemented!("fcntl(F_SETOWN) does not support process groups");
                return Err(EINVAL);
            }
            let file = ctx.task.files.get(fd)?;
            let task = ctx.task.get_task(arg.try_into().map_err(|_| EINVAL)?);
            file.set_async_owner(task.map_or(0, |task| task.id));
            Ok(SUCCESS)
        }
        F_GETFD => Ok(ctx.task.files.get_flags(fd)?.bits().into()),
        F_SETFD => {
            ctx.task.files.set_flags(fd, FdFlags::from_bits_truncate(arg as u32))?;
            Ok(SUCCESS)
        }
        _ => {
            not_implemented!("fcntl command {} not implemented", cmd);
            Err(ENOSYS)
        }
    }
}

pub fn sys_fstat(
    ctx: &SyscallContext<'_>,
    fd: FdNumber,
    buffer: UserRef<stat_t>,
) -> Result<SyscallResult, Errno> {
    let file = ctx.task.files.get(fd)?;
    let result = file.fstat(&ctx.task)?;
    ctx.task.mm.write_object(buffer, &result)?;
    return Ok(SUCCESS);
}

fn mmap_prot_to_vm_opt(prot: u32) -> zx::VmarFlags {
    let mut flags = zx::VmarFlags::empty();
    if prot & PROT_READ != 0 {
        flags |= zx::VmarFlags::PERM_READ;
    }
    if prot & PROT_WRITE != 0 {
        flags |= zx::VmarFlags::PERM_WRITE;
    }
    if prot & PROT_EXEC != 0 {
        flags |= zx::VmarFlags::PERM_EXECUTE;
    }
    flags
}

pub fn sys_mmap(
    ctx: &SyscallContext<'_>,
    addr: UserAddress,
    length: usize,
    prot: u32,
    flags: u32,
    fd: FdNumber,
    offset: usize,
) -> Result<SyscallResult, Errno> {
    // These are the flags that are currently supported.
    if prot & !(PROT_READ | PROT_WRITE | PROT_EXEC) != 0 {
        return Err(EINVAL);
    }
    if flags & !(MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED | MAP_NORESERVE) != 0 {
        return Err(EINVAL);
    }

    if flags & (MAP_PRIVATE | MAP_SHARED) == 0
        || flags & (MAP_PRIVATE | MAP_SHARED) == MAP_PRIVATE | MAP_SHARED
    {
        return Err(EINVAL);
    }
    if length == 0 {
        return Err(EINVAL);
    }
    if offset as u64 % *PAGE_SIZE != 0 {
        return Err(EINVAL);
    }

    // TODO(tbodt): should we consider MAP_NORESERVE?

    if flags & MAP_ANONYMOUS != 0 && fd.raw() != -1 {
        return Err(EINVAL);
    }

    let mut zx_flags = mmap_prot_to_vm_opt(prot) | zx::VmarFlags::ALLOW_FAULTS;
    if addr.ptr() != 0 {
        // TODO(tbodt): if no MAP_FIXED, retry on EINVAL
        zx_flags |= zx::VmarFlags::SPECIFIC;
    }
    if flags & MAP_FIXED != 0 {
        // SAFETY: We are operating on another process, so it's safe to use SPECIFIC_OVERWRITE
        zx_flags |= unsafe {
            zx::VmarFlags::from_bits_unchecked(zx::VmarFlagsExtended::SPECIFIC_OVERWRITE.bits())
        };
    }

    let vmo = if flags & MAP_ANONYMOUS != 0 {
        let vmo = zx::Vmo::create(length as u64).map_err(|s| match s {
            zx::Status::NO_MEMORY => ENOMEM,
            _ => impossible_error(s),
        })?;
        vmo.set_name(CStr::from_bytes_with_nul(b"starnix-anon\0").unwrap())
            .map_err(impossible_error)?;
        vmo
    } else {
        // TODO(tbodt): maximize protection flags so that mprotect works
        let file = ctx.task.files.get(fd)?;
        let zx_prot = mmap_prot_to_vm_opt(prot);
        if flags & MAP_PRIVATE != 0 {
            // TODO(tbodt): Use VMO_FLAG_PRIVATE to have the filesystem server do the clone for us.
            let vmo = file.get_vmo(&ctx.task, zx_prot - zx::VmarFlags::PERM_WRITE, flags)?;
            let mut clone_flags = zx::VmoChildOptions::COPY_ON_WRITE;
            if !zx_prot.contains(zx::VmarFlags::PERM_WRITE) {
                clone_flags |= zx::VmoChildOptions::NO_WRITE;
            }
            vmo.create_child(clone_flags, 0, vmo.get_size().map_err(impossible_error)?)
                .map_err(impossible_error)?
        } else {
            file.get_vmo(&ctx.task, zx_prot, flags)?
        }
    };
    let vmo_offset = if flags & MAP_ANONYMOUS != 0 { 0 } else { offset };

    let addr = ctx.task.mm.map(addr, vmo, vmo_offset as u64, length, zx_flags)?;
    Ok(addr.into())
}

pub fn sys_mprotect(
    ctx: &SyscallContext<'_>,
    addr: UserAddress,
    length: usize,
    prot: u32,
) -> Result<SyscallResult, Errno> {
    // SAFETY: This is safe because the vmar belongs to a different process.
    unsafe { ctx.task.mm.root_vmar.protect(addr.ptr(), length, mmap_prot_to_vm_opt(prot)) }
        .map_err(|s| match s {
            zx::Status::INVALID_ARGS => EINVAL,
            // TODO: This should still succeed and change protection on whatever is mapped.
            zx::Status::NOT_FOUND => EINVAL,
            zx::Status::ACCESS_DENIED => EACCES,
            _ => EINVAL, // impossible!
        })?;
    Ok(SUCCESS)
}

pub fn sys_munmap(
    ctx: &SyscallContext<'_>,
    addr: UserAddress,
    length: usize,
) -> Result<SyscallResult, Errno> {
    ctx.task.mm.unmap(addr, length)?;
    Ok(SUCCESS)
}

pub fn sys_brk(ctx: &SyscallContext<'_>, addr: UserAddress) -> Result<SyscallResult, Errno> {
    // TODO(tbodt): explicit error mapping
    Ok(ctx.task.mm.set_program_break(addr).map_err(Errno::from_status_like_fdio)?.into())
}

pub fn sys_rt_sigprocmask(
    ctx: &SyscallContext<'_>,
    how: u32,
    user_set: UserRef<sigset_t>,
    user_old_set: UserRef<sigset_t>,
    sigset_size: usize,
) -> Result<SyscallResult, Errno> {
    if sigset_size != std::mem::size_of::<sigset_t>() {
        return Err(EINVAL);
    }
    match how {
        SIG_BLOCK | SIG_UNBLOCK | SIG_SETMASK => (),
        _ => return Err(EINVAL),
    };

    let mut signal_mask = ctx.task.signal_mask.lock();
    // If old_set is not null, store the previous value in old_set.
    if !user_old_set.is_null() {
        ctx.task.mm.write_object(user_old_set, &mut signal_mask)?;
    }

    // If set is null, how is ignored and the mask is not updated.
    if user_set.is_null() {
        return Ok(SUCCESS);
    }

    let mut new_mask = sigset_t::default();
    ctx.task.mm.read_object(user_set, &mut new_mask)?;

    let mut updated_signal_mask = match how {
        SIG_BLOCK => (*signal_mask | new_mask),
        SIG_UNBLOCK => *signal_mask & !new_mask,
        SIG_SETMASK => new_mask,
        // Arguments have already been verified, this should never match.
        _ => *signal_mask,
    };

    // Can't block SIGKILL, or SIGSTOP.
    updated_signal_mask = updated_signal_mask & !((1 << SIGSTOP) | (1 << SIGKILL));
    *signal_mask = updated_signal_mask;

    Ok(SUCCESS)
}

pub fn sys_pread64(
    ctx: &SyscallContext<'_>,
    fd: FdNumber,
    buf: UserAddress,
    count: usize,
    offset: usize,
) -> Result<SyscallResult, Errno> {
    let file = ctx.task.files.get(fd)?;
    let bytes = file.read_at(&ctx.task, offset, &[iovec_t { iov_base: buf, iov_len: count }])?;
    Ok(bytes.into())
}

pub fn sys_writev(
    ctx: &SyscallContext<'_>,
    fd: FdNumber,
    iovec_addr: UserAddress,
    iovec_count: i32,
) -> Result<SyscallResult, Errno> {
    let iovec_count: usize = iovec_count.try_into().map_err(|_| EINVAL)?;
    if iovec_count > UIO_MAXIOV as usize {
        return Err(EINVAL);
    }

    let mut iovecs: Vec<iovec_t> = Vec::new();
    iovecs.reserve(iovec_count); // TODO: try_reserve
    iovecs.resize(iovec_count, iovec_t::default());

    ctx.task.mm.read_memory(iovec_addr, iovecs.as_mut_slice().as_bytes_mut())?;
    let file = ctx.task.files.get(fd)?;
    Ok(file.write(&ctx.task, &iovecs)?.into())
}

pub fn sys_access(
    _ctx: &SyscallContext<'_>,
    _path: UserCString,
    _mode: i32,
) -> Result<SyscallResult, Errno> {
    Err(ENOSYS)
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

pub fn sys_exit(ctx: &SyscallContext<'_>, error_code: i32) -> Result<SyscallResult, Errno> {
    info!(target: "exit", "exit: tid={} error_code={}", ctx.task.get_tid(), error_code);
    Ok(SyscallResult::Exit(error_code))
}

pub fn sys_exit_group(ctx: &SyscallContext<'_>, error_code: i32) -> Result<SyscallResult, Errno> {
    info!(target: "exit", "exit_group: pid={} error_code={}", ctx.task.get_pid(), error_code);
    // TODO: Once we have more than one thread in a thread group, we'll need to exit them as well.
    Ok(SyscallResult::Exit(error_code))
}

pub fn sys_uname(
    ctx: &SyscallContext<'_>,
    name: UserRef<utsname_t>,
) -> Result<SyscallResult, Errno> {
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
    ctx.task.mm.write_object(name, &result)?;
    return Ok(SUCCESS);
}

pub fn sys_readlink(
    _ctx: &SyscallContext<'_>,
    _path: UserCString,
    _buffer: UserAddress,
    _buffer_size: usize,
) -> Result<SyscallResult, Errno> {
    Err(EINVAL)
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

pub fn sys_fstatfs(
    ctx: &SyscallContext<'_>,
    _fd: FdNumber,
    user_buf: UserRef<statfs>,
) -> Result<SyscallResult, Errno> {
    let result = statfs::default();
    ctx.task.mm.write_object(user_buf, &result)?;
    Ok(SUCCESS)
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
    tidptr: UserAddress,
) -> Result<SyscallResult, Errno> {
    *ctx.task.clear_child_tid.lock() = tidptr;
    Ok(ctx.task.get_tid().into())
}

pub fn sys_sigaltstack(
    ctx: &SyscallContext<'_>,
    user_ss: UserRef<sigaltstack_t>,
    user_old_ss: UserRef<sigaltstack_t>,
) -> Result<SyscallResult, Errno> {
    let mut ss = sigaltstack_t::default();
    if !user_ss.is_null() {
        ctx.task.mm.read_object(user_ss, &mut ss)?;
        if (ss.ss_flags & !(SS_AUTODISARM | SS_DISABLE)) != 0 {
            return Err(EINVAL);
        }
    }

    let mut signal_stack = ctx.task.signal_stack.lock();

    if !user_old_ss.is_null() {
        // TODO: Implement SS_ONSTACK when we actually call the signal handler.
        ctx.task.mm.write_object(
            user_old_ss,
            &match *signal_stack {
                Some(old_ss) => old_ss,
                None => sigaltstack_t { ss_flags: SS_DISABLE, ..sigaltstack_t::default() },
            },
        )?;
    }

    if !user_ss.is_null() {
        if ss.ss_flags & SS_DISABLE != 0 {
            *signal_stack = None;
        } else {
            *signal_stack = Some(ss);
        }
    }

    Ok(SUCCESS)
}

pub fn sys_openat(
    ctx: &SyscallContext<'_>,
    dir_fd: i32,
    user_path: UserCString,
    flags: i32,
    mode: i32,
) -> Result<SyscallResult, Errno> {
    if dir_fd != AT_FDCWD {
        not_implemented!("openat dirfds are unimplemented");
        return Err(EINVAL);
    }
    let mut buf = [0u8; PATH_MAX as usize];
    let path = ctx.task.mm.read_c_string(user_path, &mut buf)?;
    strace!("openat({}, {}, {:#x}, {:#o})", dir_fd, String::from_utf8_lossy(path), flags, mode);
    if path[0] != b'/' {
        not_implemented!("non-absolute paths are unimplemented");
        return Err(ENOENT);
    }
    let path = &path[1..];
    // TODO(tbodt): Need to switch to filesystem APIs that do not require UTF-8
    let path = std::str::from_utf8(path).expect("bad UTF-8 in filename");
    let description = syncio::directory_open(
        &ctx.task.fs.root,
        path,
        fio::OPEN_RIGHT_READABLE,
        0,
        zx::Time::INFINITE,
    )
    .map_err(|e| match e {
        zx::Status::NOT_FOUND => ENOENT,
        _ => {
            warn!("open failed: {:?}", e);
            EIO
        }
    })?;
    Ok(ctx.task.files.install_fd(RemoteFile::from_description(description))?.into())
}

pub fn sys_getrandom(
    ctx: &SyscallContext<'_>,
    buf_addr: UserAddress,
    size: usize,
    _flags: i32,
) -> Result<SyscallResult, Errno> {
    let mut buf = vec![0; size];
    let size = zx::cprng_draw(&mut buf).map_err(impossible_error)?;
    ctx.task.mm.write_memory(buf_addr, &buf[0..size])?;
    Ok(size.into())
}

const NANOS_PER_SECOND: i64 = 1000 * 1000 * 1000;

pub fn sys_clock_gettime(
    ctx: &SyscallContext<'_>,
    which_clock: u32,
    tp_addr: UserRef<timespec>,
) -> Result<SyscallResult, Errno> {
    let time = match which_clock {
        CLOCK_REALTIME => utc_time(),
        CLOCK_MONOTONIC => zx::Time::get_monotonic(),
        _ => return Err(EINVAL),
    };
    let nanos = time.into_nanos();
    let tv = timespec { tv_sec: nanos / NANOS_PER_SECOND, tv_nsec: nanos % NANOS_PER_SECOND };
    return ctx.task.mm.write_object(tp_addr, &tv).map(|_| SUCCESS);
}

pub fn sys_gettimeofday(
    ctx: &SyscallContext<'_>,
    user_tv: UserRef<timeval>,
    user_tz: UserRef<timezone>,
) -> Result<SyscallResult, Errno> {
    if !user_tv.is_null() {
        let now = utc_time().into_nanos();
        let tv =
            timeval { tv_sec: now / NANOS_PER_SECOND, tv_usec: (now % NANOS_PER_SECOND) / 1000 };
        ctx.task.mm.write_object(user_tv, &tv)?;
    }
    if !user_tz.is_null() {
        not_implemented!("gettimeofday does not implement tz argument");
    }
    return Ok(SUCCESS);
}

pub fn sys_getcwd(
    ctx: &SyscallContext<'_>,
    buf: UserAddress,
    size: usize,
) -> Result<SyscallResult, Errno> {
    // TODO: We should get the cwd from the file system context.
    let cwd = CString::new("/").unwrap();

    let bytes = cwd.as_bytes_with_nul();
    if bytes.len() > size {
        return Err(ERANGE);
    }
    ctx.task.mm.write_memory(buf, bytes)?;
    return Ok(bytes.len().into());
}

pub fn sys_ioctl(
    ctx: &SyscallContext<'_>,
    fd: FdNumber,
    request: u32,
    in_addr: UserAddress,
    out_addr: UserAddress,
) -> Result<SyscallResult, Errno> {
    let file = ctx.task.files.get(fd)?;
    file.ioctl(&ctx.task, request, in_addr, out_addr)
}

pub fn sys_unknown(_ctx: &SyscallContext<'_>, syscall_number: u64) -> Result<SyscallResult, Errno> {
    warn!(target: "unknown_syscall", "UNKNOWN syscall({}): {}", syscall_number, SyscallDecl::from_number(syscall_number).name);
    // TODO: We should send SIGSYS once we have signals.
    Err(ENOSYS)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::auth::Credentials;
    use crate::fs::test::create_test_file_system;
    use fuchsia_async as fasync;

    /// Creates a `Kernel` and `Task` for testing purposes.
    ///
    /// The `Task` is backed by a real process, and can be used to test syscalls.
    fn create_kernel_and_task() -> (Arc<Kernel>, TaskOwner) {
        let kernel =
            Kernel::new(&CString::new("test-kernel").unwrap()).expect("failed to create kernel");

        let task = Task::new(
            &kernel,
            &CString::new("test-task").unwrap(),
            create_test_file_system(),
            Credentials::default(),
        )
        .expect("failed to create first task");

        (kernel, task)
    }

    /// Maps `length` at `address` with `PROT_READ | PROT_WRITE`, `MAP_ANONYMOUS | MAP_PRIVATE`.
    ///
    /// Returns the address returned by `sys_mmap`.
    fn map_memory(ctx: &SyscallContext<'_>, address: UserAddress, length: u64) -> UserAddress {
        match sys_mmap(
            &ctx,
            address,
            length.try_into().unwrap(),
            PROT_READ | PROT_WRITE,
            MAP_ANONYMOUS | MAP_PRIVATE,
            FdNumber::from_raw(-1),
            0,
        )
        .unwrap()
        {
            SyscallResult::Success(address) => UserAddress::from(address),
            _ => {
                assert!(false, "Could not map memory");
                UserAddress::default()
            }
        }
    }

    fn signal_mask(signal: u32) -> u64 {
        1 << signal
    }

    /// It is ok to call munmap with an address that is a multiple of the page size, and
    /// a non-zero length.
    #[fasync::run_singlethreaded(test)]
    async fn test_munmap() {
        let (_kernel, task_owner) = create_kernel_and_task();
        let ctx = SyscallContext::new(&task_owner.task);

        let mapped_address = map_memory(&ctx, UserAddress::default(), *PAGE_SIZE);
        assert_eq!(sys_munmap(&ctx, mapped_address, *PAGE_SIZE as usize), Ok(SUCCESS));

        // Verify that the memory is no longer readable.
        let mut data: [u8; 5] = [0; 5];
        assert_eq!(ctx.task.mm.read_memory(mapped_address, &mut data), Err(EFAULT));
    }

    /// It is ok to call munmap on an unmapped range.
    #[fasync::run_singlethreaded(test)]
    async fn test_munmap_not_mapped() {
        let (_kernel, task_owner) = create_kernel_and_task();
        let ctx = SyscallContext::new(&task_owner.task);

        let mapped_address = map_memory(&ctx, UserAddress::default(), *PAGE_SIZE);
        assert_eq!(sys_munmap(&ctx, mapped_address, *PAGE_SIZE as usize), Ok(SUCCESS));
        assert_eq!(sys_munmap(&ctx, mapped_address, *PAGE_SIZE as usize), Ok(SUCCESS));
    }

    /// It is an error to call munmap with a length of 0.
    #[fasync::run_singlethreaded(test)]
    async fn test_munmap_0_length() {
        let (_kernel, task_owner) = create_kernel_and_task();
        let ctx = SyscallContext::new(&task_owner.task);

        let mapped_address = map_memory(&ctx, UserAddress::default(), *PAGE_SIZE);
        assert_eq!(sys_munmap(&ctx, mapped_address, 0), Err(EINVAL));
    }

    /// It is an error to call munmap with an address that is not a multiple of the page size.
    #[fasync::run_singlethreaded(test)]
    async fn test_munmap_not_aligned() {
        let (_kernel, task_owner) = create_kernel_and_task();
        let ctx = SyscallContext::new(&task_owner.task);

        let mapped_address = map_memory(&ctx, UserAddress::default(), *PAGE_SIZE);
        assert_eq!(sys_munmap(&ctx, mapped_address + 1u64, *PAGE_SIZE as usize), Err(EINVAL));

        // Verify that the memory is still readable.
        let mut data: [u8; 5] = [0; 5];
        assert_eq!(ctx.task.mm.read_memory(mapped_address, &mut data), Ok(()));
    }

    /// The entire page should be unmapped, not just the range [address, address + length).
    #[fasync::run_singlethreaded(test)]
    async fn test_munmap_unmap_partial() {
        let (_kernel, task_owner) = create_kernel_and_task();
        let ctx = SyscallContext::new(&task_owner.task);

        let mapped_address = map_memory(&ctx, UserAddress::default(), *PAGE_SIZE);
        assert_eq!(sys_munmap(&ctx, mapped_address, (*PAGE_SIZE as usize) / 2), Ok(SUCCESS));

        // Verify that memory can't be read in either half of the page.
        let mut data: [u8; 5] = [0; 5];
        assert_eq!(ctx.task.mm.read_memory(mapped_address, &mut data), Err(EFAULT));
        assert_eq!(
            ctx.task.mm.read_memory(mapped_address + (*PAGE_SIZE - 2), &mut data),
            Err(EFAULT)
        );
    }

    /// All pages that intersect the munmap range should be unmapped.
    #[fasync::run_singlethreaded(test)]
    async fn test_munmap_multiple_pages() {
        let (_kernel, task_owner) = create_kernel_and_task();
        let ctx = SyscallContext::new(&task_owner.task);

        let mapped_address = map_memory(&ctx, UserAddress::default(), *PAGE_SIZE * 2);
        assert_eq!(sys_munmap(&ctx, mapped_address, (*PAGE_SIZE as usize) + 1), Ok(SUCCESS));

        // Verify that neither page is readable.
        let mut data: [u8; 5] = [0; 5];
        assert_eq!(ctx.task.mm.read_memory(mapped_address, &mut data), Err(EFAULT));
        assert_eq!(
            ctx.task.mm.read_memory(mapped_address + *PAGE_SIZE + 1u64, &mut data),
            Err(EFAULT)
        );
    }

    /// Only the pages that intersect the munmap range should be unmapped.
    #[fasync::run_singlethreaded(test)]
    async fn test_munmap_one_of_many_pages() {
        let (_kernel, task_owner) = create_kernel_and_task();
        let ctx = SyscallContext::new(&task_owner.task);

        let mapped_address = map_memory(&ctx, UserAddress::default(), *PAGE_SIZE * 2);
        assert_eq!(sys_munmap(&ctx, mapped_address, (*PAGE_SIZE as usize) - 1), Ok(SUCCESS));

        // Verify that the second page is still readable.
        let mut data: [u8; 5] = [0; 5];
        assert_eq!(ctx.task.mm.read_memory(mapped_address, &mut data), Err(EFAULT));
        assert_eq!(ctx.task.mm.read_memory(mapped_address + *PAGE_SIZE + 1u64, &mut data), Ok(()));
    }

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

    #[fasync::run_singlethreaded(test)]
    async fn test_sigaltstack() {
        let (_kernel, task_owner) = create_kernel_and_task();
        let ctx = SyscallContext::new(&task_owner.task);
        let addr = map_memory(&ctx, UserAddress::default(), *PAGE_SIZE);

        let user_ss = UserRef::<sigaltstack_t>::new(addr);
        let nullptr = UserRef::<sigaltstack_t>::default();

        // Check that the initial state is disabled.
        sys_sigaltstack(&ctx, nullptr, user_ss).expect("failed to call sigaltstack");
        let mut ss = sigaltstack_t::default();
        ctx.task.mm.read_object(user_ss, &mut ss).expect("failed to read struct");
        assert!(ss.ss_flags & SS_DISABLE != 0);

        // Install a sigaltstack and read it back out.
        ss.ss_sp = UserAddress::from(0x7FFFF);
        ss.ss_size = 0x1000;
        ss.ss_flags = SS_AUTODISARM;
        ctx.task.mm.write_object(user_ss, &ss).expect("failed to write struct");
        sys_sigaltstack(&ctx, user_ss, nullptr).expect("failed to call sigaltstack");
        ctx.task
            .mm
            .write_memory(addr, &[0u8; std::mem::size_of::<sigaltstack_t>()])
            .expect("failed to clear struct");
        sys_sigaltstack(&ctx, nullptr, user_ss).expect("failed to call sigaltstack");
        let mut another_ss = sigaltstack_t::default();
        ctx.task.mm.read_object(user_ss, &mut another_ss).expect("failed to read struct");
        assert_eq!(ss, another_ss);

        // Disable the sigaltstack and read it back out.
        ss.ss_flags = SS_DISABLE;
        ctx.task.mm.write_object(user_ss, &ss).expect("failed to write struct");
        sys_sigaltstack(&ctx, user_ss, nullptr).expect("failed to call sigaltstack");
        ctx.task
            .mm
            .write_memory(addr, &[0u8; std::mem::size_of::<sigaltstack_t>()])
            .expect("failed to clear struct");
        sys_sigaltstack(&ctx, nullptr, user_ss).expect("failed to call sigaltstack");
        ctx.task.mm.read_object(user_ss, &mut ss).expect("failed to read struct");
        assert!(ss.ss_flags & SS_DISABLE != 0);
    }

    /// It is invalid to call rt_sigprocmask with a sigsetsize that does not match the size of
    /// sigset_t.
    #[fasync::run_singlethreaded(test)]
    async fn test_sigprocmask_invalid_size() {
        let (_kernel, task_owner) = create_kernel_and_task();
        let ctx = SyscallContext::new(&task_owner.task);

        let set = UserRef::<sigset_t>::default();
        let old_set = UserRef::<sigset_t>::default();
        let how = 0;

        assert_eq!(
            sys_rt_sigprocmask(&ctx, how, set, old_set, std::mem::size_of::<sigset_t>() * 2),
            Err(EINVAL)
        );
        assert_eq!(
            sys_rt_sigprocmask(&ctx, how, set, old_set, std::mem::size_of::<sigset_t>() / 2),
            Err(EINVAL)
        );
    }

    /// It is invalid to call rt_sigprocmask with a bad `how`.
    #[fasync::run_singlethreaded(test)]
    async fn test_sigprocmask_invalid_how() {
        let (_kernel, task_owner) = create_kernel_and_task();
        let ctx = SyscallContext::new(&task_owner.task);
        let addr = map_memory(&ctx, UserAddress::default(), *PAGE_SIZE);

        let set = UserRef::<sigset_t>::new(addr);
        let old_set = UserRef::<sigset_t>::default();
        let how = SIG_SETMASK | SIG_UNBLOCK | SIG_BLOCK;

        assert_eq!(
            sys_rt_sigprocmask(&ctx, how, set, old_set, std::mem::size_of::<sigset_t>()),
            Err(EINVAL)
        );
    }

    /// It is valid to call rt_sigprocmask with a null value for set. In that case, old_set should
    /// contain the current signal mask.
    #[fasync::run_singlethreaded(test)]
    async fn test_sigprocmask_null_set() {
        let (_kernel, task_owner) = create_kernel_and_task();
        let ctx = SyscallContext::new(&task_owner.task);
        let addr = map_memory(&ctx, UserAddress::default(), *PAGE_SIZE);
        let original_mask = signal_mask(SIGTRAP);
        {
            *ctx.task.signal_mask.lock() = original_mask;
        }

        let set = UserRef::<sigset_t>::default();
        let old_set = UserRef::<sigset_t>::new(addr);
        let how = SIG_SETMASK;

        ctx.task
            .mm
            .write_memory(addr, &[0u8; std::mem::size_of::<sigset_t>()])
            .expect("failed to clear struct");

        assert_eq!(
            sys_rt_sigprocmask(&ctx, how, set, old_set, std::mem::size_of::<sigset_t>()),
            Ok(SUCCESS)
        );

        let mut old_mask = sigset_t::default();
        ctx.task.mm.read_object(old_set, &mut old_mask).expect("failed to read mask");
        assert_eq!(old_mask, original_mask);
    }

    /// It is valid to call rt_sigprocmask with null values for both set and old_set.
    /// In this case, how should be ignored and the set remains the same.
    #[fasync::run_singlethreaded(test)]
    async fn test_sigprocmask_null_set_and_old_set() {
        let (_kernel, task_owner) = create_kernel_and_task();
        let ctx = SyscallContext::new(&task_owner.task);
        let original_mask = signal_mask(SIGTRAP);
        {
            *ctx.task.signal_mask.lock() = original_mask;
        }

        let set = UserRef::<sigset_t>::default();
        let old_set = UserRef::<sigset_t>::default();
        let how = SIG_SETMASK;

        assert_eq!(
            sys_rt_sigprocmask(&ctx, how, set, old_set, std::mem::size_of::<sigset_t>()),
            Ok(SUCCESS)
        );
        assert_eq!(*ctx.task.signal_mask.lock(), original_mask);
    }

    /// Calling rt_sigprocmask with SIG_SETMASK should set the mask to the provided set.
    #[fasync::run_singlethreaded(test)]
    async fn test_sigprocmask_setmask() {
        let (_kernel, task_owner) = create_kernel_and_task();
        let ctx = SyscallContext::new(&task_owner.task);
        let addr = map_memory(&ctx, UserAddress::default(), *PAGE_SIZE);
        ctx.task
            .mm
            .write_memory(addr, &[0u8; std::mem::size_of::<sigset_t>() * 2])
            .expect("failed to clear struct");

        let original_mask = signal_mask(SIGTRAP);
        {
            *ctx.task.signal_mask.lock() = original_mask;
        }

        let new_mask: sigset_t = signal_mask(SIGPOLL);
        let set = UserRef::<sigset_t>::new(addr);
        ctx.task.mm.write_object(set, &new_mask).expect("failed to set mask");

        let old_set = UserRef::<sigset_t>::new(addr + std::mem::size_of::<sigset_t>());
        let how = SIG_SETMASK;

        assert_eq!(
            sys_rt_sigprocmask(&ctx, how, set, old_set, std::mem::size_of::<sigset_t>()),
            Ok(SUCCESS)
        );

        let mut old_mask = sigset_t::default();
        ctx.task.mm.read_object(old_set, &mut old_mask).expect("failed to read mask");
        assert_eq!(old_mask, original_mask);
        assert_eq!(*ctx.task.signal_mask.lock(), new_mask);
    }

    /// Calling st_sigprocmask with a how of SIG_BLOCK should add to the existing set.
    #[fasync::run_singlethreaded(test)]
    async fn test_sigprocmask_block() {
        let (_kernel, task_owner) = create_kernel_and_task();
        let ctx = SyscallContext::new(&task_owner.task);
        let addr = map_memory(&ctx, UserAddress::default(), *PAGE_SIZE);
        ctx.task
            .mm
            .write_memory(addr, &[0u8; std::mem::size_of::<sigset_t>() * 2])
            .expect("failed to clear struct");

        let original_mask = signal_mask(SIGTRAP);
        {
            *ctx.task.signal_mask.lock() = original_mask;
        }

        let new_mask: sigset_t = signal_mask(SIGPOLL);
        let set = UserRef::<sigset_t>::new(addr);
        ctx.task.mm.write_object(set, &new_mask).expect("failed to set mask");

        let old_set = UserRef::<sigset_t>::new(addr + std::mem::size_of::<sigset_t>());
        let how = SIG_BLOCK;

        assert_eq!(
            sys_rt_sigprocmask(&ctx, how, set, old_set, std::mem::size_of::<sigset_t>()),
            Ok(SUCCESS)
        );

        let mut old_mask = sigset_t::default();
        ctx.task.mm.read_object(old_set, &mut old_mask).expect("failed to read mask");
        assert_eq!(old_mask, original_mask);
        assert_eq!(*ctx.task.signal_mask.lock(), new_mask | original_mask);
    }

    /// Calling st_sigprocmask with a how of SIG_UNBLOCK should remove from the existing set.
    #[fasync::run_singlethreaded(test)]
    async fn test_sigprocmask_unblock() {
        let (_kernel, task_owner) = create_kernel_and_task();
        let ctx = SyscallContext::new(&task_owner.task);
        let addr = map_memory(&ctx, UserAddress::default(), *PAGE_SIZE);
        ctx.task
            .mm
            .write_memory(addr, &[0u8; std::mem::size_of::<sigset_t>() * 2])
            .expect("failed to clear struct");

        let original_mask = signal_mask(SIGTRAP) | signal_mask(SIGPOLL);
        {
            *ctx.task.signal_mask.lock() = original_mask;
        }

        let new_mask: sigset_t = signal_mask(SIGTRAP);
        let set = UserRef::<sigset_t>::new(addr);
        ctx.task.mm.write_object(set, &new_mask).expect("failed to set mask");

        let old_set = UserRef::<sigset_t>::new(addr + std::mem::size_of::<sigset_t>());
        let how = SIG_UNBLOCK;

        assert_eq!(
            sys_rt_sigprocmask(&ctx, how, set, old_set, std::mem::size_of::<sigset_t>()),
            Ok(SUCCESS)
        );

        let mut old_mask = sigset_t::default();
        ctx.task.mm.read_object(old_set, &mut old_mask).expect("failed to read mask");
        assert_eq!(old_mask, original_mask);
        assert_eq!(*ctx.task.signal_mask.lock(), signal_mask(SIGPOLL));
    }

    /// It's ok to call sigprocmask to unblock a signal that is not set.
    #[fasync::run_singlethreaded(test)]
    async fn test_sigprocmask_unblock_not_set() {
        let (_kernel, task_owner) = create_kernel_and_task();
        let ctx = SyscallContext::new(&task_owner.task);
        let addr = map_memory(&ctx, UserAddress::default(), *PAGE_SIZE);
        ctx.task
            .mm
            .write_memory(addr, &[0u8; std::mem::size_of::<sigset_t>() * 2])
            .expect("failed to clear struct");

        let original_mask = signal_mask(SIGPOLL);
        {
            *ctx.task.signal_mask.lock() = original_mask;
        }

        let new_mask: sigset_t = signal_mask(SIGTRAP);
        let set = UserRef::<sigset_t>::new(addr);
        ctx.task.mm.write_object(set, &new_mask).expect("failed to set mask");

        let old_set = UserRef::<sigset_t>::new(addr + std::mem::size_of::<sigset_t>());
        let how = SIG_UNBLOCK;

        assert_eq!(
            sys_rt_sigprocmask(&ctx, how, set, old_set, std::mem::size_of::<sigset_t>()),
            Ok(SUCCESS)
        );

        let mut old_mask = sigset_t::default();
        ctx.task.mm.read_object(old_set, &mut old_mask).expect("failed to read mask");
        assert_eq!(old_mask, original_mask);
        assert_eq!(*ctx.task.signal_mask.lock(), original_mask);
    }

    /// It's not possible to block SIGKILL or SIGSTOP.
    #[fasync::run_singlethreaded(test)]
    async fn test_sigprocmask_kill_stop() {
        let (_kernel, task_owner) = create_kernel_and_task();
        let ctx = SyscallContext::new(&task_owner.task);
        let addr = map_memory(&ctx, UserAddress::default(), *PAGE_SIZE);
        ctx.task
            .mm
            .write_memory(addr, &[0u8; std::mem::size_of::<sigset_t>() * 2])
            .expect("failed to clear struct");

        let original_mask = signal_mask(SIGPOLL);
        {
            *ctx.task.signal_mask.lock() = original_mask;
        }

        let new_mask: sigset_t = signal_mask(SIGSTOP) | signal_mask(SIGKILL);
        let set = UserRef::<sigset_t>::new(addr);
        ctx.task.mm.write_object(set, &new_mask).expect("failed to set mask");

        let old_set = UserRef::<sigset_t>::new(addr + std::mem::size_of::<sigset_t>());
        let how = SIG_BLOCK;

        assert_eq!(
            sys_rt_sigprocmask(&ctx, how, set, old_set, std::mem::size_of::<sigset_t>()),
            Ok(SUCCESS)
        );

        let mut old_mask = sigset_t::default();
        ctx.task.mm.read_object(old_set, &mut old_mask).expect("failed to read mask");
        assert_eq!(old_mask, original_mask);
        assert_eq!(*ctx.task.signal_mask.lock(), original_mask);
    }
}

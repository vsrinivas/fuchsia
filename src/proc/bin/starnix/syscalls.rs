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
    fd: FileDescriptor,
    buffer: UserAddress,
    count: usize,
) -> Result<SyscallResult, Errno> {
    let file = ctx.task.files.get(fd)?;
    Ok(file.write(&ctx.task, &[iovec_t { iov_base: buffer, iov_len: count }])?.into())
}

pub fn sys_fcntl(
    ctx: &SyscallContext<'_>,
    fd: FileDescriptor,
    cmd: u32,
    arg: u64,
) -> Result<SyscallResult, Errno> {
    let file = ctx.task.files.get(fd)?;
    match cmd {
        F_GETOWN => Ok(file.get_async_owner().into()),
        F_SETOWN => {
            if arg > std::i32::MAX as u64 {
                // Negative values are process groups.
                not_implemented!("fcntl(F_SETOWN) does not support process groups");
                return Err(EINVAL);
            }
            let task = ctx.task.get_task(arg.try_into().map_err(|_| EINVAL)?);
            file.set_async_owner(task.map_or(0, |task| task.id));
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
    fd: FileDescriptor,
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
    fd: FileDescriptor,
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

    let root_base = ctx.task.mm.root_vmar.info().unwrap().base;
    let ptr = if addr.ptr() == 0 { 0 } else { addr.ptr() - root_base };
    let addr =
        ctx.task.mm.root_vmar.map(ptr, &vmo, vmo_offset as u64, length, zx_flags).map_err(|s| {
            match s {
                zx::Status::INVALID_ARGS => EINVAL,
                zx::Status::ACCESS_DENIED => EACCES, // or EPERM?
                zx::Status::NOT_SUPPORTED => ENODEV,
                zx::Status::NO_MEMORY => ENOMEM,
                _ => impossible_error(s),
            }
        })?;
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
    match unsafe { ctx.task.mm.root_vmar.unmap(addr.ptr(), length) } {
        Ok(_) => Ok(SUCCESS),
        Err(zx::Status::NOT_FOUND) => Ok(SUCCESS),
        Err(zx::Status::INVALID_ARGS) => Err(EINVAL),
        Err(status) => Err(impossible_error(status)),
    }
}

pub fn sys_brk(ctx: &SyscallContext<'_>, addr: UserAddress) -> Result<SyscallResult, Errno> {
    // TODO(tbodt): explicit error mapping
    Ok(ctx.task.mm.set_program_break(addr).map_err(Errno::from_status_like_fdio)?.into())
}

pub fn sys_pread64(
    ctx: &SyscallContext<'_>,
    fd: FileDescriptor,
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
    fd: FileDescriptor,
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
    _fd: FileDescriptor,
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

pub fn sys_unknown(_ctx: &SyscallContext<'_>, syscall_number: u64) -> Result<SyscallResult, Errno> {
    warn!(target: "unknown_syscall", "UNKNOWN syscall({}): {}", syscall_number, SyscallDecl::from_number(syscall_number).name);
    // TODO: We should send SIGSYS once we have signals.
    Err(ENOSYS)
}

// Call this when you get an error that should "never" happen, i.e. if it does that means the
// kernel was updated to produce some other error after this match was written.
// TODO(tbodt): find a better way to handle this than a panic.
pub fn impossible_error(status: zx::Status) -> Errno {
    panic!("encountered impossible error: {}", status);
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
    fn map_memory(context: &SyscallContext<'_>, address: UserAddress, length: u64) -> UserAddress {
        match sys_mmap(
            &context,
            address,
            length.try_into().unwrap(),
            PROT_READ | PROT_WRITE,
            MAP_ANONYMOUS | MAP_PRIVATE,
            FileDescriptor::from_raw(-1),
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

    /// It is ok to call munmap with an address that is a multiple of the page size, and
    /// a non-zero length.
    #[fasync::run_singlethreaded(test)]
    async fn test_munmap() {
        let (_kernel, task_owner) = create_kernel_and_task();
        let context = SyscallContext::new(&task_owner.task);

        let mapped_address = map_memory(&context, UserAddress::default(), *PAGE_SIZE);
        assert_eq!(sys_munmap(&context, mapped_address, *PAGE_SIZE as usize), Ok(SUCCESS));

        // Verify that the memory is no longer readable.
        let mut data: [u8; 5] = [0; 5];
        assert_eq!(context.task.mm.read_memory(mapped_address, &mut data), Err(EFAULT));
    }

    /// It is ok to call munmap on an unmapped range.
    #[fasync::run_singlethreaded(test)]
    async fn test_munmap_not_mapped() {
        let (_kernel, task_owner) = create_kernel_and_task();
        let context = SyscallContext::new(&task_owner.task);

        let mapped_address = map_memory(&context, UserAddress::default(), *PAGE_SIZE);
        assert_eq!(sys_munmap(&context, mapped_address, *PAGE_SIZE as usize), Ok(SUCCESS));
        assert_eq!(sys_munmap(&context, mapped_address, *PAGE_SIZE as usize), Ok(SUCCESS));
    }

    /// It is an error to call munmap with a length of 0.
    #[fasync::run_singlethreaded(test)]
    async fn test_munmap_0_length() {
        let (_kernel, task_owner) = create_kernel_and_task();
        let context = SyscallContext::new(&task_owner.task);

        let mapped_address = map_memory(&context, UserAddress::default(), *PAGE_SIZE);
        assert_eq!(sys_munmap(&context, mapped_address, 0), Err(EINVAL));
    }

    /// It is an error to call munmap with an address that is not a multiple of the page size.
    #[fasync::run_singlethreaded(test)]
    async fn test_munmap_not_aligned() {
        let (_kernel, task_owner) = create_kernel_and_task();
        let context = SyscallContext::new(&task_owner.task);

        let mapped_address = map_memory(&context, UserAddress::default(), *PAGE_SIZE);
        assert_eq!(sys_munmap(&context, mapped_address + 1, *PAGE_SIZE as usize), Err(EINVAL));

        // Verify that the memory is still readable.
        let mut data: [u8; 5] = [0; 5];
        assert_eq!(context.task.mm.read_memory(mapped_address, &mut data), Ok(()));
    }

    /// The entire page should be unmapped, not just the range [address, address + length).
    #[fasync::run_singlethreaded(test)]
    async fn test_munmap_unmap_partial() {
        let (_kernel, task_owner) = create_kernel_and_task();
        let context = SyscallContext::new(&task_owner.task);

        let mapped_address = map_memory(&context, UserAddress::default(), *PAGE_SIZE);
        assert_eq!(sys_munmap(&context, mapped_address, (*PAGE_SIZE as usize) / 2), Ok(SUCCESS));

        // Verify that memory can't be read in either half of the page.
        let mut data: [u8; 5] = [0; 5];
        assert_eq!(context.task.mm.read_memory(mapped_address, &mut data), Err(EFAULT));
        assert_eq!(
            context.task.mm.read_memory(mapped_address + (*PAGE_SIZE - 2), &mut data),
            Err(EFAULT)
        );
    }

    /// All pages that intersect the munmap range should be unmapped.
    #[fasync::run_singlethreaded(test)]
    async fn test_munmap_multiple_pages() {
        let (_kernel, task_owner) = create_kernel_and_task();
        let context = SyscallContext::new(&task_owner.task);

        let mapped_address = map_memory(&context, UserAddress::default(), *PAGE_SIZE * 2);
        assert_eq!(sys_munmap(&context, mapped_address, (*PAGE_SIZE as usize) + 1), Ok(SUCCESS));

        // Verify that neither page is readable.
        let mut data: [u8; 5] = [0; 5];
        assert_eq!(context.task.mm.read_memory(mapped_address, &mut data), Err(EFAULT));
        assert_eq!(
            context.task.mm.read_memory(mapped_address + *PAGE_SIZE + 1, &mut data),
            Err(EFAULT)
        );
    }

    /// Only the pages that intersect the munmap range should be unmapped.
    #[fasync::run_singlethreaded(test)]
    async fn test_munmap_one_of_many_pages() {
        let (_kernel, task_owner) = create_kernel_and_task();
        let context = SyscallContext::new(&task_owner.task);

        let mapped_address = map_memory(&context, UserAddress::default(), *PAGE_SIZE * 2);
        assert_eq!(sys_munmap(&context, mapped_address, (*PAGE_SIZE as usize) - 1), Ok(SUCCESS));

        // Verify that the second page is still readable.
        let mut data: [u8; 5] = [0; 5];
        assert_eq!(context.task.mm.read_memory(mapped_address, &mut data), Err(EFAULT));
        assert_eq!(context.task.mm.read_memory(mapped_address + *PAGE_SIZE + 1, &mut data), Ok(()));
    }
}

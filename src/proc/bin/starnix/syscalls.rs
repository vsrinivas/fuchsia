// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl::endpoints::Proxy;
use fidl_fuchsia_io as fio;
use fuchsia_runtime::utc_time;
use fuchsia_zircon::{
    self as zx, sys::zx_thread_state_general_regs_t, AsHandleRef, Task as zxTask,
};
use io_util::directory;
use io_util::node::OpenError;
use log::{info, warn};
use std::convert::TryInto;
use std::ffi::{CStr, CString};
use std::sync::Arc;
use zerocopy::AsBytes;

use crate::block_on;
use crate::fs::*;
use crate::mm::*;
use crate::task::*;
use crate::uapi::*;

pub struct SyscallContext<'a> {
    pub task: &'a Arc<Task>,

    /// A copy of the registers associated with the Zircon thread. Up-to-date values can be read
    /// from `self.handle.read_state_general_regs()`. To write these values back to the thread, call
    /// `self.handle.write_state_general_regs(self.registers)`.
    pub registers: zx_thread_state_general_regs_t,
}

pub fn sys_write(
    ctx: &SyscallContext<'_>,
    fd: FileDescriptor,
    buffer: UserAddress,
    count: usize,
) -> Result<SyscallResult, Errno> {
    let fd = ctx.task.files.get(fd)?;
    Ok(fd.write(&ctx.task, &[iovec_t { iov_base: buffer, iov_len: count }])?.into())
}

pub fn sys_fstat(
    ctx: &SyscallContext<'_>,
    fd: FileDescriptor,
    buffer: UserRef<stat_t>,
) -> Result<SyscallResult, Errno> {
    let fd = ctx.task.files.get(fd)?;
    let result = fd.fstat(&ctx.task)?;
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
    info!(
        "mmap({:#x}, {:#x}, {:#x}, {:#x}, {:?}, {:#x})",
        addr.ptr(),
        length,
        prot,
        flags,
        fd,
        offset
    );
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
        let fd = ctx.task.files.get(fd)?;
        let zx_prot = mmap_prot_to_vm_opt(prot);
        if flags & MAP_PRIVATE != 0 {
            // TODO(tbodt): Use VMO_FLAG_PRIVATE to have the filesystem server do the clone for us.
            let vmo = fd.get_vmo(&ctx.task, zx_prot - zx::VmarFlags::PERM_WRITE, flags)?;
            let mut clone_flags = zx::VmoChildOptions::COPY_ON_WRITE;
            if !zx_prot.contains(zx::VmarFlags::PERM_WRITE) {
                clone_flags |= zx::VmoChildOptions::NO_WRITE;
            }
            vmo.create_child(clone_flags, 0, vmo.get_size().map_err(impossible_error)?)
                .map_err(impossible_error)?
        } else {
            fd.get_vmo(&ctx.task, zx_prot, flags)?
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

pub fn sys_brk(ctx: &SyscallContext<'_>, addr: UserAddress) -> Result<SyscallResult, Errno> {
    // info!("brk: addr={}", addr);
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
    let fd = ctx.task.files.get(fd)?;
    let bytes = fd.read_at(&ctx.task, offset, &[iovec_t { iov_base: buf, iov_len: count }])?;
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
    let fd = ctx.task.files.get(fd)?;
    Ok(fd.write(&ctx.task, &iovecs)?.into())
}

pub fn sys_access(
    _ctx: &SyscallContext<'_>,
    path: UserCString,
    mode: i32,
) -> Result<SyscallResult, Errno> {
    info!("access: path={} mode={}", path, mode);
    Err(ENOSYS)
}

pub fn sys_getpid(_ctx: &SyscallContext<'_>) -> Result<SyscallResult, Errno> {
    // This is set to 1 because Bionic skips referencing /dev if getpid() == 1, under the
    // assumption that anything running after init will have access to /dev.
    // TODO(tbodt): actual PID field (e.g., ctx.task.get_pid()).
    Ok(1.into())
}

pub fn sys_gettid(ctx: &SyscallContext<'_>) -> Result<SyscallResult, Errno> {
    Ok(ctx.task.get_tid().into())
}

pub fn sys_exit(ctx: &SyscallContext<'_>, error_code: i32) -> Result<SyscallResult, Errno> {
    info!("exit: error_code={}", error_code);
    let process: &zx::Process = &ctx.task.thread_group.read().process;
    process.kill().map_err(impossible_error)?;
    Ok(SyscallResult::Exit(error_code))
}

pub fn sys_exit_group(ctx: &SyscallContext<'_>, error_code: i32) -> Result<SyscallResult, Errno> {
    info!("exit_group: error_code={}", error_code);
    let process: &zx::Process = &ctx.task.thread_group.read().process;
    process.kill().map_err(impossible_error)?;
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
    // info!(
    //     "readlink: path={} buffer={} buffer_size={}",
    //     path, buffer, buffer_size
    // );
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
            info!("arch_prctl: Unknown code: code=0x{:x} addr={}", code, addr);
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

async fn async_openat(
    parent: &fio::DirectoryProxy,
    path: &str,
    flags: u32,
    mode: u32,
) -> Result<fio::NodeSynchronousProxy, OpenError> {
    let node = directory::open_node(parent, path, flags, mode).await?;
    Ok(fio::NodeSynchronousProxy::new(node.into_channel().unwrap().into_zx_channel()))
}

pub fn sys_openat(
    ctx: &SyscallContext<'_>,
    dir_fd: i32,
    user_path: UserCString,
    flags: i32,
    mode: i32,
) -> Result<SyscallResult, Errno> {
    if dir_fd != AT_FDCWD {
        warn!("openat dirfds are unimplemented");
        return Err(EINVAL);
    }
    let mut buf = [0u8; PATH_MAX as usize];
    let path = ctx.task.mm.read_c_string(user_path, &mut buf)?;
    info!("openat({}, {}, {:#x}, {:#o})", dir_fd, String::from_utf8_lossy(path), flags, mode);
    if path[0] != b'/' {
        warn!("non-absolute paths are unimplemented");
        return Err(ENOENT);
    }
    let path = &path[1..];
    // TODO(tbodt): Need to switch to filesystem APIs that do not require UTF-8
    let path = std::str::from_utf8(path).expect("bad UTF-8 in filename");
    let node = block_on(async_openat(&ctx.task.fs.root, path, fio::OPEN_RIGHT_READABLE, 0))
        .map_err(|e| match e {
            OpenError::OpenError(zx::Status::NOT_FOUND) => ENOENT,
            _ => {
                warn!("open failed: {:?}", e);
                EIO
            }
        })?;
    Ok(ctx.task.files.install_fd(RemoteFile::from_node(node)?)?.into())
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
    tp_addr: UserRef<timespec_t>,
) -> Result<SyscallResult, Errno> {
    let time = match which_clock {
        CLOCK_REALTIME => utc_time(),
        CLOCK_MONOTONIC => zx::Time::get_monotonic(),
        _ => return Err(EINVAL),
    };
    let nanos = time.into_nanos();
    let tv = timespec_t { tv_sec: nanos / NANOS_PER_SECOND, tv_nsec: nanos % NANOS_PER_SECOND };
    return ctx.task.mm.write_object(tp_addr, &tv).map(|_| SUCCESS);
}

pub fn sys_gettimeofday(
    ctx: &SyscallContext<'_>,
    user_tv: UserRef<timeval_t>,
    user_tz: UserRef<timezone>,
) -> Result<SyscallResult, Errno> {
    if !user_tv.is_null() {
        let now = utc_time().into_nanos();
        let tv =
            timeval_t { tv_sec: now / NANOS_PER_SECOND, tv_usec: (now % NANOS_PER_SECOND) / 1000 };
        ctx.task.mm.write_object(user_tv, &tv)?;
    }
    if !user_tz.is_null() {
        warn!("NOT_IMPLEMENTED: gettimeofday does not implement tz argument");
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
    info!("UNKNOWN syscall({}): {}", syscall_number, SyscallDecl::from_number(syscall_number).name);
    // TODO: We should send SIGSYS once we have signals.
    Err(ENOSYS)
}

// Call this when you get an error that should "never" happen, i.e. if it does that means the
// kernel was updated to produce some other error after this match was written.
// TODO(tbodt): find a better way to handle this than a panic.
pub fn impossible_error(status: zx::Status) -> Errno {
    panic!("encountered impossible error: {}", status);
}

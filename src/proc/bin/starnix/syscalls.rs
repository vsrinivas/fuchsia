// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_io as fio;
use fuchsia_runtime::utc_time;
use fuchsia_zircon::{self as zx, AsHandleRef, Task};
use io_util::directory;
use io_util::node::OpenError;
use log::{info, warn};
use std::convert::TryInto;
use std::ffi::CStr;
use zerocopy::AsBytes;

use crate::block_on;
use crate::executive::*;
use crate::fs::*;
use crate::uapi::*;

pub fn sys_write(
    ctx: &ThreadContext,
    fd: FdNumber,
    buffer: UserAddress,
    count: usize,
) -> Result<SyscallResult, Errno> {
    let fd = ctx.process.fd_table.get(fd)?;
    Ok(fd.write(ctx, &[iovec_t { iov_base: buffer, iov_len: count }])?.into())
}

pub fn sys_fstat(
    ctx: &ThreadContext,
    fd: FdNumber,
    buffer: UserAddress,
) -> Result<SyscallResult, Errno> {
    let fd = ctx.process.fd_table.get(fd)?;
    let result = fd.fstat(ctx)?;
    let bytes = result.as_bytes();
    ctx.process.write_memory(buffer, bytes)?;
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
    ctx: &ThreadContext,
    addr: UserAddress,
    length: usize,
    prot: u32,
    flags: u32,
    fd: FdNumber,
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
    if offset as u64 % PAGE_SIZE != 0 {
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
        let fd = ctx.process.fd_table.get(fd)?;
        let zx_prot = mmap_prot_to_vm_opt(prot);
        if flags & MAP_PRIVATE != 0 {
            // TODO(tbodt): Use VMO_FLAG_PRIVATE to have the filesystem server do the clone for us.
            let vmo = fd.get_vmo(ctx, zx_prot - zx::VmarFlags::PERM_WRITE, flags)?;
            let mut clone_flags = zx::VmoChildOptions::COPY_ON_WRITE;
            if !zx_prot.contains(zx::VmarFlags::PERM_WRITE) {
                clone_flags |= zx::VmoChildOptions::NO_WRITE;
            }
            vmo.create_child(clone_flags, 0, vmo.get_size().map_err(impossible_error)?)
                .map_err(impossible_error)?
        } else {
            fd.get_vmo(ctx, zx_prot, flags)?
        }
    };
    let vmo_offset = if flags & MAP_ANONYMOUS != 0 { 0 } else { offset };

    let root_base = ctx.process.mm.root_vmar.info().unwrap().base;
    let ptr = if addr.ptr() == 0 { 0 } else { addr.ptr() - root_base };
    let addr =
        ctx.process.mm.root_vmar.map(ptr, &vmo, vmo_offset as u64, length, zx_flags).map_err(
            |s| match s {
                zx::Status::INVALID_ARGS => EINVAL,
                zx::Status::ACCESS_DENIED => EACCES, // or EPERM?
                zx::Status::NOT_SUPPORTED => ENODEV,
                zx::Status::NO_MEMORY => ENOMEM,
                _ => impossible_error(s),
            },
        )?;
    Ok(addr.into())
}

pub fn sys_mprotect(
    ctx: &ThreadContext,
    addr: UserAddress,
    length: usize,
    prot: u32,
) -> Result<SyscallResult, Errno> {
    // SAFETY: This is safe because the vmar belongs to a different process.
    unsafe { ctx.process.mm.root_vmar.protect(addr.ptr(), length, mmap_prot_to_vm_opt(prot)) }
        .map_err(|s| match s {
            zx::Status::INVALID_ARGS => EINVAL,
            // TODO: This should still succeed and change protection on whatever is mapped.
            zx::Status::NOT_FOUND => EINVAL,
            zx::Status::ACCESS_DENIED => EACCES,
            _ => EINVAL, // impossible!
        })?;
    Ok(SUCCESS)
}

pub fn sys_brk(ctx: &ThreadContext, addr: UserAddress) -> Result<SyscallResult, Errno> {
    // info!("brk: addr={}", addr);
    // TODO(tbodt): explicit error mapping
    Ok(ctx.process.mm.set_program_break(addr).map_err(Errno::from_status_like_fdio)?.into())
}

pub fn sys_pread64(
    ctx: &ThreadContext,
    fd: FdNumber,
    buf: UserAddress,
    count: usize,
    offset: usize,
) -> Result<SyscallResult, Errno> {
    let fd = ctx.process.fd_table.get(fd)?;
    let bytes = fd.read_at(ctx, offset, &[iovec_t { iov_base: buf, iov_len: count }])?;
    Ok(bytes.into())
}

pub fn sys_writev(
    ctx: &ThreadContext,
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

    ctx.process.read_memory(iovec_addr, iovecs.as_mut_slice().as_bytes_mut())?;
    let fd = ctx.process.fd_table.get(fd)?;
    Ok(fd.write(ctx, &iovecs)?.into())
}

pub fn sys_access(
    _ctx: &ThreadContext,
    path: UserAddress,
    mode: i32,
) -> Result<SyscallResult, Errno> {
    info!("access: path={} mode={}", path, mode);
    Err(ENOSYS)
}

pub fn sys_getpid(_ctx: &ThreadContext) -> Result<SyscallResult, Errno> {
    // This is set to 1 because Bionic skips referencing /dev if getpid() == 1, under the
    // assumption that anything running after init will have access to /dev.
    // TODO(tbodt): actual PID field
    Ok(1.into())
}

pub fn sys_exit(ctx: &ThreadContext, error_code: i32) -> Result<SyscallResult, Errno> {
    info!("exit: error_code={}", error_code);
    ctx.process.handle.kill().map_err(impossible_error)?;
    Ok(SyscallResult::Exit(error_code))
}

pub fn sys_uname(ctx: &ThreadContext, name: UserAddress) -> Result<SyscallResult, Errno> {
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
    let bytes = result.as_bytes();
    ctx.process.write_memory(name, bytes)?;
    return Ok(SUCCESS);
}

pub fn sys_readlink(
    _ctx: &ThreadContext,
    _path: UserAddress,
    _buffer: UserAddress,
    _buffer_size: usize,
) -> Result<SyscallResult, Errno> {
    // info!(
    //     "readlink: path={} buffer={} buffer_size={}",
    //     path, buffer, buffer_size
    // );
    Err(EINVAL)
}

pub fn sys_getuid(ctx: &ThreadContext) -> Result<SyscallResult, Errno> {
    Ok(ctx.process.security.uid.into())
}

pub fn sys_getgid(ctx: &ThreadContext) -> Result<SyscallResult, Errno> {
    Ok(ctx.process.security.gid.into())
}

pub fn sys_geteuid(ctx: &ThreadContext) -> Result<SyscallResult, Errno> {
    Ok(ctx.process.security.euid.into())
}

pub fn sys_getegid(ctx: &ThreadContext) -> Result<SyscallResult, Errno> {
    Ok(ctx.process.security.egid.into())
}

pub fn sys_fstatfs(
    ctx: &ThreadContext,
    _fd: FdNumber,
    buf_addr: UserAddress,
) -> Result<SyscallResult, Errno> {
    let result = statfs::default();
    ctx.process.write_memory(buf_addr, result.as_bytes())?;
    Ok(SUCCESS)
}

pub fn sys_sched_getscheduler(_ctx: &ThreadContext, _pid: i32) -> Result<SyscallResult, Errno> {
    Ok(SCHED_NORMAL.into())
}

pub fn sys_arch_prctl(
    ctx: &mut ThreadContext,
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
    ctx: &ThreadContext,
    tidptr: UserAddress,
) -> Result<SyscallResult, Errno> {
    *ctx.clear_child_tid.lock() = tidptr;
    Ok(ctx.thread_id.into())
}

pub fn sys_exit_group(ctx: &ThreadContext, error_code: i32) -> Result<SyscallResult, Errno> {
    info!("exit_group: error_code={}", error_code);
    ctx.process.handle.kill().map_err(impossible_error)?;
    Ok(SyscallResult::Exit(error_code))
}

pub fn sys_openat(
    ctx: &ThreadContext,
    dir_fd: i32,
    path_addr: UserAddress,
    flags: i32,
    mode: i32,
) -> Result<SyscallResult, Errno> {
    if dir_fd != AT_FDCWD {
        warn!("openat dirfds are unimplemented");
        return Err(EINVAL);
    }
    let mut buf = [0u8; PATH_MAX as usize];
    let path = ctx.process.read_c_string(path_addr, &mut buf)?;
    info!("openat({}, {}, {:#x}, {:#o})", dir_fd, String::from_utf8_lossy(path), flags, mode);
    if path[0] != b'/' {
        warn!("non-absolute paths are unimplemented");
        return Err(ENOENT);
    }
    let path = &path[1..];
    // TODO(tbodt): Need to switch to filesystem APIs that do not require UTF-8
    let path = std::str::from_utf8(path).expect("bad UTF-8 in filename");
    let node = block_on(directory::open_node(&ctx.process.root, path, fio::OPEN_RIGHT_READABLE, 0))
        .map_err(|e| match e {
            OpenError::OpenError(zx::Status::NOT_FOUND) => ENOENT,
            _ => {
                warn!("open failed: {:?}", e);
                EIO
            }
        })?;
    Ok(ctx.process.fd_table.install_fd(FidlFile::from_node(node)?)?.into())
}

pub fn sys_getrandom(
    ctx: &ThreadContext,
    buf_addr: UserAddress,
    size: usize,
    _flags: i32,
) -> Result<SyscallResult, Errno> {
    let mut buf = vec![0; size];
    let size = zx::cprng_draw(&mut buf).map_err(impossible_error)?;
    ctx.process.write_memory(buf_addr, &buf[0..size])?;
    Ok(size.into())
}

const NANOS_PER_SECOND: i64 = 1000 * 1000 * 1000;

pub fn sys_clock_gettime(
    ctx: &ThreadContext,
    which_clock: u32,
    tp_addr: UserAddress,
) -> Result<SyscallResult, Errno> {
    match which_clock {
        CLOCK_REALTIME => {
            let now = utc_time().into_nanos();
            let tv = timespec_t { tv_sec: now / NANOS_PER_SECOND, tv_nsec: now % NANOS_PER_SECOND };
            ctx.process.write_memory(tp_addr, tv.as_bytes()).map(|_| SUCCESS)
        }
        _ => Err(EINVAL),
    }
}

pub fn sys_gettimeofday(
    ctx: &ThreadContext,
    tv_addr: UserAddress,
    tz_addr: UserAddress,
) -> Result<SyscallResult, Errno> {
    let process = &ctx.process;
    if !tv_addr.is_null() {
        let now = utc_time().into_nanos();
        let tv =
            timeval_t { tv_sec: now / NANOS_PER_SECOND, tv_usec: (now % NANOS_PER_SECOND) / 1000 };
        process.write_memory(tv_addr, tv.as_bytes())?;
    }
    if !tz_addr.is_null() {
        warn!("NOT_IMPLEMENTED: gettimeofday does not implement tz argument");
    }
    return Ok(SUCCESS);
}

pub fn sys_unknown(_ctx: &ThreadContext, syscall_number: u64) -> Result<SyscallResult, Errno> {
    info!("UNKNOWN syscall: {}", syscall_number);
    // TODO: We should send SIGSYS once we have signals.
    Err(ENOSYS)
}

// Call this when you get an error that should "never" happen, i.e. if it does that means the
// kernel was updated to produce some other error after this match was written.
// TODO(tbodt): find a better way to handle this than a panic.
pub fn impossible_error(status: zx::Status) -> Errno {
    panic!("encountered impossible error: {}", status);
}

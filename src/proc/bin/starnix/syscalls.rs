// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon::{self as zx, AsHandleRef, Task};
use log::info;
use std::convert::TryInto;
use std::ffi::CStr;
use zerocopy::AsBytes;

use crate::executive::*;
use crate::types::*;

pub fn sys_write(
    ctx: &ThreadContext,
    _fd: i32,
    buffer: UserAddress,
    count: usize,
) -> Result<SyscallResult, Errno> {
    let process = &ctx.process;
    let mut local = vec![0; count];
    process.read_memory(buffer, &mut local)?;
    info!("write: {}", String::from_utf8_lossy(&local));
    Ok(count.into())
}

pub fn sys_fstat(
    ctx: &ThreadContext,
    _fd: i32,
    buffer: UserAddress,
) -> Result<SyscallResult, Errno> {
    let process = &ctx.process;
    let mut result = stat_t::default();
    result.st_dev = 0x16;
    result.st_ino = 3;
    result.st_nlink = 1;
    result.st_mode = 0x2190;
    result.st_uid = process.security.uid;
    result.st_gid = process.security.gid;
    result.st_rdev = 0x8800;
    let bytes = result.as_bytes();
    process.write_memory(buffer, bytes)?;
    return Ok(SUCCESS);
}

pub fn sys_mprotect(
    _ctx: &ThreadContext,
    _addr: UserAddress,
    _length: usize,
    _prot: i32,
) -> Result<SyscallResult, Errno> {
    // TODO: Actually do the mprotect.
    Ok(SUCCESS)
}

pub fn sys_brk(ctx: &ThreadContext, addr: UserAddress) -> Result<SyscallResult, Errno> {
    // info!("brk: addr={}", addr);
    Ok(ctx.process.mm.set_program_break(addr).map_err(Errno::from)?.into())
}

pub fn sys_mmap(
    ctx: &ThreadContext,
    addr: UserAddress,
    length: usize,
    prot: i32,
    flags: i32,
    fd: i32,
    offset: usize,
) -> Result<SyscallResult, Errno> {
    info!(
        "mmap({:#x}, {:#x}, {:#x}, {:#x}, {}, {:#x})",
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
    if flags & !(MAP_PRIVATE | MAP_ANONYMOUS) != 0 {
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

    let mut zx_flags = zx::VmarFlags::ALLOW_FAULTS;
    if addr.ptr() != 0 {
        zx_flags |= zx::VmarFlags::SPECIFIC;
    }
    if prot & PROT_READ != 0 {
        zx_flags |= zx::VmarFlags::PERM_READ;
    }
    if prot & PROT_WRITE != 0 {
        zx_flags |= zx::VmarFlags::PERM_WRITE;
    }
    if prot & PROT_EXEC != 0 {
        zx_flags |= zx::VmarFlags::PERM_EXECUTE;
    }

    let vmo = zx::Vmo::create(length as u64).map_err(|s| match s {
        zx::Status::NO_MEMORY => ENOMEM,
        _ => impossible_error(s),
    })?;
    vmo.set_name(CStr::from_bytes_with_nul(b"starnix-anon\0").unwrap()).map_err(impossible_error)?;

    let addr = ctx.process.mm.root_vmar.map(addr.ptr(), &vmo, 0, length, zx_flags).map_err(
        |s| match s {
            zx::Status::INVALID_ARGS => EINVAL,
            zx::Status::ACCESS_DENIED => EACCES, // or EPERM?
            zx::Status::NOT_SUPPORTED => ENODEV,
            zx::Status::BUFFER_TOO_SMALL => panic!("faults should have been allowed!"),
            zx::Status::NO_MEMORY => ENOMEM,
            _ => impossible_error(s),
        },
    )?;
    Ok(addr.into())
}

pub fn sys_writev(
    ctx: &ThreadContext,
    fd: i32,
    iovec_addr: UserAddress,
    iovec_count: i32,
) -> Result<SyscallResult, Errno> {
    let iovec_count: usize = iovec_count.try_into().map_err(|_| EINVAL)?;
    if iovec_count > UIO_MAXIOV as usize {
        return Err(EINVAL);
    }

    let mut iovecs: Vec<iovec> = Vec::new();
    iovecs.reserve(iovec_count); // TODO: try_reserve
    iovecs.resize(iovec_count, iovec::default());
    ctx.process.read_memory(iovec_addr, iovecs.as_mut_slice().as_bytes_mut())?;

    info!("writev: fd={} iovec={:?}", fd, iovecs);

    let mut count = 0;
    for iovec in iovecs {
        let mut data = vec![0; iovec.iov_len];
        ctx.process.read_memory(iovec.iov_base, &mut data)?;
        info!("writev: {}", String::from_utf8_lossy(&data));
        count += data.len();
    }
    Ok(count.into())
}

pub fn sys_access(
    _ctx: &ThreadContext,
    path: UserAddress,
    mode: i32,
) -> Result<SyscallResult, Errno> {
    info!("access: path={} mode={}", path, mode);
    Err(ENOSYS)
}

pub fn sys_exit(ctx: &ThreadContext, error_code: i32) -> Result<SyscallResult, Errno> {
    info!("exit: error_code={}", error_code);
    // TODO: Set the error_code on the process.
    ctx.process.handle.kill().map_err(Errno::from)?;
    Ok(SUCCESS)
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

pub fn sys_arch_prctl(
    ctx: &mut ThreadContext,
    code: i32,
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

pub fn sys_exit_group(ctx: &ThreadContext, error_code: i32) -> Result<SyscallResult, Errno> {
    info!("exit_group: error_code={}", error_code);
    // TODO: Set the error_code on the process.
    ctx.process.handle.kill().map_err(Errno::from)?;
    Ok(SUCCESS)
}

pub fn sys_unknown(
    ctx: &ThreadContext,
    syscall_number: syscall_number_t,
) -> Result<SyscallResult, Errno> {
    info!("UNKNOWN syscall: {}", syscall_number);
    info!("rip={:#x}", ctx.registers.rip);
    info!("rax={:#x}", ctx.registers.rax);
    info!("rdi={:#x}", ctx.registers.rdi);
    info!("rsi={:#x}", ctx.registers.rsi);
    info!("rdx={:#x}", ctx.registers.rdx);
    info!("r10={:#x}", ctx.registers.r10);
    info!("r8={:#x}", ctx.registers.r8);
    info!("r9={:#x}", ctx.registers.r9);
    // TODO: We should send SIGSYS once we have signals.
    Err(ENOSYS)
}

// Call this when you get an error that should "never" happen, i.e. if it does that means the
// kernel was updated to produce some other error after this match was written.
// TODO(tbodt): find a better way to handle this than a panic.
pub fn impossible_error(status: zx::Status) -> Errno {
    panic!("encountered impossible error: {}", status);
}


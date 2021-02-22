// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {fuchsia_zircon::Task, zerocopy::AsBytes};

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
    println!("starnix: write: {}", String::from_utf8_lossy(&local));
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
    // println!("starnix: brk: addr={}", addr);
    Ok(ctx.process.mm.set_program_break(addr).map_err(Errno::from)?.into())
}

pub fn sys_writev(
    _ctx: &ThreadContext,
    fd: i32,
    iovec: UserAddress,
    iovec_count: i32,
) -> Result<SyscallResult, Errno> {
    println!("starnix: writev: fd={} iovec={} iovec_count={}", fd, iovec, iovec_count);
    Err(ENOSYS)
}

pub fn sys_access(
    _ctx: &ThreadContext,
    path: UserAddress,
    mode: i32,
) -> Result<SyscallResult, Errno> {
    println!("starnix: access: path={} mode={}", path, mode);
    Err(ENOSYS)
}

pub fn sys_exit(ctx: &ThreadContext, error_code: i32) -> Result<SyscallResult, Errno> {
    println!("starnix: exit: error_code={}", error_code);
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
    // println!(
    //     "starnix: readlink: path={} buffer={} buffer_size={}",
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
            println!("starnix: arch_prctl: Unknown code: code=0x{:x} addr={}", code, addr);
            Err(ENOSYS)
        }
    }
}

pub fn sys_exit_group(ctx: &ThreadContext, error_code: i32) -> Result<SyscallResult, Errno> {
    println!("starnix: exit_group: error_code={}", error_code);
    // TODO: Set the error_code on the process.
    ctx.process.handle.kill().map_err(Errno::from)?;
    Ok(SUCCESS)
}

pub fn sys_unknown(
    ctx: &ThreadContext,
    syscall_number: syscall_number_t,
) -> Result<SyscallResult, Errno> {
    println!("starnix: UNKNOWN syscall: {}", syscall_number);
    println!("starnix: rip={:#x}", ctx.registers.rip);
    println!("starnix: rax={:#x}", ctx.registers.rax);
    println!("starnix: rdi={:#x}", ctx.registers.rdi);
    println!("starnix: rsi={:#x}", ctx.registers.rsi);
    println!("starnix: rdx={:#x}", ctx.registers.rdx);
    println!("starnix: r10={:#x}", ctx.registers.r10);
    println!("starnix: r8={:#x}", ctx.registers.r8);
    println!("starnix: r9={:#x}", ctx.registers.r9);
    // TODO: We should send SIGSYS once we have signals.
    Err(ENOSYS)
}

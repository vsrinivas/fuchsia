// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_io as fio;
use fuchsia_zircon as zx;
use log::warn;
use std::convert::TryInto;
use std::ffi::CString;
use zerocopy::AsBytes;

use crate::fs::*;
use crate::not_implemented;
use crate::strace;
use crate::syscalls::*;
use crate::types::*;

pub fn sys_read(
    ctx: &SyscallContext<'_>,
    fd: FdNumber,
    buffer: UserAddress,
    count: usize,
) -> Result<SyscallResult, Errno> {
    let file = ctx.task.files.get(fd)?;
    Ok(file.ops().read(&file, &ctx.task, &[iovec_t { iov_base: buffer, iov_len: count }])?.into())
}

pub fn sys_write(
    ctx: &SyscallContext<'_>,
    fd: FdNumber,
    buffer: UserAddress,
    count: usize,
) -> Result<SyscallResult, Errno> {
    let file = ctx.task.files.get(fd)?;
    Ok(file.ops().write(&file, &ctx.task, &[iovec_t { iov_base: buffer, iov_len: count }])?.into())
}

pub fn sys_close(ctx: &SyscallContext<'_>, fd: FdNumber) -> Result<SyscallResult, Errno> {
    ctx.task.files.close(fd)?;
    Ok(SUCCESS)
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
    let result = file.ops().fstat(&file, &ctx.task)?;
    ctx.task.mm.write_object(buffer, &result)?;
    return Ok(SUCCESS);
}

pub fn sys_pread64(
    ctx: &SyscallContext<'_>,
    fd: FdNumber,
    buf: UserAddress,
    count: usize,
    offset: usize,
) -> Result<SyscallResult, Errno> {
    let file = ctx.task.files.get(fd)?;
    let bytes = file.ops().read_at(
        &file,
        &ctx.task,
        offset,
        &[iovec_t { iov_base: buf, iov_len: count }],
    )?;
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
    Ok(file.ops().write(&file, &ctx.task, &iovecs)?.into())
}

pub fn sys_access(
    _ctx: &SyscallContext<'_>,
    _path: UserCString,
    _mode: i32,
) -> Result<SyscallResult, Errno> {
    Err(ENOSYS)
}

pub fn sys_readlink(
    _ctx: &SyscallContext<'_>,
    _path: UserCString,
    _buffer: UserAddress,
    _buffer_size: usize,
) -> Result<SyscallResult, Errno> {
    Err(EINVAL)
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
    file.ops().ioctl(&file, &ctx.task, request, in_addr, out_addr)
}

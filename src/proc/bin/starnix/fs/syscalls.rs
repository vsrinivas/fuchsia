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
use crate::task::*;
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

pub fn sys_fstatfs(
    ctx: &SyscallContext<'_>,
    _fd: FdNumber,
    user_buf: UserRef<statfs>,
) -> Result<SyscallResult, Errno> {
    let result = statfs::default();
    ctx.task.mm.write_object(user_buf, &result)?;
    Ok(SUCCESS)
}

fn get_fio_flags_from_open_flags(open_flags: u32) -> Result<u32, Errno> {
    match open_flags & O_RDWR {
        O_RDONLY => Ok(fio::OPEN_RIGHT_READABLE),
        O_WRONLY => Ok(fio::OPEN_RIGHT_WRITABLE),
        O_RDWR => Ok(fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE),
        _ => Err(EINVAL),
    }
}

fn open_internal(
    task: &Task,
    dir_fd: FdNumber,
    user_path: UserCString,
    fio_flags: u32,
    mode: mode_t,
) -> Result<FileHandle, Errno> {
    if dir_fd != FdNumber::AT_FDCWD {
        not_implemented!("dirfds are unimplemented");
        return Err(EINVAL);
    }
    let mut buf = [0u8; PATH_MAX as usize];
    let path = task.mm.read_c_string(user_path, &mut buf)?;
    strace!(
        "open_internal({}, {}, {:#x}, {:#o})",
        dir_fd,
        String::from_utf8_lossy(path),
        fio_flags,
        mode
    );
    if path[0] != b'/' {
        not_implemented!("non-absolute paths are unimplemented");
        return Err(ENOENT);
    }
    let path = &path[1..];
    // TODO(tbodt): Need to switch to filesystem APIs that do not require UTF-8
    let path = std::str::from_utf8(path).expect("bad UTF-8 in filename");

    // TODO: Check fio::OPEN_FLAG_CREATE and use task.fs.apply_umask().

    let description = syncio::directory_open(&task.fs.root, path, fio_flags, 0, zx::Time::INFINITE)
        .map_err(|e| match e {
            zx::Status::NOT_FOUND => ENOENT,
            _ => {
                warn!("open failed: {:?}", e);
                EIO
            }
        })?;
    Ok(RemoteFile::from_description(description))
}

pub fn sys_openat(
    ctx: &SyscallContext<'_>,
    dir_fd: FdNumber,
    user_path: UserCString,
    flags: u32,
    mode: mode_t,
) -> Result<SyscallResult, Errno> {
    let fio_flags = get_fio_flags_from_open_flags(flags)?;
    let file = open_internal(&ctx.task, dir_fd, user_path, fio_flags, mode)?;
    Ok(ctx.task.files.install_fd(file)?.into())
}

pub fn sys_faccessat(
    ctx: &SyscallContext<'_>,
    dir_fd: FdNumber,
    user_path: UserCString,
    mode: u32,
) -> Result<SyscallResult, Errno> {
    // These values are defined in libc headers rather than in UAPI headers.
    const F_OK: u32 = 0;
    const X_OK: u32 = 1;
    const W_OK: u32 = 2;
    const R_OK: u32 = 4;

    if mode & !(X_OK | W_OK | R_OK) != 0 {
        return Err(EINVAL);
    }

    let fio_flags = if mode == F_OK {
        // TODO: Using open_internal to implement faccessat is not quite correct.
        //       For example, we cannot properly implement F_OK, which could
        //       succeed for write-only files. Implementing faccessat properly
        //       will require a more complete VFS that can perform access checks
        //       directly.
        fio::OPEN_RIGHT_READABLE
    } else {
        let mut fio_flags = 0;
        if mode & X_OK != 0 {
            fio_flags |= fio::OPEN_RIGHT_EXECUTABLE;
        }
        if mode & W_OK != 0 {
            fio_flags |= fio::OPEN_RIGHT_WRITABLE;
        }
        if mode & R_OK != 0 {
            fio_flags |= fio::OPEN_RIGHT_READABLE;
        }
        fio_flags
    };

    let _ = open_internal(&ctx.task, dir_fd, user_path, fio_flags, 0)?;
    Ok(SUCCESS)
}

pub fn sys_access(
    ctx: &SyscallContext<'_>,
    user_path: UserCString,
    mode: u32,
) -> Result<SyscallResult, Errno> {
    sys_faccessat(ctx, FdNumber::AT_FDCWD, user_path, mode)
}

pub fn sys_fstat(
    ctx: &SyscallContext<'_>,
    fd: FdNumber,
    buffer: UserRef<stat_t>,
) -> Result<SyscallResult, Errno> {
    let file = ctx.task.files.get(fd)?;
    let result = file.ops().fstat(&file, &ctx.task)?;
    ctx.task.mm.write_object(buffer, &result)?;
    Ok(SUCCESS)
}

pub fn sys_newfstatat(
    ctx: &SyscallContext<'_>,
    dir_fd: FdNumber,
    user_path: UserCString,
    buffer: UserRef<stat_t>,
    flags: u32,
) -> Result<SyscallResult, Errno> {
    if flags != 0 {
        not_implemented!("newfstatat: flags 0x{:x}", flags);
        return Err(ENOSYS);
    }
    let fio_flags = fio::OPEN_RIGHT_READABLE;
    let file = open_internal(&ctx.task, dir_fd, user_path, fio_flags, 0)?;
    let result = file.ops().fstat(&file, &ctx.task)?;
    ctx.task.mm.write_object(buffer, &result)?;
    Ok(SUCCESS)
}

pub fn sys_readlinkat(
    ctx: &SyscallContext<'_>,
    dir_fd: FdNumber,
    user_path: UserCString,
    _buffer: UserAddress,
    _buffer_size: usize,
) -> Result<SyscallResult, Errno> {
    let _ = open_internal(&ctx.task, dir_fd, user_path, fio::OPEN_RIGHT_READABLE, 0)?;
    Err(EINVAL)
}

pub fn sys_readlink(
    ctx: &SyscallContext<'_>,
    user_path: UserCString,
    buffer: UserAddress,
    buffer_size: usize,
) -> Result<SyscallResult, Errno> {
    sys_readlinkat(ctx, FdNumber::AT_FDCWD, user_path, buffer, buffer_size)
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

pub fn sys_umask(ctx: &SyscallContext<'_>, umask: mode_t) -> Result<SyscallResult, Errno> {
    Ok(ctx.task.fs.set_umask(umask).into())
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

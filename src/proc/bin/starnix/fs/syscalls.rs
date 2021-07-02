// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_io as fio;
use std::convert::TryInto;
use std::ffi::CString;

use crate::fs::pipe::*;
use crate::fs::*;
use crate::not_implemented;
use crate::strace;
use crate::syscalls::*;
use crate::task::*;
use crate::types::*;

pub fn sys_read(
    ctx: &SyscallContext<'_>,
    fd: FdNumber,
    address: UserAddress,
    length: usize,
) -> Result<SyscallResult, Errno> {
    let file = ctx.task.files.get(fd)?;
    Ok(file.read(&ctx.task, &[UserBuffer { address, length }])?.into())
}

pub fn sys_write(
    ctx: &SyscallContext<'_>,
    fd: FdNumber,
    address: UserAddress,
    length: usize,
) -> Result<SyscallResult, Errno> {
    let file = ctx.task.files.get(fd)?;
    Ok(file.write(&ctx.task, &[UserBuffer { address, length }])?.into())
}

pub fn sys_close(ctx: &SyscallContext<'_>, fd: FdNumber) -> Result<SyscallResult, Errno> {
    ctx.task.files.close(fd)?;
    Ok(SUCCESS)
}

pub fn sys_lseek(
    ctx: &SyscallContext<'_>,
    fd: FdNumber,
    offset: off_t,
    whence: u32,
) -> Result<SyscallResult, Errno> {
    let file = ctx.task.files.get(fd)?;
    Ok(file.seek(&ctx.task, offset, SeekOrigin::from_raw(whence)?)?.into())
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
        F_GETFD => Ok(ctx.task.files.get_fd_flags(fd)?.bits().into()),
        F_SETFD => {
            ctx.task.files.set_fd_flags(fd, FdFlags::from_bits_truncate(arg as u32))?;
            Ok(SUCCESS)
        }
        F_GETFL => {
            let file = ctx.task.files.get(fd)?;
            let flags = file.flags.lock();
            Ok((*flags).into())
        }
        F_SETFL => {
            // TODO: Add O_ASYNC once we have a decl for it.
            const SETTABLE_FLAGS: u32 = O_APPEND | O_DIRECT | O_NOATIME | O_NONBLOCK;
            let requested_flags = (arg as u32) & SETTABLE_FLAGS;
            let file = ctx.task.files.get(fd)?;
            let mut file_flags = file.flags.lock();
            *file_flags = (*file_flags & !SETTABLE_FLAGS) | requested_flags;
            Ok(SUCCESS)
        }
        F_GETPIPE_SZ | F_SETPIPE_SZ => {
            let file = ctx.task.files.get(fd)?;
            file.fcntl(&ctx.task, cmd, arg)
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
    address: UserAddress,
    length: usize,
    offset: usize,
) -> Result<SyscallResult, Errno> {
    let file = ctx.task.files.get(fd)?;
    Ok(file.read_at(&ctx.task, offset, &[UserBuffer { address, length }])?.into())
}

pub fn sys_pwrite64(
    ctx: &SyscallContext<'_>,
    fd: FdNumber,
    address: UserAddress,
    length: usize,
    offset: usize,
) -> Result<SyscallResult, Errno> {
    let file = ctx.task.files.get(fd)?;
    Ok(file.write_at(&ctx.task, offset, &[UserBuffer { address, length }])?.into())
}

pub fn sys_readv(
    ctx: &SyscallContext<'_>,
    fd: FdNumber,
    iovec_addr: UserAddress,
    iovec_count: i32,
) -> Result<SyscallResult, Errno> {
    let iovec = ctx.task.mm.read_iovec(iovec_addr, iovec_count)?;
    let file = ctx.task.files.get(fd)?;
    Ok(file.read(&ctx.task, &iovec)?.into())
}

pub fn sys_writev(
    ctx: &SyscallContext<'_>,
    fd: FdNumber,
    iovec_addr: UserAddress,
    iovec_count: i32,
) -> Result<SyscallResult, Errno> {
    let iovec = ctx.task.mm.read_iovec(iovec_addr, iovec_count)?;
    let file = ctx.task.files.get(fd)?;
    Ok(file.write(&ctx.task, &iovec)?.into())
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
    // TODO(tbodt): handle the flags properly
    let mut buf = [0u8; PATH_MAX as usize];
    let path = task.mm.read_c_string(user_path, &mut buf)?;
    if path[0] != b'/' {
        not_implemented!("non-absolute paths are unimplemented");
        if dir_fd != FdNumber::AT_FDCWD {
            not_implemented!("dirfds are unimplemented");
        }
        return Err(ENOENT);
    }
    strace!(
        task,
        "open_internal({}, {}, {:#x}, {:#o})",
        dir_fd,
        String::from_utf8_lossy(path),
        fio_flags,
        mode
    );
    let path = &path[1..];
    Ok(task.fs.lookup_node(path)?.open()?)
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
    Ok(ctx.task.files.add(file)?.into())
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
    let result = file.node.update_stat()?;
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
    let result = file.node.update_stat()?;
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

fn get_fd_flags(flags: u32) -> FdFlags {
    if flags & O_CLOEXEC != 0 {
        FdFlags::CLOEXEC
    } else {
        FdFlags::empty()
    }
}

pub fn sys_pipe2(
    ctx: &SyscallContext<'_>,
    user_pipe: UserRef<FdNumber>,
    flags: u32,
) -> Result<SyscallResult, Errno> {
    if flags & !(O_CLOEXEC | O_NONBLOCK | O_DIRECT) != 0 {
        return Err(EINVAL);
    }
    let (read, write) = new_pipe(ctx.kernel());

    let file_flags = flags & (O_NONBLOCK | O_DIRECT);
    read.set_file_flags(O_RDONLY | file_flags);
    write.set_file_flags(O_WRONLY | file_flags);

    let fd_flags = get_fd_flags(flags);
    let fd_read = ctx.task.files.add_with_flags(read, fd_flags)?;
    let fd_write = ctx.task.files.add_with_flags(write, fd_flags)?;

    ctx.task.mm.write_object(user_pipe, &fd_read)?;
    let user_pipe = user_pipe.next();
    ctx.task.mm.write_object(user_pipe, &fd_write)?;

    Ok(SUCCESS)
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

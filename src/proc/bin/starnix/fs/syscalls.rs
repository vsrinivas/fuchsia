// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::convert::TryInto;
use std::usize;

use crate::fs::memfd::*;
use crate::fs::pipe::*;
use crate::fs::*;
use crate::not_implemented;
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
        F_DUPFD => {
            let newfd = ctx.task.files.duplicate(fd, None, FdFlags::empty())?;
            Ok(newfd.into())
        }
        F_DUPFD_CLOEXEC => {
            let newfd = ctx.task.files.duplicate(fd, None, FdFlags::CLOEXEC)?;
            Ok(newfd.into())
        }
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
            Ok(file.flags().bits().into())
        }
        F_SETFL => {
            // TODO: Add O_ASYNC once we have a decl for it.
            let settable_flags =
                OpenFlags::APPEND | OpenFlags::DIRECT | OpenFlags::NOATIME | OpenFlags::NONBLOCK;
            let requested_flags =
                OpenFlags::from_bits_truncate((arg as u32) & settable_flags.bits());
            let file = ctx.task.files.get(fd)?;
            file.update_file_flags(requested_flags, settable_flags);
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
    offset: off_t,
) -> Result<SyscallResult, Errno> {
    let file = ctx.task.files.get(fd)?;
    let offset = offset.try_into().map_err(|_| EINVAL)?;
    Ok(file.read_at(&ctx.task, offset, &[UserBuffer { address, length }])?.into())
}

pub fn sys_pwrite64(
    ctx: &SyscallContext<'_>,
    fd: FdNumber,
    address: UserAddress,
    length: usize,
    offset: off_t,
) -> Result<SyscallResult, Errno> {
    let file = ctx.task.files.get(fd)?;
    let offset = offset.try_into().map_err(|_| EINVAL)?;
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

/// A convenient wrapper for Task::open_file_at.
///
/// Reads user_path from user memory and then calls through to Task::open_file_at.
fn open_file_at(
    task: &Task,
    dir_fd: FdNumber,
    user_path: UserCString,
    flags: u32,
    mode: FileMode,
) -> Result<FileHandle, Errno> {
    let mut buf = [0u8; PATH_MAX as usize];
    let path = task.mm.read_c_string(user_path, &mut buf)?;
    task.open_file_at(dir_fd, path, OpenFlags::from_bits_truncate(flags), mode)
}

fn lookup_parent_at<T, F>(
    task: &Task,
    dir_fd: FdNumber,
    user_path: UserCString,
    callback: F,
) -> Result<T, Errno>
where
    F: Fn(NamespaceNode, &FsStr) -> Result<T, Errno>,
{
    let mut buf = [0u8; PATH_MAX as usize];
    let path = task.mm.read_c_string(user_path, &mut buf)?;
    let (parent, basename) = task.lookup_parent_at(dir_fd, path)?;
    callback(parent, basename)
}

fn lookup_node_at(
    task: &Task,
    dir_fd: FdNumber,
    user_path: UserCString,
    symlink_follow_mode: SymlinkMode,
) -> Result<FsNodeHandle, Errno> {
    lookup_parent_at(task, dir_fd, user_path, |parent, basename| {
        Ok(parent.lookup(&task.fs, basename, symlink_follow_mode)?.node)
    })
}

pub fn sys_openat(
    ctx: &SyscallContext<'_>,
    dir_fd: FdNumber,
    user_path: UserCString,
    flags: u32,
    mode: FileMode,
) -> Result<SyscallResult, Errno> {
    let file = open_file_at(&ctx.task, dir_fd, user_path, flags, mode)?;
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

    let node = lookup_node_at(&ctx.task, dir_fd, user_path, SymlinkMode::NoFollow)?;

    if mode == F_OK {
        return Ok(SUCCESS);
    }

    // TODO(security): These access checks are not quite correct because
    // they don't consider the current uid and they don't consider GRO or
    // OTH bits. Really, these checks should be done by the auth system once
    // that exists.
    let stat = node.stat()?;
    if mode & X_OK != 0 && stat.st_mode & S_IXUSR == 0 {
        return Err(EACCES);
    }
    if mode & W_OK != 0 && stat.st_mode & S_IWUSR == 0 {
        return Err(EACCES);
    }
    if mode & R_OK != 0 && stat.st_mode & S_IRUSR == 0 {
        return Err(EACCES);
    }

    Ok(SUCCESS)
}

pub fn sys_getdents(
    ctx: &SyscallContext<'_>,
    fd: FdNumber,
    user_buffer: UserAddress,
    user_capacity: usize,
) -> Result<SyscallResult, Errno> {
    let file = ctx.task.files.get(fd)?;
    let mut sink = DirentSink32::new(ctx.task.clone(), user_buffer, user_capacity);
    file.readdir(&ctx.task, &mut sink)?;
    Ok(sink.actual().into())
}

pub fn sys_getdents64(
    ctx: &SyscallContext<'_>,
    fd: FdNumber,
    user_buffer: UserAddress,
    user_capacity: usize,
) -> Result<SyscallResult, Errno> {
    let file = ctx.task.files.get(fd)?;
    let mut sink = DirentSink64::new(ctx.task.clone(), user_buffer, user_capacity);
    file.readdir(&ctx.task, &mut sink)?;
    Ok(sink.actual().into())
}

pub fn sys_chdir(ctx: &SyscallContext<'_>, user_path: UserCString) -> Result<SyscallResult, Errno> {
    // TODO: This should probably use lookup_node.
    let file = open_file_at(
        &ctx.task,
        FdNumber::AT_FDCWD,
        user_path,
        O_RDONLY | O_DIRECTORY,
        FileMode::default(),
    )?;
    ctx.task.fs.chdir(&file);
    Ok(SUCCESS)
}

pub fn sys_fchdir(ctx: &SyscallContext<'_>, fd: FdNumber) -> Result<SyscallResult, Errno> {
    let file = ctx.task.files.get(fd)?;
    // TODO: Check isdir and return ENOTDIR.
    ctx.task.fs.chdir(&file);
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
    let result = file.node().stat()?;
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
    if flags & !AT_SYMLINK_NOFOLLOW != 0 {
        not_implemented!("newfstatat: flags 0x{:x}", flags);
        return Err(ENOSYS);
    }
    let node = if flags & AT_SYMLINK_NOFOLLOW != 0 {
        lookup_node_at(ctx.task, dir_fd, user_path, SymlinkMode::NoFollow)?
    } else {
        lookup_node_at(ctx.task, dir_fd, user_path, SymlinkMode::max_follow())?
    };
    let result = node.stat()?;
    ctx.task.mm.write_object(buffer, &result)?;
    Ok(SUCCESS)
}

pub fn sys_readlinkat(
    ctx: &SyscallContext<'_>,
    dir_fd: FdNumber,
    user_path: UserCString,
    buffer: UserAddress,
    buffer_size: usize,
) -> Result<SyscallResult, Errno> {
    let node = lookup_parent_at(ctx.task, dir_fd, user_path, |parent, basename| {
        let stat = parent.node.stat()?;
        // TODO(security): This check is obviously not correct, and should be updated once
        // we have an auth system.
        if stat.st_mode & S_IRUSR == 0 {
            return Err(EACCES);
        }
        Ok(parent.lookup(&ctx.task.fs, basename, SymlinkMode::NoFollow)?.node)
    })?;
    let link = node.readlink()?;

    // Cap the returned length at buffer_size.
    let length = std::cmp::min(buffer_size, link.len());
    ctx.task.mm.write_memory(buffer, &link[..length])?;

    Ok(length.into())
}

pub fn sys_readlink(
    ctx: &SyscallContext<'_>,
    user_path: UserCString,
    buffer: UserAddress,
    buffer_size: usize,
) -> Result<SyscallResult, Errno> {
    sys_readlinkat(ctx, FdNumber::AT_FDCWD, user_path, buffer, buffer_size)
}

pub fn sys_truncate(
    ctx: &SyscallContext<'_>,
    user_path: UserCString,
    length: off_t,
) -> Result<SyscallResult, Errno> {
    let length = length.try_into().map_err(|_| EINVAL)?;
    let node = lookup_node_at(&ctx.task, FdNumber::AT_FDCWD, user_path, SymlinkMode::max_follow())?;
    // TODO: Check for writability.
    node.truncate(length)?;
    Ok(SUCCESS)
}

pub fn sys_ftruncate(
    ctx: &SyscallContext<'_>,
    fd: FdNumber,
    length: off_t,
) -> Result<SyscallResult, Errno> {
    let length = length.try_into().map_err(|_| EINVAL)?;
    let file = ctx.task.files.get(fd)?;
    // TODO: Check for writability.
    file.node().truncate(length)?;
    Ok(SUCCESS)
}

pub fn sys_mkdirat(
    ctx: &SyscallContext<'_>,
    dir_fd: FdNumber,
    user_path: UserCString,
    mode: FileMode,
) -> Result<SyscallResult, Errno> {
    let mode = ctx.task.fs.apply_umask(mode & FileMode::ALLOW_ALL);
    lookup_parent_at(&ctx.task, dir_fd, user_path, |parent, basename| {
        parent.mknod(basename, FileMode::IFDIR | mode, DeviceType::NONE)
    })?;
    Ok(SUCCESS)
}

pub fn sys_mknodat(
    ctx: &SyscallContext<'_>,
    dir_fd: FdNumber,
    user_path: UserCString,
    mode: FileMode,
    dev: DeviceType,
) -> Result<SyscallResult, Errno> {
    let file_type = match mode & FileMode::IFMT {
        FileMode::IFREG => FileMode::IFREG,
        FileMode::IFBLK => FileMode::IFBLK,
        FileMode::IFIFO => FileMode::IFIFO,
        FileMode::IFSOCK => FileMode::IFSOCK,
        FileMode::EMPTY => FileMode::IFREG,
        _ => {
            return Err(EINVAL);
        }
    };
    let mode = file_type | ctx.task.fs.apply_umask(mode & FileMode::ALLOW_ALL);
    lookup_parent_at(&ctx.task, dir_fd, user_path, |parent, basename| {
        parent.mknod(basename, mode, dev)
    })?;
    Ok(SUCCESS)
}

pub fn sys_unlinkat(
    ctx: &SyscallContext<'_>,
    dir_fd: FdNumber,
    user_path: UserCString,
    flags: u32,
) -> Result<SyscallResult, Errno> {
    if flags & !AT_REMOVEDIR != 0 {
        return Err(EINVAL);
    }
    let kind =
        if flags & AT_REMOVEDIR != 0 { UnlinkKind::Directory } else { UnlinkKind::NonDirectory };
    lookup_parent_at(&ctx.task, dir_fd, user_path, |parent, basename| {
        parent.unlink(&ctx.task.fs, basename, kind)
    })?;
    Ok(SUCCESS)
}

pub fn sys_fchmod(
    ctx: &SyscallContext<'_>,
    dir_fd: FdNumber,
    mode: FileMode,
) -> Result<SyscallResult, Errno> {
    if mode & FileMode::IFMT != FileMode::EMPTY {
        return Err(EINVAL);
    }
    let file = ctx.task.files.get(dir_fd)?;
    file.name().node.info_write().mode = mode;
    Ok(SUCCESS)
}

pub fn sys_fchmodat(
    ctx: &SyscallContext<'_>,
    dir_fd: FdNumber,
    user_path: UserCString,
    mode: FileMode,
) -> Result<SyscallResult, Errno> {
    if mode & FileMode::IFMT != FileMode::EMPTY {
        return Err(EINVAL);
    }
    let node = lookup_node_at(&ctx.task, dir_fd, user_path, SymlinkMode::max_follow())?;
    node.info_write().mode = mode;
    Ok(SUCCESS)
}

pub fn sys_fchownat(
    _ctx: &SyscallContext<'_>,
    _dir_fd: FdNumber,
    _user_path: UserCString,
    _owner: u32,
    _group: u32,
    _flags: u32,
) -> Result<SyscallResult, Errno> {
    not_implemented!("Stubbed fchownat has no effect.");
    Ok(SUCCESS)
}

pub fn sys_getcwd(
    ctx: &SyscallContext<'_>,
    buf: UserAddress,
    size: usize,
) -> Result<SyscallResult, Errno> {
    let mut bytes = ctx.task.fs.cwd().path();
    bytes.push(b'\0');
    if bytes.len() > size {
        return Err(ERANGE);
    }
    ctx.task.mm.write_memory(buf, &bytes)?;
    return Ok(bytes.len().into());
}

pub fn sys_umask(ctx: &SyscallContext<'_>, umask: FileMode) -> Result<SyscallResult, Errno> {
    Ok(ctx.task.fs.set_umask(umask).bits().into())
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
    let supported_file_flags = OpenFlags::NONBLOCK | OpenFlags::DIRECT;
    if flags & !(O_CLOEXEC | supported_file_flags.bits()) != 0 {
        return Err(EINVAL);
    }
    let (read, write) = new_pipe(ctx.kernel());

    let file_flags = OpenFlags::from_bits_truncate(flags & supported_file_flags.bits());
    read.update_file_flags(file_flags, supported_file_flags);
    write.update_file_flags(file_flags, supported_file_flags);

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

pub fn sys_symlinkat(
    ctx: &SyscallContext<'_>,
    user_target: UserCString,
    new_dir_fd: FdNumber,
    user_path: UserCString,
) -> Result<SyscallResult, Errno> {
    let mut buf = [0u8; PATH_MAX as usize];
    let target = ctx.task.mm.read_c_string(user_target, &mut buf)?;
    if target.len() == 0 {
        return Err(ENOENT);
    }

    let mut buf = [0u8; PATH_MAX as usize];
    let path = ctx.task.mm.read_c_string(user_path, &mut buf)?;
    // TODO: This check could probably be moved into parent.symlink(..).
    if path.len() == 0 {
        return Err(ENOENT);
    }

    lookup_parent_at(ctx.task, new_dir_fd, user_path, |parent, basename| {
        let stat = parent.node.stat()?;
        if stat.st_mode & S_IWUSR == 0 {
            return Err(EACCES);
        }
        parent.symlink(basename, target)
    })?;
    Ok(SUCCESS)
}

pub fn sys_dup(ctx: &SyscallContext<'_>, oldfd: FdNumber) -> Result<SyscallResult, Errno> {
    let newfd = ctx.task.files.duplicate(oldfd, None, FdFlags::empty())?;
    Ok(newfd.into())
}

pub fn sys_dup3(
    ctx: &SyscallContext<'_>,
    oldfd: FdNumber,
    newfd: FdNumber,
    flags: u32,
) -> Result<SyscallResult, Errno> {
    if oldfd == newfd {
        return Err(EINVAL);
    }
    let valid_flags = FdFlags::from_bits(flags).ok_or(EINVAL)?;

    ctx.task.files.duplicate(oldfd, Some(newfd), valid_flags)?;
    Ok(newfd.into())
}

pub fn sys_memfd_create(
    ctx: &SyscallContext<'_>,
    _user_name: UserCString,
    flags: u32,
) -> Result<SyscallResult, Errno> {
    if flags & !MFD_CLOEXEC != 0 {
        not_implemented!("memfd_create: flags: {}", flags);
    }
    let file = new_memfd(ctx.kernel(), OpenFlags::RDWR)?;
    let mut fd_flags = FdFlags::empty();
    if flags & MFD_CLOEXEC != 0 {
        fd_flags |= FdFlags::CLOEXEC;
    }
    let fd = ctx.task.files.add_with_flags(file, fd_flags)?;
    Ok(fd.into())
}

#[cfg(test)]
mod tests {

    use super::*;
    use fuchsia_async as fasync;
    use std::sync::Arc;

    use crate::testing::*;

    #[fasync::run_singlethreaded(test)]
    async fn test_sys_lseek() -> Result<(), Errno> {
        let (_kernel, task_owner) = create_kernel_and_task_with_pkgfs();
        let ctx = SyscallContext::new(&task_owner.task);
        let fd = FdNumber::from_raw(10);
        let file_handle = task_owner.task.open_file(b"data/testfile.txt", OpenFlags::RDONLY)?;
        let file_size = file_handle.node().stat().unwrap().st_size;
        task_owner.task.files.insert(fd, file_handle);

        assert_eq!(sys_lseek(&ctx, fd, 0, SeekOrigin::CUR as u32)?, SyscallResult::Success(0));
        assert_eq!(sys_lseek(&ctx, fd, 1, SeekOrigin::CUR as u32)?, SyscallResult::Success(1));
        assert_eq!(sys_lseek(&ctx, fd, 3, SeekOrigin::SET as u32)?, SyscallResult::Success(3));
        assert_eq!(sys_lseek(&ctx, fd, -3, SeekOrigin::CUR as u32)?, SyscallResult::Success(0));
        assert_eq!(
            sys_lseek(&ctx, fd, 0, SeekOrigin::END as u32)?,
            SyscallResult::Success(file_size as u64)
        );
        assert_eq!(sys_lseek(&ctx, fd, -5, SeekOrigin::SET as u32), Err(EINVAL));

        // Make sure that the failed call above did not change the offset.
        assert_eq!(
            sys_lseek(&ctx, fd, 0, SeekOrigin::CUR as u32)?,
            SyscallResult::Success(file_size as u64)
        );

        // Prepare for an overflow.
        assert_eq!(sys_lseek(&ctx, fd, 3, SeekOrigin::SET as u32)?, SyscallResult::Success(3));

        // Check for overflow.
        assert_eq!(sys_lseek(&ctx, fd, i64::MAX, SeekOrigin::CUR as u32), Err(EINVAL));

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_sys_dup() -> Result<(), Errno> {
        let (_kernel, task_owner) = create_kernel_and_task_with_pkgfs();
        let ctx = SyscallContext::new(&task_owner.task);
        let file_handle = task_owner.task.open_file(b"data/testfile.txt", OpenFlags::RDONLY)?;
        let files = &task_owner.task.files;
        let oldfd = files.add(file_handle)?;
        let syscall_result = sys_dup(&ctx, oldfd)?;
        let newfd;
        if let SyscallResult::Success(value) = syscall_result {
            newfd = FdNumber::from_raw(value as i32);
        } else {
            return Err(EBADF);
        }

        assert_ne!(oldfd, newfd);
        assert!(Arc::ptr_eq(&files.get(oldfd).unwrap(), &files.get(newfd).unwrap()));

        assert_eq!(sys_dup(&ctx, FdNumber::from_raw(3)), Err(EBADF));

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_sys_dup3() -> Result<(), Errno> {
        let (_kernel, task_owner) = create_kernel_and_task_with_pkgfs();
        let ctx = SyscallContext::new(&task_owner.task);
        let file_handle = task_owner.task.open_file(b"data/testfile.txt", OpenFlags::RDONLY)?;
        let files = &task_owner.task.files;
        let oldfd = files.add(file_handle)?;
        let newfd = FdNumber::from_raw(2);
        sys_dup3(&ctx, oldfd, newfd, FdFlags::CLOEXEC.bits())?;

        assert_ne!(oldfd, newfd);
        assert!(Arc::ptr_eq(&files.get(oldfd).unwrap(), &files.get(newfd).unwrap()));
        assert_eq!(files.get_fd_flags(oldfd).unwrap(), FdFlags::empty());
        assert_eq!(files.get_fd_flags(newfd).unwrap(), FdFlags::CLOEXEC);

        assert_eq!(sys_dup3(&ctx, oldfd, oldfd, FdFlags::CLOEXEC.bits()), Err(EINVAL));

        // Pass invalid flags.
        let invalid_flags = 1234;
        assert_eq!(sys_dup3(&ctx, oldfd, newfd, invalid_flags), Err(EINVAL));

        // Makes sure that dup closes the old file handle before the fd points
        // to the new file handle.
        let second_file_handle =
            task_owner.task.open_file(b"data/testfile.txt", OpenFlags::RDONLY)?;
        let different_file_fd = files.add(second_file_handle)?;
        assert!(!Arc::ptr_eq(&files.get(oldfd).unwrap(), &files.get(different_file_fd).unwrap()));
        sys_dup3(&ctx, oldfd, different_file_fd, FdFlags::CLOEXEC.bits())?;
        assert!(Arc::ptr_eq(&files.get(oldfd).unwrap(), &files.get(different_file_fd).unwrap()));

        Ok(())
    }
}

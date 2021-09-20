// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::convert::TryInto;
use std::usize;

use crate::errno;
use crate::error;
use crate::fs::eventfd::*;
use crate::fs::fuchsia::*;
use crate::fs::memfd::*;
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
                return error!(EINVAL);
            }
            let file = ctx.task.files.get(fd)?;
            let task = ctx.task.get_task(arg.try_into().map_err(|_| errno!(EINVAL))?);
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
            error!(ENOSYS)
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
    let offset = offset.try_into().map_err(|_| errno!(EINVAL))?;
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
    let offset = offset.try_into().map_err(|_| errno!(EINVAL))?;
    Ok(file.write_at(&ctx.task, offset, &[UserBuffer { address, length }])?.into())
}

pub fn sys_readv(
    ctx: &SyscallContext<'_>,
    fd: FdNumber,
    iovec_addr: UserAddress,
    iovec_count: i32,
) -> Result<SyscallResult, Errno> {
    let file = ctx.task.files.get(fd)?;
    let iovec = ctx.task.mm.read_iovec(iovec_addr, iovec_count)?;
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
    fd: FdNumber,
    user_buf: UserRef<statfs>,
) -> Result<SyscallResult, Errno> {
    let file = ctx.task.files.get(fd)?;
    let stat = file.fs.statfs()?;
    ctx.task.mm.write_object(user_buf, &stat)?;
    Ok(SUCCESS)
}

pub fn sys_statfs(
    ctx: &SyscallContext<'_>,
    user_path: UserCString,
    user_buf: UserRef<statfs>,
) -> Result<SyscallResult, Errno> {
    let node = lookup_at(ctx.task, FdNumber::AT_FDCWD, user_path, LookupOptions::default())?;
    let file_system = node.entry.node.fs();
    let stat = file_system.statfs()?;
    ctx.task.mm.write_object(user_buf, &stat)?;

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
    strace!(task, "open_file_at(dir_fd={}, path={:?})", dir_fd, String::from_utf8_lossy(path));
    task.open_file_at(dir_fd, path, OpenFlags::from_bits_truncate(flags), mode)
}

pub fn lookup_parent_at<T, F>(
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
    strace!(task, "lookup_parent_at(dir_fd={}, path={:?})", dir_fd, String::from_utf8_lossy(path));
    if path.is_empty() {
        return error!(ENOENT);
    }
    let (parent, basename) = task.lookup_parent_at(dir_fd, path)?;
    callback(parent, basename)
}

/// Options for lookup_at.
struct LookupOptions {
    /// Whether AT_EMPTY_PATH was supplied.
    allow_empty_path: bool,

    /// Used to implement AT_SYMLINK_NOFOLLOW.
    symlink_mode: SymlinkMode,
}

impl Default for LookupOptions {
    fn default() -> Self {
        LookupOptions { allow_empty_path: false, symlink_mode: SymlinkMode::Follow }
    }
}

fn lookup_at(
    task: &Task,
    dir_fd: FdNumber,
    user_path: UserCString,
    options: LookupOptions,
) -> Result<NamespaceNode, Errno> {
    let mut buf = [0u8; PATH_MAX as usize];
    let path = task.mm.read_c_string(user_path, &mut buf)?;
    strace!(task, "lookup_at(dir_fd={}, path={:?})", dir_fd, String::from_utf8_lossy(path));
    if path.is_empty() {
        if options.allow_empty_path {
            let (node, _) = task.resolve_dir_fd(dir_fd, path)?;
            return Ok(node);
        }
        return error!(ENOENT);
    }
    let (parent, basename) = task.lookup_parent_at(dir_fd, path)?;
    let mut context = LookupContext::new(options.symlink_mode);
    parent.lookup_child(&mut context, task, basename)
}

pub fn sys_openat(
    ctx: &SyscallContext<'_>,
    dir_fd: FdNumber,
    user_path: UserCString,
    flags: u32,
    mode: FileMode,
) -> Result<SyscallResult, Errno> {
    let file = open_file_at(&ctx.task, dir_fd, user_path, flags, mode)?;
    let fd_flags = get_fd_flags(flags);
    Ok(ctx.task.files.add_with_flags(file, fd_flags)?.into())
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
        return error!(EINVAL);
    }

    let name = lookup_at(
        &ctx.task,
        dir_fd,
        user_path,
        LookupOptions { allow_empty_path: false, symlink_mode: SymlinkMode::NoFollow },
    )?;
    let node = &name.entry.node;

    if mode == F_OK {
        return Ok(SUCCESS);
    }

    // TODO(security): These access checks are not quite correct because
    // they don't consider the current uid and they don't consider GRO or
    // OTH bits. Really, these checks should be done by the auth system once
    // that exists.
    let stat = node.stat()?;
    if mode & X_OK != 0 && stat.st_mode & S_IXUSR == 0 {
        return error!(EACCES);
    }
    if mode & W_OK != 0 && stat.st_mode & S_IWUSR == 0 {
        return error!(EACCES);
    }
    if mode & R_OK != 0 && stat.st_mode & S_IRUSR == 0 {
        return error!(EACCES);
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
    let name = lookup_at(ctx.task, FdNumber::AT_FDCWD, user_path, LookupOptions::default())?;
    if !name.entry.node.is_dir() {
        return error!(ENOTDIR);
    }
    ctx.task.fs.chdir(name);
    Ok(SUCCESS)
}

pub fn sys_fchdir(ctx: &SyscallContext<'_>, fd: FdNumber) -> Result<SyscallResult, Errno> {
    let file = ctx.task.files.get(fd)?;
    if !file.name.entry.node.is_dir() {
        return error!(ENOTDIR);
    }
    ctx.task.fs.chdir(file.name.clone());
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
    if flags & !(AT_SYMLINK_NOFOLLOW | AT_EMPTY_PATH) != 0 {
        not_implemented!("newfstatat: flags 0x{:x}", flags);
        return error!(ENOSYS);
    }
    let options = LookupOptions {
        allow_empty_path: flags & AT_EMPTY_PATH != 0,
        symlink_mode: if flags & AT_SYMLINK_NOFOLLOW != 0 {
            SymlinkMode::NoFollow
        } else {
            SymlinkMode::Follow
        },
    };
    let name = lookup_at(ctx.task, dir_fd, user_path, options)?;
    let result = name.entry.node.stat()?;
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
    let entry = lookup_parent_at(ctx.task, dir_fd, user_path, |parent, basename| {
        let stat = parent.entry.node.stat()?;
        // TODO(security): This check is obviously not correct, and should be updated once
        // we have an auth system.
        if stat.st_mode & S_IRUSR == 0 {
            return error!(EACCES);
        }
        let mut context = LookupContext::new(SymlinkMode::NoFollow);
        Ok(parent.lookup_child(&mut context, ctx.task, basename)?.entry)
    })?;

    let target = match entry.node.readlink(ctx.task)? {
        SymlinkTarget::Path(path) => path,
        SymlinkTarget::Node(node) => node.path(),
    };

    // Cap the returned length at buffer_size.
    let length = std::cmp::min(buffer_size, target.len());
    ctx.task.mm.write_memory(buffer, &target[..length])?;
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
    let length = length.try_into().map_err(|_| errno!(EINVAL))?;
    let name = lookup_at(&ctx.task, FdNumber::AT_FDCWD, user_path, LookupOptions::default())?;
    // TODO: Check for writability.
    name.entry.node.truncate(length)?;
    Ok(SUCCESS)
}

pub fn sys_ftruncate(
    ctx: &SyscallContext<'_>,
    fd: FdNumber,
    length: off_t,
) -> Result<SyscallResult, Errno> {
    let length = length.try_into().map_err(|_| errno!(EINVAL))?;
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
        parent.create_node(basename, FileMode::IFDIR | mode, DeviceType::NONE)
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
        FileMode::IFREG
        | FileMode::IFCHR
        | FileMode::IFBLK
        | FileMode::IFIFO
        | FileMode::IFSOCK => mode & FileMode::IFMT,
        FileMode::EMPTY => FileMode::IFREG,
        _ => return error!(EINVAL),
    };
    let mode = file_type | ctx.task.fs.apply_umask(mode & FileMode::ALLOW_ALL);
    lookup_parent_at(&ctx.task, dir_fd, user_path, |parent, basename| {
        parent.create_node(basename, mode, dev)
    })?;
    Ok(SUCCESS)
}

pub fn sys_linkat(
    ctx: &SyscallContext<'_>,
    old_dir_fd: FdNumber,
    old_user_path: UserCString,
    new_dir_fd: FdNumber,
    new_user_path: UserCString,
    flags: u32,
) -> Result<SyscallResult, Errno> {
    if flags & !(AT_SYMLINK_FOLLOW | AT_EMPTY_PATH) != 0 {
        not_implemented!("linkat: flags 0x{:x}", flags);
        return error!(EINVAL);
    }

    // TODO: AT_EMPTY_PATH requires CAP_DAC_READ_SEARCH.
    let options = LookupOptions {
        allow_empty_path: flags & AT_EMPTY_PATH != 0,
        symlink_mode: if flags & AT_SYMLINK_FOLLOW != 0 {
            SymlinkMode::Follow
        } else {
            SymlinkMode::NoFollow
        },
    };
    let target = lookup_at(ctx.task, old_dir_fd, old_user_path, options)?;
    if target.entry.node.is_dir() {
        return error!(EPERM);
    }
    lookup_parent_at(ctx.task, new_dir_fd, new_user_path, |parent, basename| {
        if !NamespaceNode::mount_eq(&target, &parent) {
            return error!(EXDEV);
        }
        parent.entry.link(basename, &target.entry.node)
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
        return error!(EINVAL);
    }
    let kind =
        if flags & AT_REMOVEDIR != 0 { UnlinkKind::Directory } else { UnlinkKind::NonDirectory };
    lookup_parent_at(&ctx.task, dir_fd, user_path, |parent, basename| {
        parent.unlink(basename, kind)
    })?;
    Ok(SUCCESS)
}

pub fn sys_renameat(
    ctx: &SyscallContext<'_>,
    old_dir_fd: FdNumber,
    old_user_path: UserCString,
    new_dir_fd: FdNumber,
    new_user_path: UserCString,
) -> Result<SyscallResult, Errno> {
    let lookup = |dir_fd, user_path| {
        lookup_parent_at(ctx.task, dir_fd, user_path, |parent, basename| {
            Ok((parent, basename.to_vec()))
        })
    };

    let (old_parent, old_basename) = lookup(old_dir_fd, old_user_path)?;
    let (new_parent, new_basename) = lookup(new_dir_fd, new_user_path)?;

    if !NamespaceNode::mount_eq(&old_parent, &new_parent) {
        return error!(EXDEV);
    }

    DirEntry::rename(&old_parent, &old_basename, &new_parent, &new_basename)?;
    Ok(SUCCESS)
}

pub fn sys_fchmod(
    ctx: &SyscallContext<'_>,
    fd: FdNumber,
    mode: FileMode,
) -> Result<SyscallResult, Errno> {
    if mode & FileMode::IFMT != FileMode::EMPTY {
        return error!(EINVAL);
    }
    let file = ctx.task.files.get(fd)?;
    file.name.entry.node.chmod(mode);
    Ok(SUCCESS)
}

pub fn sys_fchmodat(
    ctx: &SyscallContext<'_>,
    dir_fd: FdNumber,
    user_path: UserCString,
    mode: FileMode,
) -> Result<SyscallResult, Errno> {
    if mode & FileMode::IFMT != FileMode::EMPTY {
        return error!(EINVAL);
    }
    let name = lookup_at(&ctx.task, dir_fd, user_path, LookupOptions::default())?;
    name.entry.node.chmod(mode);
    Ok(SUCCESS)
}

fn maybe_uid(id: u32) -> Option<uid_t> {
    if id == u32::MAX {
        None
    } else {
        Some(id)
    }
}

pub fn sys_fchown(
    ctx: &SyscallContext<'_>,
    fd: FdNumber,
    owner: u32,
    group: u32,
) -> Result<SyscallResult, Errno> {
    let file = ctx.task.files.get(fd)?;
    // TODO(security): Needs permission check
    file.name.entry.node.chown(maybe_uid(owner), maybe_uid(group));
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

pub fn sys_fsetxattr(
    ctx: &SyscallContext<'_>,
    fd: FdNumber,
    name_addr: UserCString,
    value_addr: UserAddress,
    size: usize,
    flags: u32,
) -> Result<SyscallResult, Errno> {
    let op = match flags {
        0 => XattrOp::Set,
        XATTR_CREATE => XattrOp::Create,
        XATTR_REPLACE => XattrOp::Replace,
        _ => return error!(EINVAL),
    };
    let file = ctx.task.files.get(fd)?;
    let mut name = vec![0u8; XATTR_NAME_MAX as usize];
    let name = ctx.task.mm.read_c_string(name_addr, &mut name).map_err(|e| {
        if e == ENAMETOOLONG {
            errno!(ERANGE)
        } else {
            e
        }
    })?;
    if size > XATTR_SIZE_MAX as usize {
        return error!(ERANGE);
    }
    let mut value = vec![0u8; size];
    ctx.task.mm.read_memory(value_addr, &mut value)?;
    file.name.entry.node.set_xattr(name, &value, op)?;
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
        return error!(ERANGE);
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
        return error!(EINVAL);
    }
    let (read, write) = new_pipe(ctx.kernel())?;

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
        return error!(ENOENT);
    }

    let mut buf = [0u8; PATH_MAX as usize];
    let path = ctx.task.mm.read_c_string(user_path, &mut buf)?;
    // TODO: This check could probably be moved into parent.symlink(..).
    if path.len() == 0 {
        return error!(ENOENT);
    }

    lookup_parent_at(ctx.task, new_dir_fd, user_path, |parent, basename| {
        let stat = parent.entry.node.stat()?;
        if stat.st_mode & S_IWUSR == 0 {
            return error!(EACCES);
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
        return error!(EINVAL);
    }
    if flags & !O_CLOEXEC != 0 {
        return error!(EINVAL);
    }
    let fd_flags = get_fd_flags(flags);
    ctx.task.files.duplicate(oldfd, Some(newfd), fd_flags)?;
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

pub fn sys_mount(
    ctx: &SyscallContext<'_>,
    source_addr: UserCString,
    target_addr: UserCString,
    filesystemtype_addr: UserCString,
    _flags: u64,
    _data_addr: UserAddress,
) -> Result<SyscallResult, Errno> {
    let mut buf = [0u8; PATH_MAX as usize];
    let source = ctx.task.mm.read_c_string(source_addr, &mut buf)?;
    let mut buf = [0u8; PATH_MAX as usize];
    let target = ctx.task.mm.read_c_string(target_addr, &mut buf)?;
    let mut buf = [0u8; PATH_MAX as usize];
    let fs_type = ctx.task.mm.read_c_string(filesystemtype_addr, &mut buf)?;
    strace!(
        ctx.task,
        "mount(source={:?}, target={:?}, type={:?})",
        String::from_utf8_lossy(source),
        String::from_utf8_lossy(target),
        String::from_utf8_lossy(fs_type)
    );

    let fs = create_filesystem(ctx.kernel(), source, fs_type, b"")?;
    ctx.task.lookup_path_from_root(target)?.mount(fs)?;
    Ok(SUCCESS)
}

pub fn sys_eventfd(ctx: &SyscallContext<'_>, value: u32) -> Result<SyscallResult, Errno> {
    sys_eventfd2(ctx, value, 0)
}

pub fn sys_eventfd2(
    ctx: &SyscallContext<'_>,
    value: u32,
    flags: u32,
) -> Result<SyscallResult, Errno> {
    if flags & !(O_CLOEXEC | O_NONBLOCK) != 0 {
        // TODO: Implement EFD_SEMAPHORE.
        not_implemented!("eventfd2: flags 0x{:x}", flags);
        return error!(EINVAL);
    }
    let mut open_flags = OpenFlags::empty();
    if flags & O_NONBLOCK != 0 {
        open_flags |= OpenFlags::NONBLOCK;
    }
    let file = new_eventfd(ctx.kernel(), value, open_flags);
    let fd_flags = get_fd_flags(flags);
    let fd = ctx.task.files.add_with_flags(file, fd_flags)?;
    Ok(fd.into())
}

pub fn sys_timerfd_create(
    ctx: &SyscallContext<'_>,
    clock_id: u32,
    flags: u32,
) -> Result<SyscallResult, Errno> {
    match clock_id {
        CLOCK_MONOTONIC | CLOCK_BOOTTIME => {}
        CLOCK_REALTIME => return error!(ENOSYS),
        _ => return error!(EINVAL),
    };
    if flags & !(TFD_NONBLOCK | TFD_CLOEXEC) != 0 {
        not_implemented!("timerfd_create: flags 0x{:x}", flags);
        return error!(EINVAL);
    }

    let mut open_flags = OpenFlags::RDWR;
    if flags & TFD_NONBLOCK != 0 {
        open_flags |= OpenFlags::NONBLOCK;
    }

    let mut fd_flags = FdFlags::empty();
    if flags & TFD_CLOEXEC != 0 {
        fd_flags |= FdFlags::CLOEXEC;
    };

    let timer = TimerFile::new(ctx.kernel(), open_flags)?;
    let fd = ctx.task.files.add_with_flags(timer, fd_flags)?;
    Ok(fd.into())
}

pub fn sys_timerfd_gettime<'a>(
    ctx: &SyscallContext<'_>,
    fd: FdNumber,
    user_current_value: UserRef<itimerspec>,
) -> Result<SyscallResult, Errno> {
    let file = ctx.task.files.get(fd)?;
    let timer_file = file.downcast_file::<TimerFile>().ok_or_else(|| errno!(EBADF))?;
    let timer_info = timer_file.current_timer_spec();
    ctx.task.mm.write_object(user_current_value, &timer_info)?;

    Ok(SUCCESS)
}

pub fn sys_timerfd_settime(
    ctx: &SyscallContext<'_>,
    fd: FdNumber,
    flags: u32,
    user_new_value: UserRef<itimerspec>,
    user_old_value: UserRef<itimerspec>,
) -> Result<SyscallResult, Errno> {
    if flags & !(TFD_TIMER_ABSTIME) != 0 {
        not_implemented!("timerfd_settime: flags 0x{:x}", flags);
        return error!(EINVAL);
    }

    let file = ctx.task.files.get(fd)?;
    let timer_file = file.downcast_file::<TimerFile>().ok_or_else(|| errno!(EBADF))?;

    let mut new_timer_spec = itimerspec::default();
    ctx.task.mm.read_object(user_new_value, &mut new_timer_spec)?;

    let old_timer_spec = timer_file.set_timer_spec(new_timer_spec, flags)?;
    if !user_old_value.is_null() {
        ctx.task.mm.write_object(user_old_value, &old_timer_spec)?;
    }

    Ok(SUCCESS)
}

pub fn sys_epoll_create(ctx: &SyscallContext<'_>, size: i32) -> Result<SyscallResult, Errno> {
    if size < 1 {
        return error!(EINVAL);
    }
    sys_epoll_create1(ctx, 0)
}

pub fn sys_epoll_create1(ctx: &SyscallContext<'_>, flags: u32) -> Result<SyscallResult, Errno> {
    not_implemented!("epoll_create1 not implemented");
    if flags & !EPOLL_CLOEXEC != 0 {
        return Err(EINVAL);
    }
    let ep_file = EpollFileObject::new(ctx.kernel());
    let fd_flags = if flags & EPOLL_CLOEXEC != 0 { FdFlags::CLOEXEC } else { FdFlags::empty() };
    let fd = ctx.task.files.add_with_flags(ep_file, fd_flags)?;
    Ok(fd.into())
}

pub fn sys_epoll_ctl(
    _ctx: &SyscallContext<'_>,
    _epfd: FdNumber,
    op: u32,
    _fd: FdNumber,
    _event: UserRef<epoll_event>,
) -> Result<SyscallResult, Errno> {
    not_implemented!("epoll_ctl not implemented");
    match op {
        EPOLL_CTL_ADD => Ok(SUCCESS),
        EPOLL_CTL_MOD => Ok(SUCCESS),
        EPOLL_CTL_DEL => Ok(SUCCESS),
        _ => Err(EINVAL),
    }
}

pub fn sys_epoll_wait(
    ctx: &SyscallContext<'_>,
    epfd: FdNumber,
    events: UserRef<epoll_event>,
    max_events: i32,
    timeout: i32,
) -> Result<SyscallResult, Errno> {
    sys_epoll_pwait(ctx, epfd, events, max_events, timeout, UserRef::<sigset_t>::default())
}

pub fn sys_epoll_pwait(
    _ctx: &SyscallContext<'_>,
    _epfd: FdNumber,
    _events: UserRef<epoll_event>,
    _max_events: i32,
    _timeout: i32,
    _sigmask: UserRef<sigset_t>,
) -> Result<SyscallResult, Errno> {
    not_implemented!("epoll_pwait not implemented");
    Err(ENOSYS)
}

pub fn sys_flock(
    _ctx: &SyscallContext<'_>,
    _fd: FdNumber,
    _operation: i32,
) -> Result<SyscallResult, Errno> {
    not_implemented!("flock not implemented");
    Ok(SUCCESS)
}

#[cfg(test)]
mod tests {

    use super::*;
    use fuchsia_async as fasync;
    use std::sync::Arc;

    use crate::mm::PAGE_SIZE;
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
        assert_eq!(sys_lseek(&ctx, fd, -5, SeekOrigin::SET as u32), error!(EINVAL));

        // Make sure that the failed call above did not change the offset.
        assert_eq!(
            sys_lseek(&ctx, fd, 0, SeekOrigin::CUR as u32)?,
            SyscallResult::Success(file_size as u64)
        );

        // Prepare for an overflow.
        assert_eq!(sys_lseek(&ctx, fd, 3, SeekOrigin::SET as u32)?, SyscallResult::Success(3));

        // Check for overflow.
        assert_eq!(sys_lseek(&ctx, fd, i64::MAX, SeekOrigin::CUR as u32), error!(EINVAL));

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
            return error!(EBADF);
        }

        assert_ne!(oldfd, newfd);
        assert!(Arc::ptr_eq(&files.get(oldfd).unwrap(), &files.get(newfd).unwrap()));

        assert_eq!(sys_dup(&ctx, FdNumber::from_raw(3)), error!(EBADF));

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
        sys_dup3(&ctx, oldfd, newfd, O_CLOEXEC)?;

        assert_ne!(oldfd, newfd);
        assert!(Arc::ptr_eq(&files.get(oldfd).unwrap(), &files.get(newfd).unwrap()));
        assert_eq!(files.get_fd_flags(oldfd).unwrap(), FdFlags::empty());
        assert_eq!(files.get_fd_flags(newfd).unwrap(), FdFlags::CLOEXEC);

        assert_eq!(sys_dup3(&ctx, oldfd, oldfd, O_CLOEXEC), error!(EINVAL));

        // Pass invalid flags.
        let invalid_flags = 1234;
        assert_eq!(sys_dup3(&ctx, oldfd, newfd, invalid_flags), error!(EINVAL));

        // Makes sure that dup closes the old file handle before the fd points
        // to the new file handle.
        let second_file_handle =
            task_owner.task.open_file(b"data/testfile.txt", OpenFlags::RDONLY)?;
        let different_file_fd = files.add(second_file_handle)?;
        assert!(!Arc::ptr_eq(&files.get(oldfd).unwrap(), &files.get(different_file_fd).unwrap()));
        sys_dup3(&ctx, oldfd, different_file_fd, O_CLOEXEC)?;
        assert!(Arc::ptr_eq(&files.get(oldfd).unwrap(), &files.get(different_file_fd).unwrap()));

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_sys_open_cloexec() -> Result<(), Errno> {
        let (_kernel, task_owner) = create_kernel_and_task_with_pkgfs();
        let ctx = SyscallContext::new(&task_owner.task);
        let path_addr = map_memory(&ctx, UserAddress::default(), *PAGE_SIZE);
        let path = b"data/testfile.txt\0";
        ctx.task.mm.write_memory(path_addr, path)?;
        match sys_openat(
            &ctx,
            FdNumber::AT_FDCWD,
            UserCString::new(path_addr),
            O_RDONLY | O_CLOEXEC,
            FileMode::default(),
        )? {
            SyscallResult::Success(raw_fd) => {
                let fd = FdNumber::from_raw(raw_fd as i32);
                assert!(ctx.task.files.get_fd_flags(fd)?.contains(FdFlags::CLOEXEC));
            }
            _ => {
                assert!(false);
            }
        }
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_sys_epoll() -> Result<(), Errno> {
        let (_kernel, task_owner) = create_kernel_and_task_with_pkgfs();
        let ctx = SyscallContext::new(&task_owner.task);

        if let SyscallResult::Success(epoll_fd) =
            sys_epoll_create1(&ctx, 0).expect("sys_epoll_create1 failed")
        {
            sys_close(&ctx, FdNumber::from_raw(epoll_fd as i32)).expect("sys_close failed");
        } else {
            panic!("unexpected result from sys_epoll_create1");
        }

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_fstat_tmp_file() {
        let (_kernel, task_owner) = create_kernel_and_task_with_pkgfs();
        let ctx = SyscallContext::new(&task_owner.task);

        // Create the file that will be used to stat.
        let file_path = b"data/testfile.txt";
        let _file_handle = task_owner.task.open_file(file_path, OpenFlags::RDONLY).unwrap();

        // Write the path to user memory.
        let path_addr = map_memory(&ctx, UserAddress::default(), *PAGE_SIZE);
        ctx.task.mm.write_memory(path_addr, file_path).expect("failed to clear struct");

        let stat = statfs::default();
        let user_stat = UserRef::new(path_addr + file_path.len());
        ctx.task.mm.write_object(user_stat, &stat).expect("failed to clear struct");

        let user_path = UserCString::new(path_addr);

        assert_eq!(sys_statfs(&ctx, user_path, user_stat), Ok(SUCCESS));

        let mut returned_stat = statfs::default();
        ctx.task.mm.read_object(user_stat, &mut returned_stat).expect("failed to read struct");
        assert_eq!(returned_stat, statfs::default());
    }
}

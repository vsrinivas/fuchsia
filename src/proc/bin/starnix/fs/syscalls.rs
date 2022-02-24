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
use fuchsia_zircon as zx;

pub fn sys_read(
    current_task: &CurrentTask,
    fd: FdNumber,
    address: UserAddress,
    length: usize,
) -> Result<usize, Errno> {
    let file = current_task.files.get(fd)?;
    file.read(&current_task, &[UserBuffer { address, length }])
}

pub fn sys_write(
    current_task: &CurrentTask,
    fd: FdNumber,
    address: UserAddress,
    length: usize,
) -> Result<usize, Errno> {
    let file = current_task.files.get(fd)?;
    file.write(&current_task, &[UserBuffer { address, length }])
}

pub fn sys_close(current_task: &CurrentTask, fd: FdNumber) -> Result<SyscallResult, Errno> {
    current_task.files.close(fd)?;
    Ok(SUCCESS)
}

pub fn sys_lseek(
    current_task: &CurrentTask,
    fd: FdNumber,
    offset: off_t,
    whence: u32,
) -> Result<off_t, Errno> {
    let file = current_task.files.get(fd)?;
    file.seek(&current_task, offset, SeekOrigin::from_raw(whence)?)
}

pub fn sys_fcntl(
    current_task: &CurrentTask,
    fd: FdNumber,
    cmd: u32,
    arg: u64,
) -> Result<SyscallResult, Errno> {
    match cmd {
        F_DUPFD => {
            let newfd = current_task.files.duplicate(fd, None, FdFlags::empty())?;
            Ok(newfd.into())
        }
        F_DUPFD_CLOEXEC => {
            let newfd = current_task.files.duplicate(fd, None, FdFlags::CLOEXEC)?;
            Ok(newfd.into())
        }
        F_GETOWN => {
            let file = current_task.files.get(fd)?;
            Ok(file.get_async_owner().into())
        }
        F_SETOWN => {
            if arg > std::i32::MAX as u64 {
                // Negative values are process groups.
                not_implemented!("fcntl(F_SETOWN) does not support process groups");
                return error!(EINVAL);
            }
            let file = current_task.files.get(fd)?;
            let task = current_task.get_task(arg.try_into().map_err(|_| errno!(EINVAL))?);
            file.set_async_owner(task.map_or(0, |task| task.id));
            Ok(SUCCESS)
        }
        F_GETFD => Ok(current_task.files.get_fd_flags(fd)?.into()),
        F_SETFD => {
            current_task.files.set_fd_flags(fd, FdFlags::from_bits_truncate(arg as u32))?;
            Ok(SUCCESS)
        }
        F_GETFL => {
            let file = current_task.files.get(fd)?;
            Ok(file.flags().into())
        }
        F_SETFL => {
            // TODO: Add O_ASYNC once we have a decl for it.
            let settable_flags =
                OpenFlags::APPEND | OpenFlags::DIRECT | OpenFlags::NOATIME | OpenFlags::NONBLOCK;
            let requested_flags =
                OpenFlags::from_bits_truncate((arg as u32) & settable_flags.bits());
            let file = current_task.files.get(fd)?;
            file.update_file_flags(requested_flags, settable_flags);
            Ok(SUCCESS)
        }
        F_GETPIPE_SZ | F_SETPIPE_SZ => {
            let file = current_task.files.get(fd)?;
            file.fcntl(&current_task, cmd, arg)
        }
        _ => {
            not_implemented!("fcntl command {} not implemented", cmd);
            error!(ENOSYS)
        }
    }
}

pub fn sys_pread64(
    current_task: &CurrentTask,
    fd: FdNumber,
    address: UserAddress,
    length: usize,
    offset: off_t,
) -> Result<usize, Errno> {
    let file = current_task.files.get(fd)?;
    let offset = offset.try_into().map_err(|_| errno!(EINVAL))?;
    Ok(file.read_at(&current_task, offset, &[UserBuffer { address, length }])?)
}

pub fn sys_pwrite64(
    current_task: &CurrentTask,
    fd: FdNumber,
    address: UserAddress,
    length: usize,
    offset: off_t,
) -> Result<usize, Errno> {
    let file = current_task.files.get(fd)?;
    let offset = offset.try_into().map_err(|_| errno!(EINVAL))?;
    Ok(file.write_at(&current_task, offset, &[UserBuffer { address, length }])?)
}

pub fn sys_readv(
    current_task: &CurrentTask,
    fd: FdNumber,
    iovec_addr: UserAddress,
    iovec_count: i32,
) -> Result<usize, Errno> {
    let file = current_task.files.get(fd)?;
    let iovec = current_task.mm.read_iovec(iovec_addr, iovec_count)?;
    Ok(file.read(&current_task, &iovec)?)
}

pub fn sys_writev(
    current_task: &CurrentTask,
    fd: FdNumber,
    iovec_addr: UserAddress,
    iovec_count: i32,
) -> Result<usize, Errno> {
    let iovec = current_task.mm.read_iovec(iovec_addr, iovec_count)?;
    let file = current_task.files.get(fd)?;
    Ok(file.write(&current_task, &iovec)?)
}

pub fn sys_fstatfs(
    current_task: &CurrentTask,
    fd: FdNumber,
    user_buf: UserRef<statfs>,
) -> Result<SyscallResult, Errno> {
    let file = current_task.files.get(fd)?;
    let stat = file.fs.statfs()?;
    current_task.mm.write_object(user_buf, &stat)?;
    Ok(SUCCESS)
}

pub fn sys_statfs(
    current_task: &CurrentTask,
    user_path: UserCString,
    user_buf: UserRef<statfs>,
) -> Result<SyscallResult, Errno> {
    let node = lookup_at(&current_task, FdNumber::AT_FDCWD, user_path, LookupFlags::default())?;
    let file_system = node.entry.node.fs();
    let stat = file_system.statfs()?;
    current_task.mm.write_object(user_buf, &stat)?;

    Ok(SUCCESS)
}

/// A convenient wrapper for Task::open_file_at.
///
/// Reads user_path from user memory and then calls through to Task::open_file_at.
fn open_file_at(
    current_task: &CurrentTask,
    dir_fd: FdNumber,
    user_path: UserCString,
    flags: u32,
    mode: FileMode,
) -> Result<FileHandle, Errno> {
    let mut buf = [0u8; PATH_MAX as usize];
    let path = current_task.mm.read_c_string(user_path, &mut buf)?;
    strace!(
        current_task,
        "open_file_at(dir_fd={}, path={:?})",
        dir_fd,
        String::from_utf8_lossy(path)
    );
    current_task.open_file_at(dir_fd, path, OpenFlags::from_bits_truncate(flags), mode)
}

fn lookup_parent_at<T, F>(
    current_task: &CurrentTask,
    dir_fd: FdNumber,
    user_path: UserCString,
    callback: F,
) -> Result<T, Errno>
where
    F: Fn(NamespaceNode, &FsStr) -> Result<T, Errno>,
{
    let mut buf = [0u8; PATH_MAX as usize];
    let path = current_task.mm.read_c_string(user_path, &mut buf)?;
    strace!(
        current_task,
        "lookup_parent_at(dir_fd={}, path={:?})",
        dir_fd,
        String::from_utf8_lossy(path)
    );
    if path.is_empty() {
        return error!(ENOENT);
    }
    let (parent, basename) = current_task.lookup_parent_at(dir_fd, path)?;
    callback(parent, basename)
}

/// Options for lookup_at.
struct LookupFlags {
    /// Whether AT_EMPTY_PATH was supplied.
    allow_empty_path: bool,

    /// Used to implement AT_SYMLINK_NOFOLLOW.
    symlink_mode: SymlinkMode,
}

impl Default for LookupFlags {
    fn default() -> Self {
        LookupFlags { allow_empty_path: false, symlink_mode: SymlinkMode::Follow }
    }
}

impl LookupFlags {
    fn from_bits(flags: u32, allowed_flags: u32) -> Result<Self, Errno> {
        if flags & !allowed_flags != 0 {
            return error!(EINVAL);
        }
        let follow_symlinks = if allowed_flags & AT_SYMLINK_FOLLOW != 0 {
            flags & AT_SYMLINK_FOLLOW != 0
        } else {
            flags & AT_SYMLINK_NOFOLLOW == 0
        };
        Ok(LookupFlags {
            allow_empty_path: flags & AT_EMPTY_PATH != 0,
            symlink_mode: if follow_symlinks { SymlinkMode::Follow } else { SymlinkMode::NoFollow },
        })
    }
}

fn lookup_at(
    current_task: &CurrentTask,
    dir_fd: FdNumber,
    user_path: UserCString,
    options: LookupFlags,
) -> Result<NamespaceNode, Errno> {
    let mut buf = [0u8; PATH_MAX as usize];
    let path = current_task.mm.read_c_string(user_path, &mut buf)?;
    strace!(current_task, "lookup_at(dir_fd={}, path={:?})", dir_fd, String::from_utf8_lossy(path));
    if path.is_empty() {
        if options.allow_empty_path {
            let (node, _) = current_task.resolve_dir_fd(dir_fd, path)?;
            return Ok(node);
        }
        return error!(ENOENT);
    }
    let (parent, basename) = current_task.lookup_parent_at(dir_fd, path)?;
    let mut context = LookupContext::new(options.symlink_mode);
    parent.lookup_child(current_task, &mut context, basename)
}

pub fn sys_open(
    current_task: &CurrentTask,
    user_path: UserCString,
    flags: u32,
    mode: FileMode,
) -> Result<FdNumber, Errno> {
    sys_openat(current_task, FdNumber::AT_FDCWD, user_path, flags, mode)
}

pub fn sys_openat(
    current_task: &CurrentTask,
    dir_fd: FdNumber,
    user_path: UserCString,
    flags: u32,
    mode: FileMode,
) -> Result<FdNumber, Errno> {
    let file = open_file_at(&current_task, dir_fd, user_path, flags, mode)?;
    let fd_flags = get_fd_flags(flags);
    current_task.files.add_with_flags(file, fd_flags)
}

pub fn sys_faccessat(
    current_task: &CurrentTask,
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
        current_task,
        dir_fd,
        user_path,
        LookupFlags { symlink_mode: SymlinkMode::NoFollow, ..Default::default() },
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
    current_task: &CurrentTask,
    fd: FdNumber,
    user_buffer: UserAddress,
    user_capacity: usize,
) -> Result<usize, Errno> {
    let file = current_task.files.get(fd)?;
    let mut sink = DirentSink32::new(current_task, user_buffer, user_capacity);
    file.readdir(&current_task, &mut sink)?;
    Ok(sink.actual())
}

pub fn sys_getdents64(
    current_task: &CurrentTask,
    fd: FdNumber,
    user_buffer: UserAddress,
    user_capacity: usize,
) -> Result<usize, Errno> {
    let file = current_task.files.get(fd)?;
    let mut sink = DirentSink64::new(current_task, user_buffer, user_capacity);
    file.readdir(&current_task, &mut sink)?;
    Ok(sink.actual())
}

pub fn sys_chdir(
    current_task: &CurrentTask,
    user_path: UserCString,
) -> Result<SyscallResult, Errno> {
    let name = lookup_at(current_task, FdNumber::AT_FDCWD, user_path, LookupFlags::default())?;
    if !name.entry.node.is_dir() {
        return error!(ENOTDIR);
    }
    current_task.fs.chdir(name);
    Ok(SUCCESS)
}

pub fn sys_fchdir(current_task: &CurrentTask, fd: FdNumber) -> Result<SyscallResult, Errno> {
    let file = current_task.files.get(fd)?;
    if !file.name.entry.node.is_dir() {
        return error!(ENOTDIR);
    }
    current_task.fs.chdir(file.name.clone());
    Ok(SUCCESS)
}

pub fn sys_access(
    current_task: &CurrentTask,
    user_path: UserCString,
    mode: u32,
) -> Result<SyscallResult, Errno> {
    sys_faccessat(current_task, FdNumber::AT_FDCWD, user_path, mode)
}

pub fn sys_stat(
    current_task: &CurrentTask,
    user_path: UserCString,
    buffer: UserRef<stat_t>,
) -> Result<SyscallResult, Errno> {
    // TODO(fxbug.dev/91430): Add the `AT_NO_AUTOMOUNT` flag once it is supported in
    // `sys_newfstatat`.
    sys_newfstatat(current_task, FdNumber::AT_FDCWD, user_path, buffer, 0)
}

pub fn sys_lstat(
    current_task: &CurrentTask,
    user_path: UserCString,
    buffer: UserRef<stat_t>,
) -> Result<SyscallResult, Errno> {
    // TODO(fxbug.dev/91430): Add the `AT_NO_AUTOMOUNT` flag once it is supported in
    // `sys_newfstatat`.
    sys_newfstatat(current_task, FdNumber::AT_FDCWD, user_path, buffer, AT_SYMLINK_NOFOLLOW)
}

pub fn sys_fstat(
    current_task: &CurrentTask,
    fd: FdNumber,
    buffer: UserRef<stat_t>,
) -> Result<SyscallResult, Errno> {
    let file = current_task.files.get(fd)?;
    let result = file.node().stat()?;
    current_task.mm.write_object(buffer, &result)?;
    Ok(SUCCESS)
}

pub fn sys_newfstatat(
    current_task: &CurrentTask,
    dir_fd: FdNumber,
    user_path: UserCString,
    buffer: UserRef<stat_t>,
    flags: u32,
) -> Result<SyscallResult, Errno> {
    if flags & !(AT_SYMLINK_NOFOLLOW | AT_EMPTY_PATH) != 0 {
        // TODO(fxbug.dev/91430): Support the `AT_NO_AUTOMOUNT` flag.
        not_implemented!("newfstatat: flags 0x{:x}", flags);
        return error!(ENOSYS);
    }
    let flags = LookupFlags::from_bits(flags, AT_EMPTY_PATH | AT_SYMLINK_NOFOLLOW)?;
    let name = lookup_at(current_task, dir_fd, user_path, flags)?;
    let result = name.entry.node.stat()?;
    current_task.mm.write_object(buffer, &result)?;
    Ok(SUCCESS)
}

pub fn sys_readlinkat(
    current_task: &CurrentTask,
    dir_fd: FdNumber,
    user_path: UserCString,
    buffer: UserAddress,
    buffer_size: usize,
) -> Result<usize, Errno> {
    let entry = lookup_parent_at(current_task, dir_fd, user_path, |parent, basename| {
        let stat = parent.entry.node.stat()?;
        // TODO(security): This check is obviously not correct, and should be updated once
        // we have an auth system.
        if stat.st_mode & S_IRUSR == 0 {
            return error!(EACCES);
        }
        let mut context = LookupContext::new(SymlinkMode::NoFollow);
        Ok(parent.lookup_child(&current_task, &mut context, basename)?.entry)
    })?;

    let target = match entry.node.readlink(&current_task)? {
        SymlinkTarget::Path(path) => path,
        SymlinkTarget::Node(node) => node.path(),
    };

    // Cap the returned length at buffer_size.
    let length = std::cmp::min(buffer_size, target.len());
    current_task.mm.write_memory(buffer, &target[..length])?;
    Ok(length)
}

pub fn sys_readlink(
    current_task: &CurrentTask,
    user_path: UserCString,
    buffer: UserAddress,
    buffer_size: usize,
) -> Result<usize, Errno> {
    sys_readlinkat(current_task, FdNumber::AT_FDCWD, user_path, buffer, buffer_size)
}

pub fn sys_truncate(
    current_task: &CurrentTask,
    user_path: UserCString,
    length: off_t,
) -> Result<SyscallResult, Errno> {
    let length = length.try_into().map_err(|_| errno!(EINVAL))?;
    let name = lookup_at(current_task, FdNumber::AT_FDCWD, user_path, LookupFlags::default())?;
    // TODO: Check for writability.
    name.entry.node.truncate(length)?;
    Ok(SUCCESS)
}

pub fn sys_ftruncate(
    current_task: &CurrentTask,
    fd: FdNumber,
    length: off_t,
) -> Result<SyscallResult, Errno> {
    let length = length.try_into().map_err(|_| errno!(EINVAL))?;
    let file = current_task.files.get(fd)?;
    // TODO: Check for writability.
    file.node().truncate(length)?;
    Ok(SUCCESS)
}

pub fn sys_mkdir(
    current_task: &CurrentTask,
    user_path: UserCString,
    mode: FileMode,
) -> Result<SyscallResult, Errno> {
    sys_mkdirat(current_task, FdNumber::AT_FDCWD, user_path, mode)
}

pub fn sys_mkdirat(
    current_task: &CurrentTask,
    dir_fd: FdNumber,
    user_path: UserCString,
    mode: FileMode,
) -> Result<SyscallResult, Errno> {
    let mode = current_task.fs.apply_umask(mode & FileMode::ALLOW_ALL);
    lookup_parent_at(current_task, dir_fd, user_path, |parent, basename| {
        parent.create_node(basename, FileMode::IFDIR | mode, DeviceType::NONE)
    })?;
    Ok(SUCCESS)
}

pub fn sys_mknodat(
    current_task: &CurrentTask,
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
    let mode = file_type | current_task.fs.apply_umask(mode & FileMode::ALLOW_ALL);
    lookup_parent_at(current_task, dir_fd, user_path, |parent, basename| {
        parent.create_node(basename, mode, dev)
    })?;
    Ok(SUCCESS)
}

pub fn sys_linkat(
    current_task: &CurrentTask,
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
    let flags = LookupFlags::from_bits(flags, AT_EMPTY_PATH | AT_SYMLINK_FOLLOW)?;
    let target = lookup_at(current_task, old_dir_fd, old_user_path, flags)?;
    if target.entry.node.is_dir() {
        return error!(EPERM);
    }
    lookup_parent_at(current_task, new_dir_fd, new_user_path, |parent, basename| {
        if !NamespaceNode::mount_eq(&target, &parent) {
            return error!(EXDEV);
        }
        parent.entry.link(basename, &target.entry.node)
    })?;

    Ok(SUCCESS)
}

pub fn sys_rmdir(
    current_task: &CurrentTask,
    user_path: UserCString,
) -> Result<SyscallResult, Errno> {
    sys_unlinkat(current_task, FdNumber::AT_FDCWD, user_path, AT_REMOVEDIR)
}

pub fn sys_unlink(
    current_task: &CurrentTask,
    user_path: UserCString,
) -> Result<SyscallResult, Errno> {
    sys_unlinkat(current_task, FdNumber::AT_FDCWD, user_path, 0)
}

pub fn sys_unlinkat(
    current_task: &CurrentTask,
    dir_fd: FdNumber,
    user_path: UserCString,
    flags: u32,
) -> Result<SyscallResult, Errno> {
    if flags & !AT_REMOVEDIR != 0 {
        return error!(EINVAL);
    }
    let kind =
        if flags & AT_REMOVEDIR != 0 { UnlinkKind::Directory } else { UnlinkKind::NonDirectory };
    lookup_parent_at(current_task, dir_fd, user_path, |parent, basename| {
        parent.unlink(basename, kind)
    })?;
    Ok(SUCCESS)
}

pub fn sys_rename(
    current_task: &CurrentTask,
    old_user_path: UserCString,
    new_user_path: UserCString,
) -> Result<SyscallResult, Errno> {
    sys_renameat(current_task, FdNumber::AT_FDCWD, old_user_path, FdNumber::AT_FDCWD, new_user_path)
}

pub fn sys_renameat(
    current_task: &CurrentTask,
    old_dir_fd: FdNumber,
    old_user_path: UserCString,
    new_dir_fd: FdNumber,
    new_user_path: UserCString,
) -> Result<SyscallResult, Errno> {
    let lookup = |dir_fd, user_path| {
        lookup_parent_at(current_task, dir_fd, user_path, |parent, basename| {
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

pub fn sys_chmod(
    current_task: &CurrentTask,
    user_path: UserCString,
    mode: FileMode,
) -> Result<SyscallResult, Errno> {
    sys_fchmodat(current_task, FdNumber::AT_FDCWD, user_path, mode)
}

pub fn sys_fchmod(
    current_task: &CurrentTask,
    fd: FdNumber,
    mode: FileMode,
) -> Result<SyscallResult, Errno> {
    if mode & FileMode::IFMT != FileMode::EMPTY {
        return error!(EINVAL);
    }
    let file = current_task.files.get(fd)?;
    file.name.entry.node.chmod(mode);
    Ok(SUCCESS)
}

pub fn sys_fchmodat(
    current_task: &CurrentTask,
    dir_fd: FdNumber,
    user_path: UserCString,
    mode: FileMode,
) -> Result<SyscallResult, Errno> {
    if mode & FileMode::IFMT != FileMode::EMPTY {
        return error!(EINVAL);
    }
    let name = lookup_at(current_task, dir_fd, user_path, LookupFlags::default())?;
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
    current_task: &CurrentTask,
    fd: FdNumber,
    owner: u32,
    group: u32,
) -> Result<SyscallResult, Errno> {
    let file = current_task.files.get(fd)?;
    // TODO(security): Needs permission check
    file.name.entry.node.chown(maybe_uid(owner), maybe_uid(group));
    Ok(SUCCESS)
}

pub fn sys_fchownat(
    current_task: &CurrentTask,
    dir_fd: FdNumber,
    user_path: UserCString,
    owner: u32,
    group: u32,
    flags: u32,
) -> Result<SyscallResult, Errno> {
    let flags = LookupFlags::from_bits(flags, AT_EMPTY_PATH | AT_SYMLINK_NOFOLLOW)?;
    let name = lookup_at(current_task, dir_fd, user_path, flags)?;
    name.entry.node.chown(maybe_uid(owner), maybe_uid(group));
    Ok(SUCCESS)
}

pub fn sys_fsetxattr(
    current_task: &CurrentTask,
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
    let file = current_task.files.get(fd)?;
    let mut name = vec![0u8; XATTR_NAME_MAX as usize];
    let name = current_task.mm.read_c_string(name_addr, &mut name).map_err(|e| {
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
    current_task.mm.read_memory(value_addr, &mut value)?;
    file.name.entry.node.set_xattr(name, &value, op)?;
    Ok(SUCCESS)
}

pub fn sys_getcwd(
    current_task: &CurrentTask,
    buf: UserAddress,
    size: usize,
) -> Result<usize, Errno> {
    let mut bytes = current_task.fs.cwd().path();
    bytes.push(b'\0');
    if bytes.len() > size {
        return error!(ERANGE);
    }
    current_task.mm.write_memory(buf, &bytes)?;
    Ok(bytes.len())
}

pub fn sys_umask(current_task: &CurrentTask, umask: FileMode) -> Result<FileMode, Errno> {
    Ok(current_task.fs.set_umask(umask))
}

fn get_fd_flags(flags: u32) -> FdFlags {
    if flags & O_CLOEXEC != 0 {
        FdFlags::CLOEXEC
    } else {
        FdFlags::empty()
    }
}

pub fn sys_pipe(
    current_task: &CurrentTask,
    user_pipe: UserRef<FdNumber>,
) -> Result<SyscallResult, Errno> {
    sys_pipe2(current_task, user_pipe, 0)
}

pub fn sys_pipe2(
    current_task: &CurrentTask,
    user_pipe: UserRef<FdNumber>,
    flags: u32,
) -> Result<SyscallResult, Errno> {
    let supported_file_flags = OpenFlags::NONBLOCK | OpenFlags::DIRECT;
    if flags & !(O_CLOEXEC | supported_file_flags.bits()) != 0 {
        return error!(EINVAL);
    }
    let (read, write) = new_pipe(current_task.kernel())?;

    let file_flags = OpenFlags::from_bits_truncate(flags & supported_file_flags.bits());
    read.update_file_flags(file_flags, supported_file_flags);
    write.update_file_flags(file_flags, supported_file_flags);

    let fd_flags = get_fd_flags(flags);
    let fd_read = current_task.files.add_with_flags(read, fd_flags)?;
    let fd_write = current_task.files.add_with_flags(write, fd_flags)?;

    current_task.mm.write_object(user_pipe, &fd_read)?;
    let user_pipe = user_pipe.next();
    current_task.mm.write_object(user_pipe, &fd_write)?;

    Ok(SUCCESS)
}

pub fn sys_ioctl(
    current_task: &CurrentTask,
    fd: FdNumber,
    request: u32,
    in_addr: UserAddress,
    out_addr: UserAddress,
) -> Result<SyscallResult, Errno> {
    let file = current_task.files.get(fd)?;
    file.ioctl(&current_task, request, in_addr, out_addr)
}

pub fn sys_symlinkat(
    current_task: &CurrentTask,
    user_target: UserCString,
    new_dir_fd: FdNumber,
    user_path: UserCString,
) -> Result<SyscallResult, Errno> {
    let mut buf = [0u8; PATH_MAX as usize];
    let target = current_task.mm.read_c_string(user_target, &mut buf)?;
    if target.len() == 0 {
        return error!(ENOENT);
    }

    let mut buf = [0u8; PATH_MAX as usize];
    let path = current_task.mm.read_c_string(user_path, &mut buf)?;
    // TODO: This check could probably be moved into parent.symlink(..).
    if path.len() == 0 {
        return error!(ENOENT);
    }

    lookup_parent_at(current_task, new_dir_fd, user_path, |parent, basename| {
        let stat = parent.entry.node.stat()?;
        if stat.st_mode & S_IWUSR == 0 {
            return error!(EACCES);
        }
        parent.symlink(basename, target)
    })?;
    Ok(SUCCESS)
}

pub fn sys_dup(current_task: &CurrentTask, oldfd: FdNumber) -> Result<FdNumber, Errno> {
    current_task.files.duplicate(oldfd, None, FdFlags::empty())
}

pub fn sys_dup3(
    current_task: &CurrentTask,
    oldfd: FdNumber,
    newfd: FdNumber,
    flags: u32,
) -> Result<FdNumber, Errno> {
    if oldfd == newfd {
        return error!(EINVAL);
    }
    if flags & !O_CLOEXEC != 0 {
        return error!(EINVAL);
    }
    let fd_flags = get_fd_flags(flags);
    current_task.files.duplicate(oldfd, Some(newfd), fd_flags)?;
    Ok(newfd)
}

pub fn sys_memfd_create(
    current_task: &CurrentTask,
    _user_name: UserCString,
    flags: u32,
) -> Result<FdNumber, Errno> {
    if flags & !MFD_CLOEXEC != 0 {
        not_implemented!("memfd_create: flags: {}", flags);
    }
    let file = new_memfd(current_task.kernel(), OpenFlags::RDWR)?;
    let mut fd_flags = FdFlags::empty();
    if flags & MFD_CLOEXEC != 0 {
        fd_flags |= FdFlags::CLOEXEC;
    }
    let fd = current_task.files.add_with_flags(file, fd_flags)?;
    Ok(fd)
}

// If the lookup ends up at the root of a mount, return its mountpoint instead. This is to make
// mount shadowing work right.
fn lookup_for_mount(
    current_task: &CurrentTask,
    path_addr: UserCString,
) -> Result<NamespaceNode, Errno> {
    let node = lookup_at(current_task, FdNumber::AT_FDCWD, path_addr, LookupFlags::default())?;
    Ok(node.escape_mount())
}

pub fn sys_mount(
    current_task: &CurrentTask,
    source_addr: UserCString,
    target_addr: UserCString,
    filesystemtype_addr: UserCString,
    flags: u32,
    _data_addr: UserAddress,
) -> Result<SyscallResult, Errno> {
    let flags = MountFlags::from_bits(flags).ok_or_else(|| {
        not_implemented!(
            "unsupported mount flags: {:#x}",
            flags & !MountFlags::from_bits_truncate(flags).bits()
        );
        errno!(EINVAL)
    })?;

    let target = lookup_for_mount(current_task, target_addr)?;

    if flags.contains(MountFlags::BIND) {
        strace!(current_task, "mount(MS_BIND)");

        if flags.contains(MountFlags::REC) {
            not_implemented!("MS_REC unimplemented");
        }

        let source = lookup_for_mount(current_task, source_addr)?;
        target.mount(WhatToMount::Dir(source.entry), flags)?;
    } else {
        let mut buf = [0u8; PATH_MAX as usize];
        let source = current_task.mm.read_c_string(source_addr, &mut buf)?;
        let mut buf = [0u8; PATH_MAX as usize];
        let fs_type = current_task.mm.read_c_string(filesystemtype_addr, &mut buf)?;
        strace!(
            current_task,
            "mount(source={:?}, type={:?})",
            String::from_utf8_lossy(source),
            String::from_utf8_lossy(fs_type)
        );

        let fs = create_filesystem(current_task.kernel(), source, fs_type, b"")?;
        target.mount(fs, flags)?;
    }

    Ok(SUCCESS)
}

pub fn sys_eventfd(current_task: &CurrentTask, value: u32) -> Result<FdNumber, Errno> {
    sys_eventfd2(current_task, value, 0)
}

pub fn sys_eventfd2(current_task: &CurrentTask, value: u32, flags: u32) -> Result<FdNumber, Errno> {
    if flags & !(EFD_CLOEXEC | EFD_NONBLOCK | EFD_SEMAPHORE) != 0 {
        return error!(EINVAL);
    }
    let blocking = (flags & EFD_NONBLOCK) == 0;
    let eventfd_type =
        if (flags & EFD_SEMAPHORE) == 0 { EventFdType::Counter } else { EventFdType::Semaphore };
    let file = new_eventfd(current_task.kernel(), value, eventfd_type, blocking);
    let fd_flags = if flags & EFD_CLOEXEC != 0 { FdFlags::CLOEXEC } else { FdFlags::empty() };
    let fd = current_task.files.add_with_flags(file, fd_flags)?;
    Ok(fd)
}

pub fn sys_timerfd_create(
    current_task: &CurrentTask,
    clock_id: u32,
    flags: u32,
) -> Result<FdNumber, Errno> {
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

    let timer = TimerFile::new(current_task.kernel(), open_flags)?;
    let fd = current_task.files.add_with_flags(timer, fd_flags)?;
    Ok(fd)
}

pub fn sys_timerfd_gettime<'a>(
    current_task: &CurrentTask,
    fd: FdNumber,
    user_current_value: UserRef<itimerspec>,
) -> Result<SyscallResult, Errno> {
    let file = current_task.files.get(fd)?;
    let timer_file = file.downcast_file::<TimerFile>().ok_or_else(|| errno!(EBADF))?;
    let timer_info = timer_file.current_timer_spec();
    current_task.mm.write_object(user_current_value, &timer_info)?;

    Ok(SUCCESS)
}

pub fn sys_timerfd_settime(
    current_task: &CurrentTask,
    fd: FdNumber,
    flags: u32,
    user_new_value: UserRef<itimerspec>,
    user_old_value: UserRef<itimerspec>,
) -> Result<SyscallResult, Errno> {
    if flags & !(TFD_TIMER_ABSTIME) != 0 {
        not_implemented!("timerfd_settime: flags 0x{:x}", flags);
        return error!(EINVAL);
    }

    let file = current_task.files.get(fd)?;
    let timer_file = file.downcast_file::<TimerFile>().ok_or_else(|| errno!(EBADF))?;

    let mut new_timer_spec = itimerspec::default();
    current_task.mm.read_object(user_new_value, &mut new_timer_spec)?;

    let old_timer_spec = timer_file.set_timer_spec(new_timer_spec, flags)?;
    if !user_old_value.is_null() {
        current_task.mm.write_object(user_old_value, &old_timer_spec)?;
    }

    Ok(SUCCESS)
}

pub fn sys_epoll_create(current_task: &CurrentTask, size: i32) -> Result<FdNumber, Errno> {
    if size < 1 {
        return error!(EINVAL);
    }
    sys_epoll_create1(current_task, 0)
}

pub fn sys_epoll_create1(current_task: &CurrentTask, flags: u32) -> Result<FdNumber, Errno> {
    if flags & !EPOLL_CLOEXEC != 0 {
        return Err(EINVAL);
    }
    let ep_file = EpollFileObject::new(current_task.kernel());
    let fd_flags = if flags & EPOLL_CLOEXEC != 0 { FdFlags::CLOEXEC } else { FdFlags::empty() };
    let fd = current_task.files.add_with_flags(ep_file, fd_flags)?;
    Ok(fd)
}

pub fn sys_epoll_ctl(
    current_task: &CurrentTask,
    epfd: FdNumber,
    op: u32,
    fd: FdNumber,
    event: UserRef<EpollEvent>,
) -> Result<SyscallResult, Errno> {
    let file = current_task.files.get(epfd)?;
    let epoll_file = file.downcast_file::<EpollFileObject>().ok_or_else(|| errno!(EINVAL))?;

    let ctl_file = current_task.files.get(fd)?;

    // TODO We cannot wait on other epoll fds for fear of deadlocks caused by
    // loops of dependency - for example, two loops that wait on each
    // other. Fix this by detecting loops and returning ELOOP.
    if ctl_file.downcast_file::<EpollFileObject>().is_some() {
        not_implemented!("epoll_ctl cannot yet add another epoll fd");
        return error!(ENOSYS);
    }

    let mut epoll_event = EpollEvent { events: 0, data: 0 };
    match op {
        EPOLL_CTL_ADD => {
            current_task.mm.read_object(event, &mut epoll_event)?;
            epoll_file.add(&current_task, &ctl_file, epoll_event)?;
        }
        EPOLL_CTL_MOD => {
            current_task.mm.read_object(event, &mut epoll_event)?;
            epoll_file.modify(&ctl_file, epoll_event)?;
        }
        EPOLL_CTL_DEL => epoll_file.delete(&ctl_file)?,
        _ => return error!(EINVAL),
    }
    Ok(SUCCESS)
}

pub fn sys_epoll_wait(
    current_task: &mut CurrentTask,
    epfd: FdNumber,
    events: UserRef<EpollEvent>,
    max_events: i32,
    timeout: i32,
) -> Result<usize, Errno> {
    sys_epoll_pwait(current_task, epfd, events, max_events, timeout, UserRef::<sigset_t>::default())
}

pub fn sys_epoll_pwait(
    current_task: &mut CurrentTask,
    epfd: FdNumber,
    events: UserRef<EpollEvent>,
    max_events: i32,
    timeout: i32,
    user_sigmask: UserRef<sigset_t>,
) -> Result<usize, Errno> {
    if max_events < 1 {
        return error!(EINVAL);
    }

    let file = current_task.files.get(epfd)?;
    let epoll_file = file.downcast_file::<EpollFileObject>().ok_or(errno!(EINVAL))?;

    let active_events = if !user_sigmask.is_null() {
        let mut signal_mask = sigset_t::default();
        current_task.mm.read_object(user_sigmask, &mut signal_mask)?;
        current_task.wait_with_temporary_mask(signal_mask, |current_task| {
            epoll_file.wait(current_task, max_events, timeout)
        })?
    } else {
        epoll_file.wait(&current_task, max_events, timeout)?
    };

    let mut event_ref = events;
    for event in active_events.iter() {
        current_task.mm.write_object(UserRef::new(event_ref.addr()), event)?;
        event_ref = event_ref.next();
    }

    Ok(active_events.len())
}

fn poll(
    current_task: &mut CurrentTask,
    user_pollfds: UserRef<pollfd>,
    num_fds: i32,
    mask: Option<sigset_t>,
    timeout: i32,
) -> Result<usize, Errno> {
    // TODO: Update this to use a dynamic limit (that can be set by setrlimit).
    if num_fds > RLIMIT_NOFILE_MAX as i32 || num_fds < 0 {
        return error!(EINVAL);
    }

    let mut pollfds = vec![pollfd::default(); num_fds as usize];
    let file_object = EpollFileObject::new(current_task.kernel());
    let epoll_file = file_object.downcast_file::<EpollFileObject>().unwrap();

    for (index, poll_descriptor) in pollfds.iter_mut().enumerate() {
        current_task.mm.read_object(user_pollfds.at(index), poll_descriptor)?;
        if poll_descriptor.fd < 0 {
            continue;
        }
        let file = current_task.files.get(FdNumber::from_raw(poll_descriptor.fd as i32))?;
        let event = EpollEvent { events: poll_descriptor.events as u32, data: index as u64 };
        epoll_file.add(&current_task, &file, event)?;
    }

    let mask = mask.unwrap_or_else(|| current_task.signals.read().mask);
    let ready_fds = current_task.wait_with_temporary_mask(mask, |current_task| {
        epoll_file.wait(current_task, num_fds, timeout)
    })?;

    for event in &ready_fds {
        pollfds[event.data as usize].revents = event.events as i16;
    }

    for (index, poll_descriptor) in pollfds.iter().enumerate() {
        current_task.mm.write_object(user_pollfds.at(index), poll_descriptor)?;
    }

    Ok(ready_fds.len())
}

pub fn sys_ppoll(
    current_task: &mut CurrentTask,
    user_fds: UserRef<pollfd>,
    num_fds: i32,
    user_timespec: UserRef<timespec>,
    user_mask: UserRef<sigset_t>,
) -> Result<usize, Errno> {
    let timeout = if user_timespec.is_null() {
        // Passing -1 to poll is equivalent to an infinite timeout.
        -1
    } else {
        let mut ts = timespec::default();
        current_task.mm.read_object(user_timespec, &mut ts)?;
        duration_from_timespec(ts)?.into_millis() as i32
    };

    let start_time = zx::Time::get_monotonic();

    let mask = if !user_mask.is_null() {
        let mut mask = sigset_t::default();
        current_task.mm.read_object(user_mask, &mut mask)?;
        Some(mask)
    } else {
        None
    };

    let poll_result = poll(current_task, user_fds, num_fds, mask, timeout);

    let elapsed_duration =
        zx::Duration::from_millis(timeout as i64) - (zx::Time::get_monotonic() - start_time);
    let remaining_duration = if elapsed_duration < zx::Duration::from_millis(0) {
        zx::Duration::from_millis(0)
    } else {
        elapsed_duration
    };
    let mut remaining_timespec = timespec_from_duration(remaining_duration);

    // From gVisor: "ppoll is normally restartable if interrupted by something other than a signal
    // handled by the application (i.e. returns ERESTARTNOHAND). However, if
    // [copy out] failed, then the restarted ppoll would use the wrong timeout, so the
    // error should be left as EINTR."
    match (current_task.mm.write_object(user_timespec, &mut remaining_timespec), poll_result) {
        // If write was ok, and poll was ok, return poll result.
        (Ok(_), Ok(num_events)) => Ok(num_events),
        // TODO: Here we should return an error that indicates the syscall should return EINTR if
        // interrupted by a signal with a user handler, and otherwise be restarted.
        (Ok(_), Err(e)) if e == EINTR => error!(EINTR),
        (Ok(_), poll_result) => poll_result,
        // If write was a failure, return the poll result unchanged.
        (Err(_), poll_result) => poll_result,
    }
}

pub fn sys_poll(
    current_task: &mut CurrentTask,
    user_fds: UserRef<pollfd>,
    num_fds: i32,
    timeout: i32,
) -> Result<usize, Errno> {
    poll(current_task, user_fds, num_fds, None, timeout)
}

pub fn sys_flock(
    _ctx: &CurrentTask,
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
        let (_kernel, current_task) = create_kernel_and_task_with_pkgfs();
        let fd = FdNumber::from_raw(10);
        let file_handle = current_task.open_file(b"data/testfile.txt", OpenFlags::RDONLY)?;
        let file_size = file_handle.node().stat().unwrap().st_size;
        current_task.files.insert(fd, file_handle);

        assert_eq!(sys_lseek(&current_task, fd, 0, SeekOrigin::CUR as u32)?, 0);
        assert_eq!(sys_lseek(&current_task, fd, 1, SeekOrigin::CUR as u32)?, 1);
        assert_eq!(sys_lseek(&current_task, fd, 3, SeekOrigin::SET as u32)?, 3);
        assert_eq!(sys_lseek(&current_task, fd, -3, SeekOrigin::CUR as u32)?, 0);
        assert_eq!(sys_lseek(&current_task, fd, 0, SeekOrigin::END as u32)?, file_size);
        assert_eq!(sys_lseek(&current_task, fd, -5, SeekOrigin::SET as u32), error!(EINVAL));

        // Make sure that the failed call above did not change the offset.
        assert_eq!(sys_lseek(&current_task, fd, 0, SeekOrigin::CUR as u32)?, file_size);

        // Prepare for an overflow.
        assert_eq!(sys_lseek(&current_task, fd, 3, SeekOrigin::SET as u32)?, 3);

        // Check for overflow.
        assert_eq!(sys_lseek(&current_task, fd, i64::MAX, SeekOrigin::CUR as u32), error!(EINVAL));

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_sys_dup() -> Result<(), Errno> {
        let (_kernel, current_task) = create_kernel_and_task_with_pkgfs();
        let file_handle = current_task.open_file(b"data/testfile.txt", OpenFlags::RDONLY)?;
        let files = &current_task.files;
        let oldfd = files.add(file_handle)?;
        let newfd = sys_dup(&current_task, oldfd)?;

        assert_ne!(oldfd, newfd);
        assert!(Arc::ptr_eq(&files.get(oldfd).unwrap(), &files.get(newfd).unwrap()));

        assert_eq!(sys_dup(&current_task, FdNumber::from_raw(3)), error!(EBADF));

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_sys_dup3() -> Result<(), Errno> {
        let (_kernel, current_task) = create_kernel_and_task_with_pkgfs();
        let file_handle = current_task.open_file(b"data/testfile.txt", OpenFlags::RDONLY)?;
        let files = &current_task.files;
        let oldfd = files.add(file_handle)?;
        let newfd = FdNumber::from_raw(2);
        sys_dup3(&current_task, oldfd, newfd, O_CLOEXEC)?;

        assert_ne!(oldfd, newfd);
        assert!(Arc::ptr_eq(&files.get(oldfd).unwrap(), &files.get(newfd).unwrap()));
        assert_eq!(files.get_fd_flags(oldfd).unwrap(), FdFlags::empty());
        assert_eq!(files.get_fd_flags(newfd).unwrap(), FdFlags::CLOEXEC);

        assert_eq!(sys_dup3(&current_task, oldfd, oldfd, O_CLOEXEC), error!(EINVAL));

        // Pass invalid flags.
        let invalid_flags = 1234;
        assert_eq!(sys_dup3(&current_task, oldfd, newfd, invalid_flags), error!(EINVAL));

        // Makes sure that dup closes the old file handle before the fd points
        // to the new file handle.
        let second_file_handle = current_task.open_file(b"data/testfile.txt", OpenFlags::RDONLY)?;
        let different_file_fd = files.add(second_file_handle)?;
        assert!(!Arc::ptr_eq(&files.get(oldfd).unwrap(), &files.get(different_file_fd).unwrap()));
        sys_dup3(&current_task, oldfd, different_file_fd, O_CLOEXEC)?;
        assert!(Arc::ptr_eq(&files.get(oldfd).unwrap(), &files.get(different_file_fd).unwrap()));

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_sys_open_cloexec() -> Result<(), Errno> {
        let (_kernel, current_task) = create_kernel_and_task_with_pkgfs();
        let path_addr = map_memory(&current_task, UserAddress::default(), *PAGE_SIZE);
        let path = b"data/testfile.txt\0";
        current_task.mm.write_memory(path_addr, path)?;
        let fd = sys_openat(
            &current_task,
            FdNumber::AT_FDCWD,
            UserCString::new(path_addr),
            O_RDONLY | O_CLOEXEC,
            FileMode::default(),
        )?;
        assert!(current_task.files.get_fd_flags(fd)?.contains(FdFlags::CLOEXEC));
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_sys_epoll() -> Result<(), Errno> {
        let (_kernel, current_task) = create_kernel_and_task_with_pkgfs();

        let epoll_fd = sys_epoll_create1(&current_task, 0).expect("sys_epoll_create1 failed");
        sys_close(&current_task, epoll_fd).expect("sys_close failed");

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_fstat_tmp_file() {
        let (_kernel, current_task) = create_kernel_and_task_with_pkgfs();

        // Create the file that will be used to stat.
        let file_path = b"data/testfile.txt";
        let _file_handle = current_task.open_file(file_path, OpenFlags::RDONLY).unwrap();

        // Write the path to user memory.
        let path_addr = map_memory(&current_task, UserAddress::default(), *PAGE_SIZE);
        current_task.mm.write_memory(path_addr, file_path).expect("failed to clear struct");

        let stat = statfs::default();
        let user_stat = UserRef::new(path_addr + file_path.len());
        current_task.mm.write_object(user_stat, &stat).expect("failed to clear struct");

        let user_path = UserCString::new(path_addr);

        assert_eq!(sys_statfs(&current_task, user_path, user_stat), Ok(SUCCESS));

        let mut returned_stat = statfs::default();
        current_task.mm.read_object(user_stat, &mut returned_stat).expect("failed to read struct");
        assert_eq!(returned_stat, statfs::default());
    }
}

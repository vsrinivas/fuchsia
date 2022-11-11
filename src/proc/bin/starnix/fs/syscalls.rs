// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::convert::TryInto;
use std::sync::Arc;
use std::usize;

use crate::fs::eventfd::*;
use crate::fs::fuchsia::*;
use crate::fs::inotify::*;
use crate::fs::pipe::*;
use crate::fs::*;
use crate::lock::Mutex;
use crate::logging::{log_trace, not_implemented};
use crate::mm::{MemoryAccessor, MemoryAccessorExt};
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
    file.read(current_task, &[UserBuffer { address, length }]).map_eintr(errno!(ERESTARTSYS))
}

pub fn sys_write(
    current_task: &CurrentTask,
    fd: FdNumber,
    address: UserAddress,
    length: usize,
) -> Result<usize, Errno> {
    let file = current_task.files.get(fd)?;
    file.write(current_task, &[UserBuffer { address, length }]).map_eintr(errno!(ERESTARTSYS))
}

pub fn sys_close(current_task: &CurrentTask, fd: FdNumber) -> Result<(), Errno> {
    current_task.files.close(fd)?;
    Ok(())
}

pub fn sys_lseek(
    current_task: &CurrentTask,
    fd: FdNumber,
    offset: off_t,
    whence: u32,
) -> Result<off_t, Errno> {
    let file = current_task.files.get(fd)?;
    file.seek(current_task, offset, SeekOrigin::from_raw(whence)?)
}

pub fn sys_fcntl(
    current_task: &CurrentTask,
    fd: FdNumber,
    cmd: u32,
    arg: u64,
) -> Result<SyscallResult, Errno> {
    match cmd {
        F_DUPFD | F_DUPFD_CLOEXEC => {
            let fd_number = arg as i32;
            let flags = if cmd == F_DUPFD_CLOEXEC { FdFlags::CLOEXEC } else { FdFlags::empty() };
            let newfd = current_task.files.duplicate(
                fd,
                TargetFdNumber::Minimum(FdNumber::from_raw(fd_number)),
                flags,
            )?;
            Ok(newfd.into())
        }
        F_GETOWN => {
            let file = current_task.files.get(fd)?;
            Ok(file.get_async_owner().into())
        }
        F_SETOWN => {
            if arg > std::i32::MAX as u64 {
                // Negative values are process groups.
                not_implemented!(current_task, "fcntl(F_SETOWN) does not support process groups");
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
        _ => {
            let file = current_task.files.get(fd)?;
            file.fcntl(current_task, cmd, arg)
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
    file.read_at(current_task, offset, &[UserBuffer { address, length }])
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
    file.write_at(current_task, offset, &[UserBuffer { address, length }])
}

pub fn sys_readv(
    current_task: &CurrentTask,
    fd: FdNumber,
    iovec_addr: UserAddress,
    iovec_count: i32,
) -> Result<usize, Errno> {
    let file = current_task.files.get(fd)?;
    let iovec = current_task.mm.read_iovec(iovec_addr, iovec_count)?;
    file.read(current_task, &iovec)
}

pub fn sys_writev(
    current_task: &CurrentTask,
    fd: FdNumber,
    iovec_addr: UserAddress,
    iovec_count: i32,
) -> Result<usize, Errno> {
    let iovec = current_task.mm.read_iovec(iovec_addr, iovec_count)?;
    let file = current_task.files.get(fd)?;
    file.write(current_task, &iovec)
}

pub fn sys_fstatfs(
    current_task: &CurrentTask,
    fd: FdNumber,
    user_buf: UserRef<statfs>,
) -> Result<(), Errno> {
    let file = current_task.files.get(fd)?;
    let stat = file.fs.statfs()?;
    current_task.mm.write_object(user_buf, &stat)?;
    Ok(())
}

pub fn sys_statfs(
    current_task: &CurrentTask,
    user_path: UserCString,
    user_buf: UserRef<statfs>,
) -> Result<(), Errno> {
    let node = lookup_at(current_task, FdNumber::AT_FDCWD, user_path, LookupFlags::default())?;
    let file_system = node.entry.node.fs();
    let stat = file_system.statfs()?;
    current_task.mm.write_object(user_buf, &stat)?;

    Ok(())
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
    log_trace!(
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
    log_trace!(
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
    fn no_follow() -> Self {
        Self { allow_empty_path: false, symlink_mode: SymlinkMode::NoFollow }
    }

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
    log_trace!(
        current_task,
        "lookup_at(dir_fd={}, path={:?})",
        dir_fd,
        String::from_utf8_lossy(path)
    );
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
    let file = open_file_at(current_task, dir_fd, user_path, flags, mode)?;
    let fd_flags = get_fd_flags(flags);
    current_task.files.add_with_flags(file, fd_flags)
}

pub fn sys_faccessat(
    current_task: &CurrentTask,
    dir_fd: FdNumber,
    user_path: UserCString,
    mode: u32,
) -> Result<(), Errno> {
    sys_faccessat2(current_task, dir_fd, user_path, mode, 0)
}

pub fn sys_faccessat2(
    current_task: &CurrentTask,
    dir_fd: FdNumber,
    user_path: UserCString,
    mode: u32,
    flags: u32,
) -> Result<(), Errno> {
    let mode = Access::from_bits(mode).ok_or_else(|| errno!(EINVAL))?;
    let lookup_flags = LookupFlags::from_bits(flags, AT_SYMLINK_NOFOLLOW | AT_EACCESS)?;
    let name = lookup_at(current_task, dir_fd, user_path, lookup_flags)?;
    name.entry.node.check_access(current_task, mode)
}

pub fn sys_getdents(
    current_task: &CurrentTask,
    fd: FdNumber,
    user_buffer: UserAddress,
    user_capacity: usize,
) -> Result<usize, Errno> {
    let file = current_task.files.get(fd)?;
    let mut offset = file.offset.lock();
    let mut sink = DirentSink32::new(current_task, &mut offset, user_buffer, user_capacity);
    file.readdir(current_task, &mut sink)?;
    Ok(sink.actual())
}

pub fn sys_getdents64(
    current_task: &CurrentTask,
    fd: FdNumber,
    user_buffer: UserAddress,
    user_capacity: usize,
) -> Result<usize, Errno> {
    let file = current_task.files.get(fd)?;
    let mut offset = file.offset.lock();
    let mut sink = DirentSink64::new(current_task, &mut offset, user_buffer, user_capacity);
    file.readdir(current_task, &mut sink)?;
    Ok(sink.actual())
}

pub fn sys_chroot(current_task: &CurrentTask, user_path: UserCString) -> Result<(), Errno> {
    if !current_task.creds().has_capability(CAP_SYS_CHROOT) {
        return error!(EPERM);
    }

    let name = lookup_at(current_task, FdNumber::AT_FDCWD, user_path, LookupFlags::default())?;
    if !name.entry.node.is_dir() {
        return error!(ENOTDIR);
    }

    current_task.fs().chroot(name);
    Ok(())
}

pub fn sys_chdir(current_task: &CurrentTask, user_path: UserCString) -> Result<(), Errno> {
    let name = lookup_at(current_task, FdNumber::AT_FDCWD, user_path, LookupFlags::default())?;
    if !name.entry.node.is_dir() {
        return error!(ENOTDIR);
    }
    current_task.fs().chdir(current_task, name)
}

pub fn sys_fchdir(current_task: &CurrentTask, fd: FdNumber) -> Result<(), Errno> {
    let file = current_task.files.get(fd)?;
    if !file.name.entry.node.is_dir() {
        return error!(ENOTDIR);
    }
    current_task.fs().chdir(current_task, file.name.clone())
}

pub fn sys_access(
    current_task: &CurrentTask,
    user_path: UserCString,
    mode: u32,
) -> Result<(), Errno> {
    sys_faccessat(current_task, FdNumber::AT_FDCWD, user_path, mode)
}

pub fn sys_stat(
    current_task: &CurrentTask,
    user_path: UserCString,
    buffer: UserRef<stat_t>,
) -> Result<(), Errno> {
    // TODO(fxbug.dev/91430): Add the `AT_NO_AUTOMOUNT` flag once it is supported in
    // `sys_newfstatat`.
    sys_newfstatat(current_task, FdNumber::AT_FDCWD, user_path, buffer, 0)
}

pub fn sys_lstat(
    current_task: &CurrentTask,
    user_path: UserCString,
    buffer: UserRef<stat_t>,
) -> Result<(), Errno> {
    // TODO(fxbug.dev/91430): Add the `AT_NO_AUTOMOUNT` flag once it is supported in
    // `sys_newfstatat`.
    sys_newfstatat(current_task, FdNumber::AT_FDCWD, user_path, buffer, AT_SYMLINK_NOFOLLOW)
}

pub fn sys_fstat(
    current_task: &CurrentTask,
    fd: FdNumber,
    buffer: UserRef<stat_t>,
) -> Result<(), Errno> {
    let file = current_task.files.get(fd)?;
    let result = file.node().stat()?;
    current_task.mm.write_object(buffer, &result)?;
    Ok(())
}

pub fn sys_newfstatat(
    current_task: &CurrentTask,
    dir_fd: FdNumber,
    user_path: UserCString,
    buffer: UserRef<stat_t>,
    flags: u32,
) -> Result<(), Errno> {
    if flags & !(AT_SYMLINK_NOFOLLOW | AT_EMPTY_PATH) != 0 {
        // TODO(fxbug.dev/91430): Support the `AT_NO_AUTOMOUNT` flag.
        not_implemented!(current_task, "newfstatat: flags 0x{:x}", flags);
        return error!(ENOSYS);
    }
    let flags = LookupFlags::from_bits(flags, AT_EMPTY_PATH | AT_SYMLINK_NOFOLLOW)?;
    let name = lookup_at(current_task, dir_fd, user_path, flags)?;
    let result = name.entry.node.stat()?;
    current_task.mm.write_object(buffer, &result)?;
    Ok(())
}

pub fn sys_readlinkat(
    current_task: &CurrentTask,
    dir_fd: FdNumber,
    user_path: UserCString,
    buffer: UserAddress,
    buffer_size: usize,
) -> Result<usize, Errno> {
    let entry = lookup_parent_at(current_task, dir_fd, user_path, |parent, basename| {
        let mut context = LookupContext::new(SymlinkMode::NoFollow);
        Ok(parent.lookup_child(current_task, &mut context, basename)?.entry)
    })?;

    let target = match entry.node.readlink(current_task)? {
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
) -> Result<(), Errno> {
    let length = length.try_into().map_err(|_| errno!(EINVAL))?;
    let name = lookup_at(current_task, FdNumber::AT_FDCWD, user_path, LookupFlags::default())?;
    name.entry.node.truncate(current_task, length)?;
    Ok(())
}

pub fn sys_ftruncate(current_task: &CurrentTask, fd: FdNumber, length: off_t) -> Result<(), Errno> {
    let length = length.try_into().map_err(|_| errno!(EINVAL))?;
    let file = current_task.files.get(fd)?;
    file.node().truncate(current_task, length)?;
    Ok(())
}

pub fn sys_mkdir(
    current_task: &CurrentTask,
    user_path: UserCString,
    mode: FileMode,
) -> Result<(), Errno> {
    sys_mkdirat(current_task, FdNumber::AT_FDCWD, user_path, mode)
}

pub fn sys_mkdirat(
    current_task: &CurrentTask,
    dir_fd: FdNumber,
    user_path: UserCString,
    mode: FileMode,
) -> Result<(), Errno> {
    let mut buf = [0u8; PATH_MAX as usize];
    let mut path = current_task.mm.read_c_string(user_path, &mut buf)?;

    // Strip trailing slashes to make mkdir("foo/") valid even if "foo/" doesn't exist. This is a
    // special case for mkdir. Every other fs operation (including O_CREAT) will handle "foo/" by
    // attempt to traverse into foo/ and failing if it doesn't exist.
    while let Some(one_slash_removed) = path.strip_suffix(b"/") {
        path = one_slash_removed;
    }
    let mut end = path.len();
    while end > 0 && path[end - 1] == b'/' {
        end -= 1;
    }
    let path = &path[..end];

    if path.is_empty() {
        return error!(ENOENT);
    }
    let (parent, basename) = current_task.lookup_parent_at(dir_fd, path)?;
    parent.create_node(
        current_task,
        basename,
        mode.with_type(FileMode::IFDIR),
        DeviceType::NONE,
    )?;
    Ok(())
}

pub fn sys_mknodat(
    current_task: &CurrentTask,
    dir_fd: FdNumber,
    user_path: UserCString,
    mode: FileMode,
    dev: DeviceType,
) -> Result<(), Errno> {
    let file_type = match mode & FileMode::IFMT {
        FileMode::IFREG
        | FileMode::IFCHR
        | FileMode::IFBLK
        | FileMode::IFIFO
        | FileMode::IFSOCK => mode & FileMode::IFMT,
        FileMode::EMPTY => FileMode::IFREG,
        _ => return error!(EINVAL),
    };
    lookup_parent_at(current_task, dir_fd, user_path, |parent, basename| {
        parent.create_node(current_task, basename, mode.with_type(file_type), dev)
    })?;
    Ok(())
}

pub fn sys_linkat(
    current_task: &CurrentTask,
    old_dir_fd: FdNumber,
    old_user_path: UserCString,
    new_dir_fd: FdNumber,
    new_user_path: UserCString,
    flags: u32,
) -> Result<(), Errno> {
    if flags & !(AT_SYMLINK_FOLLOW | AT_EMPTY_PATH) != 0 {
        not_implemented!(current_task, "linkat: flags 0x{:x}", flags);
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
        parent.entry.link(current_task, basename, &target.entry.node)
    })?;

    Ok(())
}

pub fn sys_rmdir(current_task: &CurrentTask, user_path: UserCString) -> Result<(), Errno> {
    sys_unlinkat(current_task, FdNumber::AT_FDCWD, user_path, AT_REMOVEDIR)
}

pub fn sys_unlink(current_task: &CurrentTask, user_path: UserCString) -> Result<(), Errno> {
    sys_unlinkat(current_task, FdNumber::AT_FDCWD, user_path, 0)
}

pub fn sys_unlinkat(
    current_task: &CurrentTask,
    dir_fd: FdNumber,
    user_path: UserCString,
    flags: u32,
) -> Result<(), Errno> {
    if flags & !AT_REMOVEDIR != 0 {
        return error!(EINVAL);
    }
    let kind =
        if flags & AT_REMOVEDIR != 0 { UnlinkKind::Directory } else { UnlinkKind::NonDirectory };
    lookup_parent_at(current_task, dir_fd, user_path, |parent, basename| {
        parent.unlink(current_task, basename, kind)
    })?;
    Ok(())
}

pub fn sys_rename(
    current_task: &CurrentTask,
    old_user_path: UserCString,
    new_user_path: UserCString,
) -> Result<(), Errno> {
    sys_renameat(current_task, FdNumber::AT_FDCWD, old_user_path, FdNumber::AT_FDCWD, new_user_path)
}

pub fn sys_renameat(
    current_task: &CurrentTask,
    old_dir_fd: FdNumber,
    old_user_path: UserCString,
    new_dir_fd: FdNumber,
    new_user_path: UserCString,
) -> Result<(), Errno> {
    sys_renameat2(current_task, old_dir_fd, old_user_path, new_dir_fd, new_user_path, 0)
}

pub fn sys_renameat2(
    current_task: &CurrentTask,
    old_dir_fd: FdNumber,
    old_user_path: UserCString,
    new_dir_fd: FdNumber,
    new_user_path: UserCString,
    flags: u32,
) -> Result<(), Errno> {
    if flags != 0 {
        not_implemented!(current_task, "renameat flags {:?}", flags);
        return error!(EINVAL);
    }

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

    DirEntry::rename(current_task, &old_parent, &old_basename, &new_parent, &new_basename)?;
    Ok(())
}

pub fn sys_chmod(
    current_task: &CurrentTask,
    user_path: UserCString,
    mode: FileMode,
) -> Result<(), Errno> {
    sys_fchmodat(current_task, FdNumber::AT_FDCWD, user_path, mode)
}

pub fn sys_fchmod(current_task: &CurrentTask, fd: FdNumber, mode: FileMode) -> Result<(), Errno> {
    // Remove the filetype from the mode.
    let mode = mode & FileMode::PERMISSIONS;
    let file = current_task.files.get_unless_opath(fd)?;
    file.name.entry.node.chmod(mode);
    Ok(())
}

pub fn sys_fchmodat(
    current_task: &CurrentTask,
    dir_fd: FdNumber,
    user_path: UserCString,
    mode: FileMode,
) -> Result<(), Errno> {
    // Remove the filetype from the mode.
    let mode = mode & FileMode::PERMISSIONS;
    let name = lookup_at(current_task, dir_fd, user_path, LookupFlags::default())?;
    name.entry.node.chmod(mode);
    Ok(())
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
) -> Result<(), Errno> {
    let file = current_task.files.get_unless_opath(fd)?;
    // TODO(security): Needs permission check
    file.name.entry.node.chown(maybe_uid(owner), maybe_uid(group));
    Ok(())
}

pub fn sys_fchownat(
    current_task: &CurrentTask,
    dir_fd: FdNumber,
    user_path: UserCString,
    owner: u32,
    group: u32,
    flags: u32,
) -> Result<(), Errno> {
    let flags = LookupFlags::from_bits(flags, AT_EMPTY_PATH | AT_SYMLINK_NOFOLLOW)?;
    let name = lookup_at(current_task, dir_fd, user_path, flags)?;
    name.entry.node.chown(maybe_uid(owner), maybe_uid(group));
    Ok(())
}

fn read_xattr_name<'a>(
    current_task: &CurrentTask,
    name_addr: UserCString,
    buf: &'a mut [u8],
) -> Result<&'a [u8], Errno> {
    let name = current_task.mm.read_c_string(name_addr, buf).map_err(|e| {
        if e == ENAMETOOLONG {
            errno!(ERANGE)
        } else {
            e
        }
    })?;
    if name.is_empty() {
        return error!(ERANGE);
    }
    let dot_index = memchr::memchr(b'.', name).ok_or_else(|| errno!(ENOTSUP))?;
    if name[dot_index + 1..].is_empty() {
        return error!(EINVAL);
    }
    match &name[..dot_index] {
        b"user" | b"security" | b"trusted" | b"system" => {}
        _ => return error!(ENOTSUP),
    }
    Ok(name)
}

fn do_getxattr(
    current_task: &CurrentTask,
    node: &NamespaceNode,
    name_addr: UserCString,
    value_addr: UserAddress,
    size: usize,
) -> Result<usize, Errno> {
    let mut name = vec![0u8; XATTR_NAME_MAX as usize + 1];
    let name = read_xattr_name(current_task, name_addr, &mut name)?;
    let value = node.entry.node.get_xattr(current_task, name)?;
    if size == 0 {
        return Ok(value.len());
    }
    if size < value.len() {
        return error!(ERANGE);
    }
    current_task.mm.write_memory(value_addr, &value)
}

pub fn sys_getxattr(
    current_task: &CurrentTask,
    path_addr: UserCString,
    name_addr: UserCString,
    value_addr: UserAddress,
    size: usize,
) -> Result<usize, Errno> {
    let node = lookup_at(current_task, FdNumber::AT_FDCWD, path_addr, LookupFlags::default())?;
    do_getxattr(current_task, &node, name_addr, value_addr, size)
}

pub fn sys_fgetxattr(
    current_task: &CurrentTask,
    fd: FdNumber,
    name_addr: UserCString,
    value_addr: UserAddress,
    size: usize,
) -> Result<usize, Errno> {
    let file = current_task.files.get_unless_opath(fd)?;
    do_getxattr(current_task, &file.name, name_addr, value_addr, size)
}

pub fn sys_lgetxattr(
    current_task: &CurrentTask,
    path_addr: UserCString,
    name_addr: UserCString,
    value_addr: UserAddress,
    size: usize,
) -> Result<usize, Errno> {
    let node = lookup_at(current_task, FdNumber::AT_FDCWD, path_addr, LookupFlags::no_follow())?;
    do_getxattr(current_task, &node, name_addr, value_addr, size)
}

fn do_setxattr(
    current_task: &CurrentTask,
    node: &NamespaceNode,
    name_addr: UserCString,
    value_addr: UserAddress,
    size: usize,
    flags: u32,
) -> Result<(), Errno> {
    if size > XATTR_NAME_MAX as usize {
        return error!(E2BIG);
    }
    let mode = node.entry.node.info().mode;
    if mode.is_chr() || mode.is_fifo() {
        return error!(EPERM);
    }

    let op = match flags {
        0 => XattrOp::Set,
        XATTR_CREATE => XattrOp::Create,
        XATTR_REPLACE => XattrOp::Replace,
        _ => return error!(EINVAL),
    };
    let mut name = vec![0u8; XATTR_NAME_MAX as usize + 1];
    let name = read_xattr_name(current_task, name_addr, &mut name)?;
    let mut value = vec![0u8; size];
    current_task.mm.read_memory(value_addr, &mut value)?;
    node.entry.node.set_xattr(current_task, name, &value, op)
}

pub fn sys_fsetxattr(
    current_task: &CurrentTask,
    fd: FdNumber,
    name_addr: UserCString,
    value_addr: UserAddress,
    size: usize,
    flags: u32,
) -> Result<(), Errno> {
    let file = current_task.files.get_unless_opath(fd)?;
    do_setxattr(current_task, &file.name, name_addr, value_addr, size, flags)
}

pub fn sys_lsetxattr(
    current_task: &CurrentTask,
    path_addr: UserCString,
    name_addr: UserCString,
    value_addr: UserAddress,
    size: usize,
    flags: u32,
) -> Result<(), Errno> {
    let node = lookup_at(current_task, FdNumber::AT_FDCWD, path_addr, LookupFlags::no_follow())?;
    do_setxattr(current_task, &node, name_addr, value_addr, size, flags)
}

pub fn sys_setxattr(
    current_task: &CurrentTask,
    path_addr: UserCString,
    name_addr: UserCString,
    value_addr: UserAddress,
    size: usize,
    flags: u32,
) -> Result<(), Errno> {
    let node = lookup_at(current_task, FdNumber::AT_FDCWD, path_addr, LookupFlags::default())?;
    do_setxattr(current_task, &node, name_addr, value_addr, size, flags)
}

fn do_removexattr(
    current_task: &CurrentTask,
    node: &NamespaceNode,
    name_addr: UserCString,
) -> Result<(), Errno> {
    let mode = node.entry.node.info().mode;
    if mode.is_chr() || mode.is_fifo() {
        return error!(EPERM);
    }
    let mut name = vec![0u8; XATTR_NAME_MAX as usize + 1];
    let name = read_xattr_name(current_task, name_addr, &mut name)?;
    node.entry.node.remove_xattr(current_task, name)
}

pub fn sys_removexattr(
    current_task: &CurrentTask,
    path_addr: UserCString,
    name_addr: UserCString,
) -> Result<(), Errno> {
    let node = lookup_at(current_task, FdNumber::AT_FDCWD, path_addr, LookupFlags::default())?;
    do_removexattr(current_task, &node, name_addr)
}

pub fn sys_lremovexattr(
    current_task: &CurrentTask,
    path_addr: UserCString,
    name_addr: UserCString,
) -> Result<(), Errno> {
    let node = lookup_at(current_task, FdNumber::AT_FDCWD, path_addr, LookupFlags::no_follow())?;
    if node.entry.node.is_lnk() {
        return error!(EPERM);
    }
    do_removexattr(current_task, &node, name_addr)
}

pub fn sys_fremovexattr(
    current_task: &CurrentTask,
    fd: FdNumber,
    name_addr: UserCString,
) -> Result<(), Errno> {
    let file = current_task.files.get_unless_opath(fd)?;
    do_removexattr(current_task, &file.name, name_addr)
}

fn do_listxattr(
    current_task: &CurrentTask,
    node: &NamespaceNode,
    list_addr: UserAddress,
    size: usize,
) -> Result<usize, Errno> {
    let mut list = vec![];
    let xattrs = node.entry.node.list_xattrs(current_task)?;
    for name in xattrs.iter() {
        list.extend_from_slice(name);
        list.push(b'\0');
    }
    if size == 0 {
        return Ok(list.len());
    }
    if size < list.len() {
        return error!(ERANGE);
    }
    current_task.mm.write_memory(list_addr, &list)
}

pub fn sys_listxattr(
    current_task: &CurrentTask,
    path_addr: UserCString,
    list_addr: UserAddress,
    size: usize,
) -> Result<usize, Errno> {
    let node = lookup_at(current_task, FdNumber::AT_FDCWD, path_addr, LookupFlags::default())?;
    do_listxattr(current_task, &node, list_addr, size)
}

pub fn sys_llistxattr(
    current_task: &CurrentTask,
    path_addr: UserCString,
    list_addr: UserAddress,
    size: usize,
) -> Result<usize, Errno> {
    let node = lookup_at(current_task, FdNumber::AT_FDCWD, path_addr, LookupFlags::no_follow())?;
    do_listxattr(current_task, &node, list_addr, size)
}

pub fn sys_flistxattr(
    current_task: &CurrentTask,
    fd: FdNumber,
    list_addr: UserAddress,
    size: usize,
) -> Result<usize, Errno> {
    let file = current_task.files.get_unless_opath(fd)?;
    do_listxattr(current_task, &file.name, list_addr, size)
}

pub fn sys_getcwd(
    current_task: &CurrentTask,
    buf: UserAddress,
    size: usize,
) -> Result<usize, Errno> {
    let mut cwd = current_task.fs().cwd().path();

    let mut bytes = vec![];
    if !cwd.starts_with(&current_task.fs().root().path()) {
        bytes.append(&mut b"(unreachable)".to_vec());
    }

    bytes.append(&mut cwd);
    bytes.push(b'\0');
    if bytes.len() > size {
        return error!(ERANGE);
    }
    current_task.mm.write_memory(buf, &bytes)?;
    Ok(bytes.len())
}

pub fn sys_umask(current_task: &CurrentTask, umask: FileMode) -> Result<FileMode, Errno> {
    Ok(current_task.fs().set_umask(umask))
}

fn get_fd_flags(flags: u32) -> FdFlags {
    if flags & O_CLOEXEC != 0 {
        FdFlags::CLOEXEC
    } else {
        FdFlags::empty()
    }
}

pub fn sys_pipe(current_task: &CurrentTask, user_pipe: UserRef<FdNumber>) -> Result<(), Errno> {
    sys_pipe2(current_task, user_pipe, 0)
}

pub fn sys_pipe2(
    current_task: &CurrentTask,
    user_pipe: UserRef<FdNumber>,
    flags: u32,
) -> Result<(), Errno> {
    let supported_file_flags = OpenFlags::NONBLOCK | OpenFlags::DIRECT;
    if flags & !(O_CLOEXEC | supported_file_flags.bits()) != 0 {
        return error!(EINVAL);
    }
    let (read, write) = new_pipe(current_task)?;

    let file_flags = OpenFlags::from_bits_truncate(flags & supported_file_flags.bits());
    read.update_file_flags(file_flags, supported_file_flags);
    write.update_file_flags(file_flags, supported_file_flags);

    let fd_flags = get_fd_flags(flags);
    let fd_read = current_task.files.add_with_flags(read, fd_flags)?;
    let fd_write = current_task.files.add_with_flags(write, fd_flags)?;
    log_trace!(current_task, "pipe2 -> [{:#x}, {:#x}]", fd_read.raw(), fd_write.raw());

    current_task.mm.write_object(user_pipe, &fd_read)?;
    let user_pipe = user_pipe.next();
    current_task.mm.write_object(user_pipe, &fd_write)?;

    Ok(())
}

pub fn sys_ioctl(
    current_task: &CurrentTask,
    fd: FdNumber,
    request: u32,
    user_addr: UserAddress,
) -> Result<SyscallResult, Errno> {
    let file = current_task.files.get(fd)?;
    match request {
        FIONBIO => {
            file.update_file_flags(OpenFlags::NONBLOCK, OpenFlags::NONBLOCK);
            Ok(SUCCESS)
        }
        _ => file.ioctl(current_task, request, user_addr),
    }
}

pub fn sys_symlinkat(
    current_task: &CurrentTask,
    user_target: UserCString,
    new_dir_fd: FdNumber,
    user_path: UserCString,
) -> Result<(), Errno> {
    let mut buf = [0u8; PATH_MAX as usize];
    let target = current_task.mm.read_c_string(user_target, &mut buf)?;
    if target.is_empty() {
        return error!(ENOENT);
    }

    let mut buf = [0u8; PATH_MAX as usize];
    let path = current_task.mm.read_c_string(user_path, &mut buf)?;
    // TODO: This check could probably be moved into parent.symlink(..).
    if path.is_empty() {
        return error!(ENOENT);
    }

    let res = lookup_parent_at(current_task, new_dir_fd, user_path, |parent, basename| {
        parent.symlink(current_task, basename, target)
    });
    res?;
    Ok(())
}

pub fn sys_dup(current_task: &CurrentTask, oldfd: FdNumber) -> Result<FdNumber, Errno> {
    current_task.files.duplicate(oldfd, TargetFdNumber::Default, FdFlags::empty())
}

pub fn sys_dup2(
    current_task: &CurrentTask,
    oldfd: FdNumber,
    newfd: FdNumber,
) -> Result<FdNumber, Errno> {
    if oldfd == newfd {
        current_task.files.get(oldfd)?;
        return Ok(newfd);
    }
    sys_dup3(current_task, oldfd, newfd, 0)
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
    current_task.files.duplicate(oldfd, TargetFdNumber::Specific(newfd), fd_flags)?;
    Ok(newfd)
}

pub fn sys_memfd_create(
    current_task: &CurrentTask,
    _user_name: UserCString,
    flags: u32,
) -> Result<FdNumber, Errno> {
    if flags & !MFD_CLOEXEC != 0 {
        not_implemented!(current_task, "memfd_create: flags: {}", flags);
    }
    let file = new_memfd(current_task, OpenFlags::RDWR)?;
    let mut fd_flags = FdFlags::empty();
    if flags & MFD_CLOEXEC != 0 {
        fd_flags |= FdFlags::CLOEXEC;
    }
    let fd = current_task.files.add_with_flags(file, fd_flags)?;
    Ok(fd)
}

pub fn sys_mount(
    current_task: &CurrentTask,
    source_addr: UserCString,
    target_addr: UserCString,
    filesystemtype_addr: UserCString,
    flags: u32,
    data_addr: UserCString,
) -> Result<(), Errno> {
    if !current_task.creds().has_capability(CAP_SYS_ADMIN) {
        return error!(EPERM);
    }

    let flags = MountFlags::from_bits(flags).ok_or_else(|| {
        not_implemented!(
            current_task,
            "unsupported mount flags: {:#x}",
            flags & !MountFlags::from_bits_truncate(flags).bits()
        );
        errno!(EINVAL)
    })?;

    let target = lookup_at(current_task, FdNumber::AT_FDCWD, target_addr, LookupFlags::default())?;

    if flags.contains(MountFlags::BIND) {
        do_mount_bind(current_task, source_addr, target, flags)
    } else if flags.intersects(MountFlags::SHARED | MountFlags::PRIVATE | MountFlags::DOWNSTREAM) {
        do_mount_change_propagation_type(current_task, target, flags)
    } else {
        do_mount_create(current_task, source_addr, target, filesystemtype_addr, data_addr, flags)
    }
}

fn do_mount_bind(
    current_task: &CurrentTask,
    source_addr: UserCString,
    target: NamespaceNode,
    flags: MountFlags,
) -> Result<(), Errno> {
    let source = lookup_at(current_task, FdNumber::AT_FDCWD, source_addr, LookupFlags::default())?;
    log_trace!(
        current_task,
        "mount(source={:?}, target={:?}, flags={:?})",
        String::from_utf8_lossy(&source.path()),
        String::from_utf8_lossy(&target.path()),
        flags
    );
    target.mount(WhatToMount::Bind(source), flags)
}

fn do_mount_change_propagation_type(
    current_task: &CurrentTask,
    target: NamespaceNode,
    flags: MountFlags,
) -> Result<(), Errno> {
    log_trace!(
        current_task,
        "mount(target={:?}, flags={:?})",
        String::from_utf8_lossy(&target.path()),
        flags
    );

    // Flag validation. Of the three propagation type flags, exactly one must be passed. The only
    // valid flags other than propagation type are MS_SILENT and MS_REC.
    //
    // Use if statements to find the first propagation type flag, then check for valid flags using
    // only the first propagation flag and MS_REC / MS_SILENT as valid flags.
    let propagation_flag = if flags.contains(MountFlags::SHARED) {
        MountFlags::SHARED
    } else if flags.contains(MountFlags::PRIVATE) {
        MountFlags::PRIVATE
    } else if flags.contains(MountFlags::DOWNSTREAM) {
        MountFlags::DOWNSTREAM
    } else {
        return error!(EINVAL);
    };
    if flags.intersects(!(propagation_flag | MountFlags::REC | MountFlags::SILENT)) {
        return error!(EINVAL);
    }

    let mount = target.mount_if_root()?;
    mount.change_propagation(propagation_flag, flags.contains(MountFlags::REC));
    Ok(())
}

fn do_mount_create(
    current_task: &CurrentTask,
    source_addr: UserCString,
    target: NamespaceNode,
    filesystemtype_addr: UserCString,
    data_addr: UserCString,
    flags: MountFlags,
) -> Result<(), Errno> {
    let mut buf = [0u8; PATH_MAX as usize];
    let source = if source_addr.is_null() {
        b""
    } else {
        current_task.mm.read_c_string(source_addr, &mut buf)?
    };
    let mut buf = [0u8; PATH_MAX as usize];
    let fs_type = current_task.mm.read_c_string(filesystemtype_addr, &mut buf)?;
    let mut buf = [0u8; PATH_MAX as usize];
    let data = if data_addr.is_null() {
        b""
    } else {
        current_task.mm.read_c_string(data_addr, &mut buf)?
    };
    log_trace!(
        current_task,
        "mount(source={:?}, target={:?}, type={:?}, data={:?})",
        String::from_utf8_lossy(source),
        String::from_utf8_lossy(&target.path()),
        String::from_utf8_lossy(fs_type),
        String::from_utf8_lossy(data)
    );

    target.mount(create_filesystem(current_task, source, fs_type, data)?, flags)
}

pub fn sys_umount2(
    current_task: &CurrentTask,
    target_addr: UserCString,
    flags: u32,
) -> Result<(), Errno> {
    if !current_task.creds().has_capability(CAP_SYS_ADMIN) {
        return error!(EPERM);
    }

    let flags = if flags & UMOUNT_NOFOLLOW != 0 {
        LookupFlags::no_follow()
    } else {
        LookupFlags::default()
    };
    let target = lookup_at(current_task, FdNumber::AT_FDCWD, target_addr, flags)?;
    target.unmount()
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
    let file = new_eventfd(current_task, value, eventfd_type, blocking);
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
        not_implemented!(current_task, "timerfd_create: flags 0x{:x}", flags);
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

    let timer = TimerFile::new_file(current_task, open_flags)?;
    let fd = current_task.files.add_with_flags(timer, fd_flags)?;
    Ok(fd)
}

pub fn sys_timerfd_gettime(
    current_task: &CurrentTask,
    fd: FdNumber,
    user_current_value: UserRef<itimerspec>,
) -> Result<(), Errno> {
    let file = current_task.files.get(fd)?;
    let timer_file = file.downcast_file::<TimerFile>().ok_or_else(|| errno!(EBADF))?;
    let timer_info = timer_file.current_timer_spec();
    current_task.mm.write_object(user_current_value, &timer_info)?;

    Ok(())
}

pub fn sys_timerfd_settime(
    current_task: &CurrentTask,
    fd: FdNumber,
    flags: u32,
    user_new_value: UserRef<itimerspec>,
    user_old_value: UserRef<itimerspec>,
) -> Result<(), Errno> {
    if flags & !(TFD_TIMER_ABSTIME) != 0 {
        not_implemented!(current_task, "timerfd_settime: flags 0x{:x}", flags);
        return error!(EINVAL);
    }

    let file = current_task.files.get(fd)?;
    let timer_file = file.downcast_file::<TimerFile>().ok_or_else(|| errno!(EBADF))?;

    let new_timer_spec = current_task.mm.read_object(user_new_value)?;
    let old_timer_spec = timer_file.set_timer_spec(new_timer_spec, flags)?;
    if !user_old_value.is_null() {
        current_task.mm.write_object(user_old_value, &old_timer_spec)?;
    }

    Ok(())
}

pub fn sys_pselect6(
    current_task: &mut CurrentTask,
    nfds: u32,
    readfds_addr: UserRef<__kernel_fd_set>,
    writefds_addr: UserRef<__kernel_fd_set>,
    exceptfds_addr: UserRef<__kernel_fd_set>,
    timeout_addr: UserRef<timespec>,
    sigmask_addr: UserRef<pselect6_sigmask>,
) -> Result<i32, Errno> {
    const BITS_PER_BYTE: usize = 8;

    fn sizeof<T>(_: &T) -> usize {
        BITS_PER_BYTE * std::mem::size_of::<T>()
    }
    fn is_fd_set(set: &__kernel_fd_set, fd: usize) -> bool {
        let index = fd / sizeof(&set.fds_bits[0]);
        let remainder = fd % sizeof(&set.fds_bits[0]);
        set.fds_bits[index] & (1 << remainder) > 0
    }
    fn add_fd_to_set(set: &mut __kernel_fd_set, fd: usize) {
        let index = fd / sizeof(&set.fds_bits[0]);
        let remainder = fd % sizeof(&set.fds_bits[0]);

        set.fds_bits[index] |= 1 << remainder;
    }
    let start_time = zx::Time::get_monotonic();
    let read_fd_set = |addr: UserRef<__kernel_fd_set>| {
        if addr.is_null() {
            Ok(Default::default())
        } else {
            current_task.mm.read_object(addr)
        }
    };

    if nfds as usize >= BITS_PER_BYTE * std::mem::size_of::<__kernel_fd_set>() {
        return error!(EINVAL);
    }

    let mut timeout = if timeout_addr.is_null() {
        zx::Duration::INFINITE
    } else {
        let timespec = current_task.mm.read_object(timeout_addr)?;
        duration_from_timespec(timespec)?
    };

    let read_events = POLLRDNORM | POLLRDBAND | POLLIN | POLLHUP | POLLERR;
    let write_events = POLLWRBAND | POLLWRNORM | POLLOUT | POLLERR;
    let except_events = POLLPRI;

    let readfds = read_fd_set(readfds_addr)?;
    let writefds = read_fd_set(writefds_addr)?;
    let exceptfds = read_fd_set(exceptfds_addr)?;

    let sets = &[(read_events, &readfds), (write_events, &writefds), (except_events, &exceptfds)];

    let mut num_fds = 0;
    let file_object = EpollFileObject::new_file(current_task);
    let epoll_file = file_object.downcast_file::<EpollFileObject>().unwrap();
    for fd in 0..nfds {
        let mut aggregated_events = 0;
        for (events, fds) in sets.iter() {
            if is_fd_set(fds, fd as usize) {
                aggregated_events |= events;
            }
        }
        if aggregated_events != 0 {
            let event = EpollEvent { events: aggregated_events, data: fd as u64 };
            let file = current_task.files.get(FdNumber::from_raw(fd as i32))?;
            epoll_file.add(current_task, &file, &file_object, event)?;
            num_fds += 1;
        }
    }

    let mask = if sigmask_addr.is_null() {
        current_task.read().signals.mask()
    } else {
        let sigmask = current_task.mm.read_object(sigmask_addr)?;
        if sigmask.ss.is_null() {
            current_task.read().signals.mask()
        } else {
            if sigmask.ss_len < std::mem::size_of::<sigset_t>() {
                return error!(EINVAL);
            }
            current_task.mm.read_object(sigmask.ss.into())?
        }
    };
    let ready_fds = current_task.wait_with_temporary_mask(mask, |current_task| {
        epoll_file.wait(current_task, num_fds, timeout)
    })?;
    let mut num_fds = 0;
    let mut readfds: __kernel_fd_set = Default::default();
    let mut writefds: __kernel_fd_set = Default::default();
    let mut exceptfds: __kernel_fd_set = Default::default();
    let mut sets = [
        (read_events, &mut readfds),
        (write_events, &mut writefds),
        (except_events, &mut exceptfds),
    ];

    for event in &ready_fds {
        let fd = event.data as usize;
        sets.iter_mut().for_each(|entry| {
            let events = entry.0;
            let fds: &mut __kernel_fd_set = entry.1;
            if events & event.events > 0 {
                add_fd_to_set(fds, fd);
                num_fds += 1;
            }
        });
    }

    let write_fd_set =
        |addr: UserRef<__kernel_fd_set>, value: __kernel_fd_set| -> Result<(), Errno> {
            if !addr.is_null() {
                current_task.mm.write_object(addr, &value)?;
            }
            Ok(())
        };
    write_fd_set(readfds_addr, readfds)?;
    write_fd_set(writefds_addr, writefds)?;
    write_fd_set(exceptfds_addr, exceptfds)?;
    if !timeout_addr.is_null() {
        timeout -= zx::Time::get_monotonic() - start_time;
        timeout = std::cmp::max(timeout, zx::Duration::from_seconds(0));
        current_task.mm.write_object(timeout_addr, &timespec_from_duration(timeout))?;
    }
    Ok(num_fds)
}

pub fn sys_epoll_create(current_task: &CurrentTask, size: i32) -> Result<FdNumber, Errno> {
    if size < 1 {
        return error!(EINVAL);
    }
    sys_epoll_create1(current_task, 0)
}

pub fn sys_epoll_create1(current_task: &CurrentTask, flags: u32) -> Result<FdNumber, Errno> {
    if flags & !EPOLL_CLOEXEC != 0 {
        return error!(EINVAL);
    }
    let ep_file = EpollFileObject::new_file(current_task);
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
) -> Result<(), Errno> {
    if epfd == fd {
        return error!(EINVAL);
    }

    let file = current_task.files.get(epfd)?;
    let epoll_file = file.downcast_file::<EpollFileObject>().ok_or_else(|| errno!(EINVAL))?;

    let ctl_file = current_task.files.get(fd)?;
    match op {
        EPOLL_CTL_ADD => {
            let epoll_event = current_task.mm.read_object(event)?;
            epoll_file.add(current_task, &ctl_file, &file, epoll_event)?;
        }
        EPOLL_CTL_MOD => {
            let epoll_event = current_task.mm.read_object(event)?;
            epoll_file.modify(current_task, &ctl_file, epoll_event)?;
        }
        EPOLL_CTL_DEL => epoll_file.delete(current_task, &ctl_file)?,
        _ => return error!(EINVAL),
    }
    Ok(())
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

    let timeout = duration_from_poll_timeout(timeout)?;
    let file = current_task.files.get(epfd)?;
    let epoll_file = file.downcast_file::<EpollFileObject>().ok_or_else(|| errno!(EINVAL))?;

    let active_events = if !user_sigmask.is_null() {
        let signal_mask = current_task.mm.read_object(user_sigmask)?;
        current_task.wait_with_temporary_mask(signal_mask, |current_task| {
            epoll_file.wait(current_task, max_events, timeout)
        })?
    } else {
        epoll_file.wait(current_task, max_events, timeout)?
    };

    let mut event_ref = events;
    for event in active_events.iter() {
        current_task.mm.write_object(UserRef::new(event_ref.addr()), event)?;
        event_ref = event_ref.next();
    }

    Ok(active_events.len())
}

struct ReadyPollItem {
    index: usize,
    events: u32,
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

    let timeout = duration_from_poll_timeout(timeout)?;
    let mut pollfds = vec![pollfd::default(); num_fds as usize];
    let ready_items = Arc::new(Mutex::new(Vec::<ReadyPollItem>::new()));
    let waiter = Waiter::new();

    for (index, poll_descriptor) in pollfds.iter_mut().enumerate() {
        *poll_descriptor = current_task.mm.read_object(user_pollfds.at(index))?;
        poll_descriptor.revents = 0;
        if poll_descriptor.fd < 0 {
            continue;
        }
        let file = current_task.files.get(FdNumber::from_raw(poll_descriptor.fd as i32))?;
        let handler_ready_items = ready_items.clone();
        let handler = move |observed: FdEvents| {
            handler_ready_items.lock().push(ReadyPollItem { index, events: observed.mask() });
        };

        let sought_events =
            FdEvents::from(poll_descriptor.events as u32) | FdEvents::POLLERR | FdEvents::POLLHUP;
        file.wait_async(
            current_task,
            &waiter,
            sought_events,
            Box::new(handler),
            WaitAsyncOptions::empty(),
        );
    }

    let mask = mask.unwrap_or_else(|| current_task.read().signals.mask());
    match current_task.wait_with_temporary_mask(mask, |current_task| {
        waiter.wait_until(current_task, zx::Time::after(timeout))
    }) {
        Err(err) if err == ETIMEDOUT => {}
        result => result?,
    }

    let ready_items = ready_items.lock();
    for ready_item in ready_items.iter() {
        let interested_events = FdEvents::from(pollfds[ready_item.index].events as u32)
            | FdEvents::POLLERR
            | FdEvents::POLLHUP;
        let return_events = interested_events.mask() & ready_item.events;
        pollfds[ready_item.index].revents = return_events as i16;
    }

    for (index, poll_descriptor) in pollfds.iter().enumerate() {
        current_task.mm.write_object(user_pollfds.at(index), poll_descriptor)?;
    }

    Ok(ready_items.len())
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
        let ts = current_task.mm.read_object(user_timespec)?;
        duration_from_timespec(ts)?.into_millis() as i32
    };

    let start_time = zx::Time::get_monotonic();

    let mask = if !user_mask.is_null() {
        let mask = current_task.mm.read_object(user_mask)?;
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
    let remaining_timespec = timespec_from_duration(remaining_duration);

    // From gVisor: "ppoll is normally restartable if interrupted by something other than a signal
    // handled by the application (i.e. returns ERESTARTNOHAND). However, if
    // [copy out] failed, then the restarted ppoll would use the wrong timeout, so the
    // error should be left as EINTR."
    match (current_task.mm.write_object(user_timespec, &remaining_timespec), poll_result) {
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

pub fn sys_flock(current_task: &CurrentTask, fd: FdNumber, operation: u32) -> Result<(), Errno> {
    let file = current_task.files.get(fd)?;
    let operation = FlockOperation::from_flags(operation)?;
    file.name.entry.node.flock(current_task, &file, operation)
}

pub fn sys_fsync(current_task: &CurrentTask, fd: FdNumber) -> Result<(), Errno> {
    let _file = current_task.files.get(fd)?;
    not_implemented!(current_task, "fsync");
    Ok(())
}

pub fn sys_fdatasync(current_task: &CurrentTask, fd: FdNumber) -> Result<(), Errno> {
    let _file = current_task.files.get(fd)?;
    not_implemented!(current_task, "fdatasync");
    Ok(())
}

pub fn sys_fallocate(
    current_task: &CurrentTask,
    fd: FdNumber,
    mode: u32,
    offset: off_t,
    len: off_t,
) -> Result<(), Errno> {
    let _file = current_task.files.get(fd)?;
    not_implemented!(current_task, "fallocate({}, {}, {}, {})", fd, mode, offset, len);
    Ok(())
}

pub fn sys_inotify_init(current_task: &CurrentTask) -> Result<FdNumber, Errno> {
    sys_inotify_init1(current_task, 0)
}

pub fn sys_inotify_init1(current_task: &CurrentTask, flags: u32) -> Result<FdNumber, Errno> {
    not_implemented!(current_task, "inotify_init1({})", flags);
    if flags & !(IN_NONBLOCK | IN_CLOEXEC) != 0 {
        return error!(EINVAL);
    }
    let non_blocking = flags & IN_NONBLOCK != 0;
    let close_on_exec = flags & IN_CLOEXEC != 0;
    let ep_file = InotifyFileObject::new_file(current_task, non_blocking);
    let fd_flags = if close_on_exec { FdFlags::CLOEXEC } else { FdFlags::empty() };
    let fd = current_task.files.add_with_flags(ep_file, fd_flags)?;
    Ok(fd)
}

pub fn sys_inotify_add_watch(
    current_task: &CurrentTask,
    fd: FdNumber,
    user_path: UserCString,
    mask: u32,
) -> Result<u32, Errno> {
    not_implemented!(current_task, "sys_inotify_add_watch({}, {}, {})", fd, user_path, mask);
    Ok(1)
}

pub fn sys_inotify_rm_watch(
    current_task: &CurrentTask,
    fd: FdNumber,
    wd: u32,
) -> Result<(), Errno> {
    not_implemented!(current_task, "sys_inotify_rm_watch({}, {})", fd, wd);
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::mm::PAGE_SIZE;
    use crate::testing::*;
    use std::sync::Arc;

    #[::fuchsia::test]
    fn test_sys_lseek() -> Result<(), Errno> {
        let (_kernel, current_task) = create_kernel_and_task_with_pkgfs();
        let fd = FdNumber::from_raw(10);
        let file_handle = current_task.open_file(b"data/testfile.txt", OpenFlags::RDONLY)?;
        let file_size = file_handle.node().stat().unwrap().st_size;
        current_task.files.insert(fd, file_handle);

        assert_eq!(sys_lseek(&current_task, fd, 0, SeekOrigin::Cur as u32)?, 0);
        assert_eq!(sys_lseek(&current_task, fd, 1, SeekOrigin::Cur as u32)?, 1);
        assert_eq!(sys_lseek(&current_task, fd, 3, SeekOrigin::Set as u32)?, 3);
        assert_eq!(sys_lseek(&current_task, fd, -3, SeekOrigin::Cur as u32)?, 0);
        assert_eq!(sys_lseek(&current_task, fd, 0, SeekOrigin::End as u32)?, file_size);
        assert_eq!(sys_lseek(&current_task, fd, -5, SeekOrigin::Set as u32), error!(EINVAL));

        // Make sure that the failed call above did not change the offset.
        assert_eq!(sys_lseek(&current_task, fd, 0, SeekOrigin::Cur as u32)?, file_size);

        // Prepare for an overflow.
        assert_eq!(sys_lseek(&current_task, fd, 3, SeekOrigin::Set as u32)?, 3);

        // Check for overflow.
        assert_eq!(sys_lseek(&current_task, fd, i64::MAX, SeekOrigin::Cur as u32), error!(EINVAL));

        Ok(())
    }

    #[::fuchsia::test]
    fn test_sys_dup() -> Result<(), Errno> {
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

    #[::fuchsia::test]
    fn test_sys_dup3() -> Result<(), Errno> {
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

    #[::fuchsia::test]
    fn test_sys_dup2() {
        // Most tests are handled by test_sys_dup3, only test the case where both fds are equals.
        let (_kernel, current_task) = create_kernel_and_task_with_pkgfs();
        let fd = FdNumber::from_raw(42);
        assert_eq!(sys_dup2(&current_task, fd, fd), error!(EBADF));
        let file_handle =
            current_task.open_file(b"data/testfile.txt", OpenFlags::RDONLY).expect("open_file");
        let files = &current_task.files;
        let fd = files.add(file_handle).expect("add");
        assert_eq!(sys_dup2(&current_task, fd, fd), Ok(fd));
    }

    #[::fuchsia::test]
    fn test_sys_open_cloexec() -> Result<(), Errno> {
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

    #[::fuchsia::test]
    fn test_sys_epoll() -> Result<(), Errno> {
        let (_kernel, current_task) = create_kernel_and_task_with_pkgfs();

        let epoll_fd = sys_epoll_create1(&current_task, 0).expect("sys_epoll_create1 failed");
        sys_close(&current_task, epoll_fd).expect("sys_close failed");

        Ok(())
    }

    #[::fuchsia::test]
    fn test_fstat_tmp_file() {
        let (_kernel, current_task) = create_kernel_and_task_with_pkgfs();

        // Create the file that will be used to stat.
        let file_path = b"data/testfile.txt";
        let _file_handle = current_task.open_file(file_path, OpenFlags::RDONLY).unwrap();

        // Write the path to user memory.
        let path_addr = map_memory(&current_task, UserAddress::default(), *PAGE_SIZE);
        current_task.mm.write_memory(path_addr, file_path).expect("failed to clear struct");

        let user_stat = UserRef::new(path_addr + file_path.len());
        current_task
            .mm
            .write_object(user_stat, &statfs::default(0))
            .expect("failed to clear struct");

        let user_path = UserCString::new(path_addr);

        assert_eq!(sys_statfs(&current_task, user_path, user_stat), Ok(()));

        let returned_stat = current_task.mm.read_object(user_stat).expect("failed to read struct");
        assert_eq!(returned_stat, statfs::default(u32::from_be_bytes(*b"f.io")));
    }
}

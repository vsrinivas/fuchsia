// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use bitflags::bitflags;
use fuchsia_zircon as zx;
use parking_lot::{Mutex, RwLock};
use std::collections::HashMap;
use std::fmt;
use std::sync::Arc;
use zerocopy::{AsBytes, FromBytes};

use crate::fs::*;
use crate::not_implemented;
use crate::syscalls::SyscallResult;
use crate::task::*;
use crate::types::*;

pub use starnix_macros::FileObject;

#[derive(Hash, PartialEq, Eq, Debug, Copy, Clone, AsBytes, FromBytes)]
#[repr(transparent)]
pub struct FdNumber(i32);

impl FdNumber {
    pub const AT_FDCWD: FdNumber = FdNumber(AT_FDCWD);

    pub fn from_raw(n: i32) -> FdNumber {
        FdNumber(n)
    }
    pub fn raw(&self) -> i32 {
        self.0
    }
}

impl fmt::Display for FdNumber {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "fd({})", self.0)
    }
}

/// Corresponds to struct file_operations in Linux, plus any filesystem-specific data.
pub trait FileOps: Send + Sync {
    /// Read from the file without an offset. If your file is seekable, consider implementing this
    /// with fd_impl_seekable.
    fn read(&self, fd: &FileObject, task: &Task, data: &[iovec_t]) -> Result<usize, Errno>;
    /// Read from the file at an offset. If your file is seekable, consider implementing this with
    /// fd_impl_nonseekable!.
    fn read_at(
        &self,
        fd: &FileObject,
        task: &Task,
        offset: usize,
        data: &[iovec_t],
    ) -> Result<usize, Errno>;
    /// Write to the file without an offset. If your file is seekable, consider implementing this
    /// with fd_impl_seekable!.
    fn write(&self, fd: &FileObject, task: &Task, data: &[iovec_t]) -> Result<usize, Errno>;
    /// Write to the file at a offset. If your file is nonseekable, consider implementing this with
    /// fd_impl_nonseekable!.
    fn write_at(
        &self,
        fd: &FileObject,
        task: &Task,
        offset: usize,
        data: &[iovec_t],
    ) -> Result<usize, Errno>;

    /// Responds to an mmap call by returning a VMO. At least the requested protection flags must
    /// be set on the VMO. Reading or writing the VMO must read or write the file. If this is not
    /// possible given the requested protection, an error must be returned.
    fn get_vmo(
        &self,
        _fd: &FileObject,
        _task: &Task,
        _prot: zx::VmarFlags,
        _flags: u32,
    ) -> Result<zx::Vmo, Errno> {
        Err(ENODEV)
    }

    // TODO(tbodt): This is actually an operation of the filesystem and not the file descriptor: if
    // you open a device file, fstat will go to the filesystem, not to the device. It's only here
    // because we don't have such a thing yet. Will need to be moved.
    fn fstat(&self, fd: &FileObject, task: &Task) -> Result<stat_t, Errno>;

    fn ioctl(
        &self,
        _fd: &FileObject,
        _task: &Task,
        request: u32,
        _in_addr: UserAddress,
        _out_addr: UserAddress,
    ) -> Result<SyscallResult, Errno> {
        default_ioctl(request)
    }
}

/// Implements FileDesc methods in a way that makes sense for nonseekable files. You must implement
/// read and write.
#[macro_export]
macro_rules! fd_impl_nonseekable {
    () => {
        fn read_at(
            &self,
            _fd: &FileObject,
            _task: &Task,
            _offset: usize,
            _data: &[iovec_t],
        ) -> Result<usize, Errno> {
            Err(ESPIPE)
        }
        fn write_at(
            &self,
            _fd: &FileObject,
            _task: &Task,
            _offset: usize,
            _data: &[iovec_t],
        ) -> Result<usize, Errno> {
            Err(ESPIPE)
        }
    };
}

/// Implements FileDesc methods in a way that makes sense for seekable files. You must implement
/// read_at and write_at.
#[macro_export]
macro_rules! fd_impl_seekable {
    () => {
        fn read(&self, fd: &FileObject, task: &Task, data: &[iovec_t]) -> Result<usize, Errno> {
            let mut offset = fd.offset.lock();
            let size = self.read_at(fd, task, *offset, data)?;
            *offset += size;
            Ok(size)
        }
        fn write(&self, fd: &FileObject, task: &Task, data: &[iovec_t]) -> Result<usize, Errno> {
            let mut offset = fd.offset.lock();
            let size = self.write_at(fd, task, *offset, data)?;
            *offset += size;
            Ok(size)
        }
    };
}

pub fn default_ioctl(request: u32) -> Result<SyscallResult, Errno> {
    not_implemented!("ioctl: request=0x{:x}", request);
    Err(ENOTTY)
}

/// Corresponds to struct file in Linux.
pub struct FileObject {
    ops: Box<dyn FileOps>,
    pub node: Option<FsNodeHandle>,
    pub offset: Mutex<usize>,
    pub async_owner: Mutex<pid_t>,
}

impl FileObject {
    pub fn new<T: FileOps + 'static>(ops: T) -> FileHandle {
        Self::new_with_node(Box::new(ops), None)
    }
    pub fn new_with_node(ops: Box<dyn FileOps>, node: Option<FsNodeHandle>) -> FileHandle {
        Arc::new(Self { node, ops, offset: Mutex::new(0), async_owner: Mutex::new(0) })
    }

    pub fn ops(&self) -> &dyn FileOps {
        &*self.ops
    }

    /// Get the async owner of this file.
    ///
    /// See fcntl(F_GETOWN)
    pub fn get_async_owner(&self) -> pid_t {
        *self.async_owner.lock()
    }

    /// Set the async owner of this file.
    ///
    /// See fcntl(F_SETOWN)
    pub fn set_async_owner(&self, owner: pid_t) {
        *self.async_owner.lock() = owner;
    }
}

pub type FileHandle = Arc<FileObject>;

bitflags! {
    pub struct FdFlags: u32 {
        const CLOEXEC = FD_CLOEXEC;
    }
}

#[derive(Clone)]
struct FdTableEntry {
    file: FileHandle,

    // Rather than using a separate "flags" field, we could maintain this data
    // as a bitfield over the file descriptors because there is only one flag
    // currently (CLOEXEC) and file descriptor numbers tend to cluster near 0.
    flags: FdFlags,
}

impl FdTableEntry {
    fn new(file: FileHandle) -> FdTableEntry {
        FdTableEntry { file, flags: FdFlags::empty() }
    }
}

pub struct FdTable {
    table: RwLock<HashMap<FdNumber, FdTableEntry>>,
}

impl FdTable {
    pub fn new() -> Arc<FdTable> {
        Arc::new(FdTable { table: RwLock::new(HashMap::new()) })
    }

    pub fn fork(&self) -> Arc<FdTable> {
        Arc::new(FdTable { table: RwLock::new(self.table.read().clone()) })
    }

    pub fn exec(&self) {
        let mut table = self.table.write();
        table.retain(|_fd, entry| !entry.flags.contains(FdFlags::CLOEXEC));
    }

    pub fn insert(&self, fd: FdNumber, file: FileHandle) {
        let mut table = self.table.write();
        table.insert(fd, FdTableEntry::new(file));
    }

    pub fn install_fd(&self, file: FileHandle) -> Result<FdNumber, Errno> {
        let mut table = self.table.write();
        let mut fd = FdNumber::from_raw(0);
        while table.contains_key(&fd) {
            fd = FdNumber::from_raw(fd.raw() + 1);
        }
        table.insert(fd, FdTableEntry::new(file));
        Ok(fd)
    }

    pub fn get(&self, fd: FdNumber) -> Result<FileHandle, Errno> {
        let table = self.table.read();
        table.get(&fd).map(|entry| entry.file.clone()).ok_or(EBADF)
    }

    pub fn close(&self, fd: FdNumber) -> Result<(), Errno> {
        let mut table = self.table.write();
        table.remove(&fd).ok_or(EBADF).map(|_| {})
    }

    pub fn get_flags(&self, fd: FdNumber) -> Result<FdFlags, Errno> {
        let table = self.table.read();
        table.get(&fd).map(|entry| entry.flags).ok_or(EBADF)
    }

    pub fn set_flags(&self, fd: FdNumber, flags: FdFlags) -> Result<(), Errno> {
        let mut table = self.table.write();
        table
            .get_mut(&fd)
            .map(|entry| {
                entry.flags = flags;
            })
            .ok_or(EBADF)
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::fs::SyslogFile;

    #[test]
    fn test_fd_table_install() {
        let files = FdTable::new();
        let file = SyslogFile::new();

        let fd0 = files.install_fd(file.clone()).unwrap();
        assert_eq!(fd0.raw(), 0);
        let fd1 = files.install_fd(file.clone()).unwrap();
        assert_eq!(fd1.raw(), 1);

        assert!(Arc::ptr_eq(&files.get(fd0).unwrap(), &file));
        assert!(Arc::ptr_eq(&files.get(fd1).unwrap(), &file));
        assert_eq!(files.get(FdNumber::from_raw(fd1.raw() + 1)).map(|_| ()), Err(EBADF));
    }

    #[test]
    fn test_fd_table_fork() {
        let files = FdTable::new();
        let file = SyslogFile::new();

        let fd0 = files.install_fd(file.clone()).unwrap();
        let fd1 = files.install_fd(file.clone()).unwrap();
        let fd2 = FdNumber::from_raw(2);

        let forked = files.fork();

        assert_eq!(Arc::as_ptr(&files.get(fd0).unwrap()), Arc::as_ptr(&forked.get(fd0).unwrap()));
        assert_eq!(Arc::as_ptr(&files.get(fd1).unwrap()), Arc::as_ptr(&forked.get(fd1).unwrap()));
        assert!(files.get(fd2).is_err());
        assert!(forked.get(fd2).is_err());

        files.set_flags(fd0, FdFlags::CLOEXEC).unwrap();
        assert_eq!(FdFlags::CLOEXEC, files.get_flags(fd0).unwrap());
        assert_ne!(FdFlags::CLOEXEC, forked.get_flags(fd0).unwrap());
    }

    #[test]
    fn test_fd_table_exec() {
        let files = FdTable::new();
        let file = SyslogFile::new();

        let fd0 = files.install_fd(file.clone()).unwrap();
        let fd1 = files.install_fd(file.clone()).unwrap();

        files.set_flags(fd0, FdFlags::CLOEXEC).unwrap();

        assert!(files.get(fd0).is_ok());
        assert!(files.get(fd1).is_ok());

        files.exec();

        assert!(!files.get(fd0).is_ok());
        assert!(files.get(fd1).is_ok());
    }

    #[test]
    fn test_fd_table_pack_values() {
        let files = FdTable::new();
        let file = SyslogFile::new();

        // Add two FDs.
        let fd0 = files.install_fd(file.clone()).unwrap();
        let fd1 = files.install_fd(file.clone()).unwrap();
        assert_eq!(fd0.raw(), 0);
        assert_eq!(fd1.raw(), 1);

        // Close FD 0
        assert!(files.close(fd0).is_ok());
        assert!(files.close(fd0).is_err());
        // Now it's gone.
        assert!(files.get(fd0).is_err());

        // The next FD we insert fills in the hole we created.
        let another_fd = files.install_fd(file.clone()).unwrap();
        assert_eq!(another_fd.raw(), 0);
    }
}

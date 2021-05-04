// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use bitflags::bitflags;
use fuchsia_zircon as zx;
use parking_lot::{Mutex, RwLock};
use std::collections::HashMap;
use std::ops::Deref;
use std::sync::Arc;

use crate::not_implemented;
use crate::task::*;
use crate::uapi::*;

pub use starnix_macros::FileObject;

#[derive(Hash, PartialEq, Eq, Debug, Copy, Clone)]
pub struct FileDescriptor(i32);
impl FileDescriptor {
    pub fn from_raw(n: i32) -> FileDescriptor {
        FileDescriptor(n)
    }
    pub fn raw(&self) -> i32 {
        self.0
    }
}

/// Corresponds to struct file_operations in Linux, plus any filesystem-specific data.
pub trait FileObject: Deref<Target = FileCommon> {
    /// Read from the file without an offset. If your file is seekable, consider implementing this
    /// with fd_impl_seekable.
    fn read(&self, task: &Task, data: &[iovec_t]) -> Result<usize, Errno>;
    /// Read from the file at an offset. If your file is seekable, consider implementing this with
    /// fd_impl_nonseekable!.
    fn read_at(&self, _task: &Task, _offset: usize, _data: &[iovec_t]) -> Result<usize, Errno>;
    /// Write to the file without an offset. If your file is seekable, consider implementing this
    /// with fd_impl_seekable!.
    fn write(&self, task: &Task, data: &[iovec_t]) -> Result<usize, Errno>;
    /// Write to the file at a offset. If your file is nonseekable, consider implementing this with
    /// fd_impl_nonseekable!.
    fn write_at(&self, _task: &Task, _offset: usize, data: &[iovec_t]) -> Result<usize, Errno>;

    /// Responds to an mmap call by returning a VMO. At least the requested protection flags must
    /// be set on the VMO. Reading or writing the VMO must read or write the file. If this is not
    /// possible given the requested protection, an error must be returned.
    fn get_vmo(&self, _task: &Task, _prot: zx::VmarFlags, _flags: u32) -> Result<zx::Vmo, Errno> {
        Err(ENODEV)
    }

    // TODO(tbodt): This is actually an operation of the filesystem and not the file descriptor: if
    // you open a device file, fstat will go to the filesystem, not to the device. It's only here
    // because we don't have such a thing yet. Will need to be moved.
    fn fstat(&self, task: &Task) -> Result<stat_t, Errno>;

    fn ioctl(
        &self,
        task: &Task,
        request: u32,
        in_addr: UserAddress,
        out_addr: UserAddress,
    ) -> Result<SyscallResult, Errno>;

    /// Get the async owner of this file.
    ///
    /// See fcntl(F_GETOWN)
    fn get_async_owner(&self) -> pid_t;

    /// Set the async owner of this file.
    ///
    /// See fcntl(F_SETOWN)
    fn set_async_owner(&self, owner: pid_t);
}

/// Implements FileDesc methods in a way that makes sense for nonseekable files. You must implement
/// read and write.
#[macro_export]
macro_rules! fd_impl_nonseekable {
    () => {
        fn read_at(&self, _task: &Task, _offset: usize, _data: &[iovec_t]) -> Result<usize, Errno> {
            Err(ESPIPE)
        }
        fn write_at(
            &self,
            _task: &Task,
            _offset: usize,
            _data: &[iovec_t],
        ) -> Result<usize, Errno> {
            Err(ESPIPE)
        }
        fn get_async_owner(&self) -> pid_t {
            self.common.get_async_owner()
        }
        fn set_async_owner(&self, owner: pid_t) {
            self.common.set_async_owner(owner)
        }
    };
}

/// Implements FileDesc methods in a way that makes sense for seekable files. You must implement
/// read_at and write_at.
#[macro_export]
macro_rules! fd_impl_seekable {
    () => {
        fn read(&self, task: &Task, data: &[iovec_t]) -> Result<usize, Errno> {
            let mut offset = self.offset.lock();
            let size = self.read_at(task, *offset, data)?;
            *offset += size;
            Ok(size)
        }
        fn write(&self, task: &Task, data: &[iovec_t]) -> Result<usize, Errno> {
            let mut offset = self.offset.lock();
            let size = self.write_at(task, *offset, data)?;
            *offset += size;
            Ok(size)
        }
        fn get_async_owner(&self) -> pid_t {
            self.common.get_async_owner()
        }
        fn set_async_owner(&self, owner: pid_t) {
            self.common.set_async_owner(owner)
        }
    };
}

/// Corresponds to struct file in Linux.
#[derive(Default)]
pub struct FileCommon {
    pub offset: Mutex<usize>,
    pub async_owner: Mutex<pid_t>,
}

impl FileCommon {
    pub fn get_async_owner(&self) -> pid_t {
        *self.async_owner.lock()
    }

    pub fn set_async_owner(&self, owner: pid_t) {
        *self.async_owner.lock() = owner;
    }

    pub fn ioctl(
        &self,
        _task: &Task,
        request: u32,
        _in_addr: UserAddress,
        _out_addr: UserAddress,
    ) -> Result<SyscallResult, Errno> {
        not_implemented!("ioctl: request=0x{:x}", request);
        Err(ENOTTY)
    }
}

pub type FileHandle = Arc<dyn FileObject + Send + Sync>;

bitflags! {
    pub struct FdFlags: u32 {
        const CLOEXEC = FD_CLOEXEC;
    }
}

struct FdTableEntry {
    file: FileHandle,

    // Rather than using a separate "flags" field, we could maintain this data
    // as a bitfield over the file descriptors because there is only one flag
    // currently (CLOEXEC) and file descriptor numbers tend to cluster near 0.
    flags: FdFlags,
}

pub struct FdTable {
    table: RwLock<HashMap<FileDescriptor, FdTableEntry>>,
}

impl FdTable {
    pub fn new() -> FdTable {
        FdTable { table: RwLock::new(HashMap::new()) }
    }

    pub fn install_fd(&self, file: FileHandle) -> Result<FileDescriptor, Errno> {
        let mut table = self.table.write();
        let mut fd = FileDescriptor::from_raw(0);
        while table.contains_key(&fd) {
            fd = FileDescriptor::from_raw(fd.raw() + 1);
        }
        table.insert(fd, FdTableEntry { file, flags: FdFlags::empty() });
        Ok(fd)
    }

    pub fn get(&self, fd: FileDescriptor) -> Result<FileHandle, Errno> {
        let table = self.table.read();
        table.get(&fd).map(|entry| entry.file.clone()).ok_or_else(|| EBADF)
    }

    pub fn get_flags(&self, fd: FileDescriptor) -> Result<FdFlags, Errno> {
        let table = self.table.read();
        table.get(&fd).map(|entry| entry.flags).ok_or_else(|| EBADF)
    }

    pub fn set_flags(&self, fd: FileDescriptor, flags: FdFlags) -> Result<(), Errno> {
        let mut table = self.table.write();
        table
            .get_mut(&fd)
            .map(|entry| {
                entry.flags = flags;
            })
            .ok_or_else(|| EBADF)
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::fs::SyslogFile;

    #[test]
    fn test_fd_table_install() {
        let table = FdTable::new();
        let test_file = SyslogFile::new();

        let fd_num = table.install_fd(test_file.clone()).unwrap();
        assert_eq!(fd_num.raw(), 0);
        let fd_num2 = table.install_fd(test_file.clone()).unwrap();
        assert_eq!(fd_num2.raw(), 1);

        assert!(Arc::ptr_eq(&table.get(fd_num).unwrap(), &test_file));
        assert!(Arc::ptr_eq(&table.get(fd_num2).unwrap(), &test_file));
        assert_eq!(table.get(FileDescriptor::from_raw(fd_num2.raw() + 1)).map(|_| ()), Err(EBADF));
    }
}

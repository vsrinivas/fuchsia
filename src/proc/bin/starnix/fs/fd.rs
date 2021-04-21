// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon as zx;
use parking_lot::{Mutex, RwLock};
pub use starnix_macros::FileDesc;
use std::collections::HashMap;
use std::ops::Deref;
use std::sync::Arc;

use crate::uapi::*;
use crate::ThreadContext;

#[derive(Hash, PartialEq, Eq, Debug, Copy, Clone)]
pub struct FdNumber(pub i32);
impl FdNumber {
    pub fn from_raw(n: i32) -> FdNumber {
        FdNumber(n)
    }
    pub fn raw(&self) -> i32 {
        self.0
    }
}

/// Corresponds to struct file_operations in Linux, plus any filesystem-specific data.
pub trait FileDesc: Deref<Target = FileCommon> {
    /// Read from the file without an offset. If your file is seekable, consider implementing this
    /// with fd_impl_seekable.
    fn read(&self, ctx: &ThreadContext, data: &[iovec_t]) -> Result<usize, Errno>;
    /// Read from the file at an offset. If your file is seekable, consider implementing this with
    /// fd_impl_nonseekable!.
    fn read_at(
        &self,
        _ctx: &ThreadContext,
        _offset: usize,
        _data: &[iovec_t],
    ) -> Result<usize, Errno>;
    /// Write to the file without an offset. If your file is seekable, consider implementing this
    /// with fd_impl_seekable!.
    fn write(&self, ctx: &ThreadContext, data: &[iovec_t]) -> Result<usize, Errno>;
    /// Write to the file at a offset. If your file is nonseekable, consider implementing this with
    /// fd_impl_nonseekable!.
    fn write_at(
        &self,
        _ctx: &ThreadContext,
        _offset: usize,
        data: &[iovec_t],
    ) -> Result<usize, Errno>;

    /// Responds to an mmap call by returning a VMO. At least the requested protection flags must
    /// be set on the VMO. Reading or writing the VMO must read or write the file. If this is not
    /// possible given the requested protection, an error must be returned.
    fn get_vmo(
        &self,
        _ctx: &ThreadContext,
        _prot: zx::VmarFlags,
        _flags: u32,
    ) -> Result<zx::Vmo, Errno> {
        Err(ENODEV)
    }

    // TODO(tbodt): This is actually an operation of the filesystem and not the file descriptor: if
    // you open a device file, fstat will go to the filesystem, not to the device. It's only here
    // because we don't have such a thing yet. Will need to be moved.
    fn fstat(&self, ctx: &ThreadContext) -> Result<stat_t, Errno>;
}

/// Implements FileDesc methods in a way that makes sense for nonseekable files. You must implement
/// read and write.
#[macro_export]
macro_rules! fd_impl_nonseekable {
    () => {
        fn read_at(
            &self,
            _ctx: &ThreadContext,
            _offset: usize,
            _data: &[iovec_t],
        ) -> Result<usize, Errno> {
            Err(ESPIPE)
        }
        fn write_at(
            &self,
            _ctx: &ThreadContext,
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
        fn read(&self, ctx: &ThreadContext, data: &[iovec_t]) -> Result<usize, Errno> {
            let mut offset = self.offset.lock();
            let size = self.read_at(ctx, *offset, data)?;
            *offset += size;
            Ok(size)
        }
        fn write(&self, ctx: &ThreadContext, data: &[iovec_t]) -> Result<usize, Errno> {
            let mut offset = self.offset.lock();
            let size = self.write_at(ctx, *offset, data)?;
            *offset += size;
            Ok(size)
        }
    };
}

/// Corresponds to struct file in Linux.
#[derive(Default)]
pub struct FileCommon {
    pub offset: Mutex<usize>,
}

pub type FdHandle = Arc<dyn FileDesc>;

pub struct FdTable {
    table: RwLock<HashMap<FdNumber, FdHandle>>,
}

impl FdTable {
    pub fn new() -> FdTable {
        FdTable { table: RwLock::new(HashMap::new()) }
    }

    pub fn install_fd(&self, fd: FdHandle) -> Result<FdNumber, Errno> {
        let mut table = self.table.write();
        let mut n = FdNumber::from_raw(0);
        while table.contains_key(&n) {
            n = FdNumber::from_raw(n.raw() + 1);
        }
        table.insert(n, fd);
        Ok(n)
    }

    pub fn get(&self, n: FdNumber) -> Result<FdHandle, Errno> {
        let table = self.table.read();
        table.get(&n).map(|handle| handle.clone()).ok_or_else(|| EBADF)
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
        assert_eq!(table.get(FdNumber::from_raw(fd_num2.raw() + 1)).map(|_| ()), Err(EBADF));
    }
}

// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use bitflags::bitflags;
use parking_lot::RwLock;
use std::collections::HashMap;
use std::sync::Arc;

use crate::errno;
use crate::fs::*;
use crate::types::*;

bitflags! {
    pub struct FdFlags: u32 {
        const CLOEXEC = FD_CLOEXEC;
    }
}

#[derive(Clone)]
pub struct FdTableEntry {
    pub file: FileHandle,

    // Rather than using a separate "flags" field, we could maintain this data
    // as a bitfield over the file descriptors because there is only one flag
    // currently (CLOEXEC) and file descriptor numbers tend to cluster near 0.
    flags: FdFlags,
}

impl FdTableEntry {
    fn new(file: FileHandle, flags: FdFlags) -> FdTableEntry {
        FdTableEntry { file, flags }
    }
}

#[derive(Default)]
pub struct FdTable {
    table: RwLock<HashMap<FdNumber, FdTableEntry>>,
}

impl FdTable {
    pub fn new() -> Arc<FdTable> {
        Arc::new(FdTable::default())
    }

    pub fn fork(&self) -> Arc<FdTable> {
        Arc::new(FdTable { table: RwLock::new(self.table.read().clone()) })
    }

    pub fn exec(&self) {
        let mut table = self.table.write();
        table.retain(|_fd, entry| !entry.flags.contains(FdFlags::CLOEXEC));
    }

    pub fn insert(&self, fd: FdNumber, file: FileHandle) {
        self.insert_with_flags(fd, file, FdFlags::empty())
    }

    pub fn insert_with_flags(&self, fd: FdNumber, file: FileHandle, flags: FdFlags) {
        let mut table = self.table.write();
        table.insert(fd, FdTableEntry::new(file, flags));
    }

    #[cfg(test)]
    pub fn add(&self, file: FileHandle) -> Result<FdNumber, Errno> {
        self.add_with_flags(file, FdFlags::empty())
    }

    pub fn add_with_flags(&self, file: FileHandle, flags: FdFlags) -> Result<FdNumber, Errno> {
        let mut table = self.table.write();
        let fd = self.get_lowest_available_fd(&*table);
        table.insert(fd, FdTableEntry::new(file, flags));
        Ok(fd)
    }

    // Duplicates a file handle.
    // If newfd does not contain a value, a new FdNumber is allocated. Returns the new FdNumber.
    pub fn duplicate(
        &self,
        oldfd: FdNumber,
        newfd: Option<FdNumber>,
        flags: FdFlags,
    ) -> Result<FdNumber, Errno> {
        // Drop the file object only after releasing the writer lock in case
        // the close() function on the FileOps calls back into the FdTable.
        let _removed_file;
        let result = {
            let mut table = self.table.write();
            let file = table.get(&oldfd).map(|entry| entry.file.clone()).ok_or(errno!(EBADF))?;
            let fd = if let Some(fd) = newfd {
                _removed_file = table.remove(&fd);
                fd
            } else {
                self.get_lowest_available_fd(&*table)
            };
            table.insert(fd, FdTableEntry::new(file, flags));
            Ok(fd)
        };
        result
    }

    pub fn get(&self, fd: FdNumber) -> Result<FileHandle, Errno> {
        let table = self.table.read();
        table.get(&fd).map(|entry| entry.file.clone()).ok_or(errno!(EBADF))
    }

    pub fn close(&self, fd: FdNumber) -> Result<(), Errno> {
        // Drop the file object only after releasing the writer lock in case
        // the close() function on the FileOps calls back into the FdTable.
        let removed = {
            let mut table = self.table.write();
            table.remove(&fd)
        };
        removed.ok_or(errno!(EBADF)).map(|_| {})
    }

    pub fn get_fd_flags(&self, fd: FdNumber) -> Result<FdFlags, Errno> {
        let table = self.table.read();
        table.get(&fd).map(|entry| entry.flags).ok_or(errno!(EBADF))
    }

    pub fn set_fd_flags(&self, fd: FdNumber, flags: FdFlags) -> Result<(), Errno> {
        let mut table = self.table.write();
        table
            .get_mut(&fd)
            .map(|entry| {
                entry.flags = flags;
            })
            .ok_or(errno!(EBADF))
    }

    fn get_lowest_available_fd(&self, table: &HashMap<FdNumber, FdTableEntry>) -> FdNumber {
        let mut fd = FdNumber::from_raw(0);
        while table.contains_key(&fd) {
            fd = FdNumber::from_raw(fd.raw() + 1);
        }
        fd
    }

    /// Returns a vector of all current file descriptors in the table.
    pub fn get_all_fds(&self) -> Vec<FdNumber> {
        self.table.read().keys().cloned().collect()
    }
}

#[cfg(test)]
mod test {
    use super::*;

    use crate::error;
    use crate::fs::fuchsia::SyslogFile;
    use crate::task::*;

    #[test]
    fn test_fd_table_install() {
        let kern = Kernel::new_for_testing();
        let files = FdTable::new();
        let file = SyslogFile::new(&kern);

        let fd0 = files.add(file.clone()).unwrap();
        assert_eq!(fd0.raw(), 0);
        let fd1 = files.add(file.clone()).unwrap();
        assert_eq!(fd1.raw(), 1);

        assert!(Arc::ptr_eq(&files.get(fd0).unwrap(), &file));
        assert!(Arc::ptr_eq(&files.get(fd1).unwrap(), &file));
        assert_eq!(files.get(FdNumber::from_raw(fd1.raw() + 1)).map(|_| ()), error!(EBADF));
    }

    #[test]
    fn test_fd_table_fork() {
        let kern = Kernel::new_for_testing();
        let files = FdTable::new();
        let file = SyslogFile::new(&kern);

        let fd0 = files.add(file.clone()).unwrap();
        let fd1 = files.add(file.clone()).unwrap();
        let fd2 = FdNumber::from_raw(2);

        let forked = files.fork();

        assert_eq!(Arc::as_ptr(&files.get(fd0).unwrap()), Arc::as_ptr(&forked.get(fd0).unwrap()));
        assert_eq!(Arc::as_ptr(&files.get(fd1).unwrap()), Arc::as_ptr(&forked.get(fd1).unwrap()));
        assert!(files.get(fd2).is_err());
        assert!(forked.get(fd2).is_err());

        files.set_fd_flags(fd0, FdFlags::CLOEXEC).unwrap();
        assert_eq!(FdFlags::CLOEXEC, files.get_fd_flags(fd0).unwrap());
        assert_ne!(FdFlags::CLOEXEC, forked.get_fd_flags(fd0).unwrap());
    }

    #[test]
    fn test_fd_table_exec() {
        let kern = Kernel::new_for_testing();
        let files = FdTable::new();
        let file = SyslogFile::new(&kern);

        let fd0 = files.add(file.clone()).unwrap();
        let fd1 = files.add(file.clone()).unwrap();

        files.set_fd_flags(fd0, FdFlags::CLOEXEC).unwrap();

        assert!(files.get(fd0).is_ok());
        assert!(files.get(fd1).is_ok());

        files.exec();

        assert!(!files.get(fd0).is_ok());
        assert!(files.get(fd1).is_ok());
    }

    #[test]
    fn test_fd_table_pack_values() {
        let kern = Kernel::new_for_testing();
        let files = FdTable::new();
        let file = SyslogFile::new(&kern);

        // Add two FDs.
        let fd0 = files.add(file.clone()).unwrap();
        let fd1 = files.add(file.clone()).unwrap();
        assert_eq!(fd0.raw(), 0);
        assert_eq!(fd1.raw(), 1);

        // Close FD 0
        assert!(files.close(fd0).is_ok());
        assert!(files.close(fd0).is_err());
        // Now it's gone.
        assert!(files.get(fd0).is_err());

        // The next FD we insert fills in the hole we created.
        let another_fd = files.add(file.clone()).unwrap();
        assert_eq!(another_fd.raw(), 0);
    }
}

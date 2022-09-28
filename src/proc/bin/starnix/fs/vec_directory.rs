// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::fs::{
    emit_dotdot, fileops_impl_directory, DirectoryEntryType, DirentSink, FileObject, FileOps,
    FsString, SeekOrigin,
};
use crate::task::CurrentTask;
use crate::types::*;

/// A directory entry used for [`VecDirectory`].
#[derive(Debug, PartialEq, Eq, Clone, PartialOrd, Ord)]
pub struct VecDirectoryEntry {
    /// The type of the directory entry (directory, regular, socket, etc).
    pub entry_type: DirectoryEntryType,

    /// The name of the directory entry.
    pub name: FsString,

    /// Optional inode associated with the entry. If `None`, the entry will be auto-assigned one.
    pub inode: Option<ino_t>,
}

/// A FileOps that iterates over a vector of [`VecDirectoryEntry`].
pub struct VecDirectory(Vec<VecDirectoryEntry>);

impl VecDirectory {
    pub fn new_file(entries: Vec<VecDirectoryEntry>) -> Box<dyn FileOps> {
        Box::new(Self(entries))
    }
}

impl FileOps for VecDirectory {
    fileops_impl_directory!();

    fn seek(
        &self,
        file: &FileObject,
        _current_task: &CurrentTask,
        offset: off_t,
        whence: SeekOrigin,
    ) -> Result<off_t, Errno> {
        file.unbounded_seek(offset, whence)
    }

    fn readdir(
        &self,
        file: &FileObject,
        _current_task: &CurrentTask,
        sink: &mut dyn DirentSink,
    ) -> Result<(), Errno> {
        emit_dotdot(file, sink)?;

        // Skip through the entries until the current offset is reached.
        // Subtract 2 from the offset to account for `.` and `..`.
        for entry in self.0.iter().skip(sink.offset() as usize - 2) {
            // Assign an inode if one wasn't set.
            let inode = entry.inode.unwrap_or_else(|| file.fs.next_inode_num());
            sink.add(inode, sink.offset() + 1, entry.entry_type, &entry.name)?;
        }
        Ok(())
    }
}

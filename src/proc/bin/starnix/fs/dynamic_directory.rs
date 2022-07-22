// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::auth::FsCred;
use crate::fs::{
    emit_dotdot, fileops_impl_directory, DirectoryEntryType, DirentSink, FileObject, FileOps,
    FileSystem, FsNode, FsNodeOps, FsStr, FsString, SeekOrigin,
};
use crate::task::CurrentTask;
use crate::types::*;
use std::sync::Arc;

/// A directory entry generated on-demand by implementors of [`DirectoryDelegate`].
#[derive(Debug, PartialEq, Eq, Clone, PartialOrd, Ord)]
pub struct DynamicDirectoryEntry {
    /// The type of the directory entry (directory, regular, socket, etc).
    pub entry_type: DirectoryEntryType,

    /// The name of the directory entry.
    pub name: FsString,

    /// Optional inode associated with the entry. If `None`, the entry will be auto-assigned one.
    pub inode: Option<ino_t>,
}

/// The trait defining the core operations a custom, dynamic in-memory directory must implement.
/// For use with [`dynamic_directory()`].
pub trait DirectoryDelegate: Sync + Send + 'static {
    /// Generate a list of [`DynamicDirectoryEntry`]. A [`FileSystem`] is provided in order to
    /// generate inode values.
    fn list(&self, fs: &Arc<FileSystem>) -> Result<Vec<DynamicDirectoryEntry>, Errno>;

    /// Look up an entry by `name`. A [`FileSystem`] is provided in order to generate an [`FsNode`]
    /// instance.
    fn lookup(
        &self,
        current_task: &CurrentTask,
        fs: &Arc<FileSystem>,
        name: &FsStr,
    ) -> Result<Arc<FsNode>, Errno>;
}

/// Creates an [`FsNode`] that implements a directory, whose contents are determined dynamically by
/// a [`DirectoryDelegate`] implementation. The resulting directory has permissions `0o777`
/// (`rwxrwxrwx`).
pub fn dynamic_directory<D: DirectoryDelegate>(fs: &Arc<FileSystem>, d: D) -> Arc<FsNode> {
    fs.create_node_with_ops(Arc::new(DynamicDirectory(d)), mode!(IFDIR, 0o777), FsCred::root())
}

pub fn root_dynamic_directory<D: DirectoryDelegate>(d: D) -> FsNode {
    FsNode::new_root(Arc::new(DynamicDirectory(d)))
}

struct DynamicDirectory<D>(D);

impl<D: DirectoryDelegate> FsNodeOps for Arc<DynamicDirectory<D>> {
    fn create_file_ops(
        &self,
        _node: &FsNode,
        _flags: OpenFlags,
    ) -> Result<Box<dyn FileOps>, Errno> {
        Ok(Box::new(self.clone()))
    }

    fn lookup(
        &self,
        node: &FsNode,
        current_task: &CurrentTask,
        name: &FsStr,
    ) -> Result<Arc<FsNode>, Errno> {
        self.0.lookup(current_task, &node.fs(), name)
    }
}

impl<D: DirectoryDelegate> FileOps for Arc<DynamicDirectory<D>> {
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

        let mut entries = self.0.list(&file.fs)?;

        // Sort to ensure consistent ordering between listings.
        entries.sort_unstable();

        // Skip through the entries until the current offset is reached.
        // Subtract 2 from the offset to account for `.` and `..`.
        for entry in entries.into_iter().skip(sink.offset() as usize - 2) {
            // Assign an inode if one wasn't set.
            let inode = entry.inode.unwrap_or_else(|| file.fs.next_inode_num());
            sink.add(inode, sink.offset() + 1, entry.entry_type, &entry.name)?;
        }
        Ok(())
    }
}

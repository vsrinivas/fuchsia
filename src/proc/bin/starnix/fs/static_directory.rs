// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::auth::FsCred;
use crate::fs::{
    emit_dotdot, fileops_impl_directory, DirectoryEntryType, DirentSink, FileObject, FileOps,
    FileSystem, FsNode, FsNodeHandle, FsNodeOps, FsStr, SeekOrigin,
};
use crate::task::CurrentTask;
use crate::types::*;
use std::collections::BTreeMap;
use std::sync::Arc;

/// Builds an implementation of [`FsNodeOps`] that serves as a directory of static and immutable
/// entries.
pub struct StaticDirectoryBuilder<'a> {
    fs: &'a Arc<FileSystem>,
    mode: FileMode,
    creds: FsCred,
    entries: BTreeMap<&'static FsStr, Arc<FsNode>>,
}

impl<'a> StaticDirectoryBuilder<'a> {
    /// Creates a new builder using the given [`FileSystem`] to acquire inode numbers.
    pub fn new(fs: &'a Arc<FileSystem>) -> Self {
        Self { fs, mode: mode!(IFDIR, 0o777), creds: FsCred::root(), entries: BTreeMap::new() }
    }

    /// Adds an entry to the directory. Panics if an entry with the same name was already added.
    pub fn add_entry(
        self,
        name: &'static FsStr,
        ops: impl FsNodeOps,
        mode: FileMode,
    ) -> Self {
        self.add_device_entry(name, ops, mode, DeviceType::NONE)
    }

    /// Adds an entry to the directory. Panics if an entry with the same name was already added.
    pub fn add_entry_with_creds(
        self,
        name: &'static FsStr,
        ops: impl FsNodeOps,
        mode: FileMode,
        creds: FsCred,
    ) -> Self {
        self.add_device_entry_with_creds(name, ops, mode, DeviceType::NONE, creds)
    }

    /// Adds an entry to the directory. Panics if an entry with the same name was already added.
    pub fn add_device_entry(
        self,
        name: &'static FsStr,
        ops: impl FsNodeOps,
        mode: FileMode,
        dev: DeviceType,
    ) -> Self {
        self.add_device_entry_with_creds(name, ops, mode, dev, FsCred::root())
    }

    /// Adds an entry to the directory. Panics if an entry with the same name was already added.
    pub fn add_device_entry_with_creds(
        self,
        name: &'static FsStr,
        ops: impl FsNodeOps,
        mode: FileMode,
        dev: DeviceType,
        creds: FsCred,
    ) -> Self {
        let node = self.fs.create_node_with_ops(ops, mode, creds);
        {
            let mut info = node.info_write();
            info.rdev = dev;
        }
        self.add_node_entry(name, node)
    }

    /// Adds an [`FsNode`] entry to the directory, which already has an inode number and file mode.
    /// Panics if an entry with the same name was already added.
    pub fn add_node_entry(mut self, name: &'static FsStr, node: Arc<FsNode>) -> Self {
        assert!(
            self.entries.insert(name, node).is_none(),
            "adding a duplicate entry into a StaticDirectory",
        );
        self
    }

    /// Set the mode of the directory. The type must always be IFDIR.
    pub fn set_mode(mut self, mode: FileMode) -> Self {
        assert!(mode.is_dir());
        self.mode = mode;
        self
    }

    pub fn set_creds(mut self, creds: FsCred) -> Self {
        self.creds = creds;
        self
    }

    /// Builds an [`FsNode`] that serves as a directory of the entries added to this builder.
    /// The resulting directory has mode `0o777` (`rwxrwxrwx`).
    pub fn build(self) -> Arc<FsNode> {
        self.fs.create_node_with_ops(
            Arc::new(StaticDirectory { entries: self.entries }),
            self.mode,
            self.creds,
        )
    }

    /// Build the node associated with the static directory and makes it the root of the
    /// filesystem.
    pub fn build_root(self) {
        let node = FsNode::new_root(Arc::new(StaticDirectory { entries: self.entries }));
        {
            let mut info = node.info_write();
            info.uid = self.creds.uid;
            info.gid = self.creds.gid;
            info.mode = self.mode;
        }
        self.fs.set_root_node(node);
    }
}

struct StaticDirectory {
    entries: BTreeMap<&'static FsStr, Arc<FsNode>>,
}

impl FsNodeOps for Arc<StaticDirectory> {
    fn create_file_ops(
        &self,
        _node: &FsNode,
        _flags: OpenFlags,
    ) -> Result<Box<dyn FileOps>, Errno> {
        Ok(Box::new(self.clone()))
    }

    fn lookup(
        &self,
        _node: &FsNode,
        _current_task: &CurrentTask,
        name: &FsStr,
    ) -> Result<Arc<FsNode>, Errno> {
        self.entries.get(name).cloned().ok_or_else(|| {
            errno!(
                ENOENT,
                format!(
                    "looking for {:?} in {:?}",
                    String::from_utf8_lossy(name),
                    self.entries
                        .keys()
                        .map(|e| String::from_utf8_lossy(e).to_string())
                        .collect::<Vec<String>>()
                )
            )
        })
    }

    fn mknod(
        &self,
        _node: &FsNode,
        _name: &FsStr,
        _mode: FileMode,
        _dev: DeviceType,
        _owner: FsCred,
    ) -> Result<FsNodeHandle, Errno> {
        error!(EROFS)
    }

    fn mkdir(
        &self,
        _node: &FsNode,
        _name: &FsStr,
        _mode: FileMode,
        _owner: FsCred,
    ) -> Result<FsNodeHandle, Errno> {
        error!(EROFS)
    }

    fn unlink(&self, _parent: &FsNode, _name: &FsStr, _child: &FsNodeHandle) -> Result<(), Errno> {
        error!(EROFS)
    }
}

impl FileOps for Arc<StaticDirectory> {
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
        for (name, node) in self.entries.iter().skip(sink.offset() as usize - 2) {
            sink.add(
                node.inode_num,
                sink.offset() + 1,
                DirectoryEntryType::from_mode(node.info().mode),
                name,
            )?;
        }
        Ok(())
    }
}

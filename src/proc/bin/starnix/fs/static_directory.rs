// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::auth::FsCred;
use crate::fs::{
    emit_dotdot, fileops_impl_directory, fs_node_impl_dir_readonly, DirectoryEntryType, DirentSink,
    FileObject, FileOps, FileSystem, FileSystemHandle, FsNode, FsNodeOps, FsStr, SeekOrigin,
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
    entry_creds: FsCred,
    entries: BTreeMap<&'static FsStr, Arc<FsNode>>,
}

impl<'a> StaticDirectoryBuilder<'a> {
    /// Creates a new builder using the given [`FileSystem`] to acquire inode numbers.
    pub fn new(fs: &'a FileSystemHandle) -> Self {
        Self {
            fs,
            mode: mode!(IFDIR, 0o777),
            creds: FsCred::root(),
            entry_creds: FsCred::root(),
            entries: BTreeMap::new(),
        }
    }

    /// Set the creds used for future entries.
    pub fn entry_creds(mut self, creds: FsCred) -> Self {
        self.entry_creds = creds;
        self
    }

    /// Adds an entry to the directory. Panics if an entry with the same name was already added.
    pub fn entry(self, name: &'static FsStr, ops: impl FsNodeOps, mode: FileMode) -> Self {
        self.entry_dev(name, ops, mode, DeviceType::NONE)
    }

    /// Adds an entry to the directory. Panics if an entry with the same name was already added.
    pub fn entry_dev(
        self,
        name: &'static FsStr,
        ops: impl FsNodeOps,
        mode: FileMode,
        dev: DeviceType,
    ) -> Self {
        let node = self.fs.create_node_with_ops(ops, mode, self.entry_creds.clone());
        {
            let mut info = node.info_write();
            info.rdev = dev;
        }
        self.node(name, node)
    }

    pub fn subdir(
        self,
        name: &'static FsStr,
        mode: u32,
        build_subdir: impl Fn(Self) -> Self,
    ) -> Self {
        let subdir = build_subdir(Self::new(self.fs));
        self.node(name, subdir.set_mode(mode!(IFDIR, mode)).build())
    }

    /// Adds an [`FsNode`] entry to the directory, which already has an inode number and file mode.
    /// Panics if an entry with the same name was already added.
    pub fn node(mut self, name: &'static FsStr, node: Arc<FsNode>) -> Self {
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

    pub fn dir_creds(mut self, creds: FsCred) -> Self {
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

pub struct StaticDirectory {
    entries: BTreeMap<&'static FsStr, Arc<FsNode>>,
}

impl FsNodeOps for Arc<StaticDirectory> {
    fs_node_impl_dir_readonly!();

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

// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::fs::*;
use crate::types::*;

/// A `NodeHolder` is responsible for the lazy initialization of all the non-"task directory"
/// entries in the proc directory.
///
/// It will create a new `FsNode` when required, using the filesystem to determine which inode
/// number the node will get.
pub struct NodeHolder {
    /// The function that creates the `FsNodeOps` for the created node.
    ops: Box<dyn Fn() -> Result<Box<dyn FsNodeOps>, Errno> + Send + Sync>,

    /// The file mode to set on the created node.
    pub file_mode: FileMode,

    /// The inode number of the node, if the node has been created.
    inode_num: Option<ino_t>,
}

impl NodeHolder {
    /// Creates a new `NodeHolder`.
    ///
    /// - `ops`: A function, returning `FsNodeOps`, that is called when a new `FsNode` is created.
    /// - `file_mode`: The `FileMode` that will be set on the created `FsNode`.
    pub fn new<F>(ops: F, file_mode: FileMode) -> NodeHolder
    where
        F: Fn() -> Result<Box<dyn FsNodeOps>, Errno> + Send + Sync,
        F: 'static,
    {
        NodeHolder { ops: Box::new(ops), file_mode, inode_num: None }
    }

    /// Returns a `FsNodeHandle` to the node that this holder is responsible for.
    ///
    /// - `fs`: The file system that is used to create and cache the `FsNode`. It is also
    /// used to generate the inode number for the node.
    pub fn get_or_create_node(&mut self, fs: &FileSystemHandle) -> Result<FsNodeHandle, Errno> {
        fs.get_or_create_node(self.inode_num, |inode_num| {
            self.inode_num = Some(inode_num);
            Ok(FsNode::new((self.ops)()?, fs, inode_num, self.file_mode))
        })
    }

    /// Returns the `ino_t` associated with this `NodeHolder`'s `FsNode`.
    ///
    /// Creates the underlying `FsNode` if needed to return the correct `ino_t`.
    ///
    /// - `fs`: The file system that is used to create and cache the `FsNode`, if needed.
    pub fn get_inode_num(&mut self, fs: &FileSystemHandle) -> Result<ino_t, Errno> {
        if let Some(inode_num) = self.inode_num {
            Ok(inode_num)
        } else {
            let node = self.get_or_create_node(fs)?;
            Ok(node.inode_num)
        }
    }
}

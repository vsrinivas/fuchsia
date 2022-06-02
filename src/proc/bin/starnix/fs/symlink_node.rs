// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;
use crate::fs::fs_node_impl_symlink;
use crate::fs::fs_node_impl_xattr_delegate;
use crate::task::CurrentTask;
use crate::types::*;

/// A node that represents a symlink to another node.
pub struct SymlinkNode {
    /// The target of the symlink (the path to use to find the actual node).
    target: FsString,
    xattrs: MemoryXattrStorage,
}

impl SymlinkNode {
    pub fn new(target: &FsStr) -> Self {
        SymlinkNode { target: target.to_owned(), xattrs: MemoryXattrStorage::default() }
    }
}

impl FsNodeOps for SymlinkNode {
    fs_node_impl_symlink!();
    fs_node_impl_xattr_delegate!(self, self.xattrs);

    fn readlink(
        &self,
        _node: &FsNode,
        _current_task: &Option<&CurrentTask>,
    ) -> Result<SymlinkTarget, Errno> {
        Ok(SymlinkTarget::Path(self.target.clone()))
    }
}

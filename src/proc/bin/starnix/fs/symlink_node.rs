// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use parking_lot::Mutex;
use std::sync::Weak;

use super::*;
use crate::fs::tmpfs::TmpfsState;
use crate::types::*;

/// A node that represents a symlink to another node.
pub struct SymlinkNode {
    /// The target of the symlink (the path to use to find the actual node).
    target: FsString,

    /// The file system to which this file belongs.
    fs: Weak<Mutex<TmpfsState>>,
}

impl SymlinkNode {
    pub fn new(fs: Weak<Mutex<TmpfsState>>, target: &FsStr) -> Self {
        SymlinkNode { fs, target: target.to_owned() }
    }
}

impl FsNodeOps for SymlinkNode {
    fn open(&self, _node: &FsNode, _flags: OpenFlags) -> Result<Box<dyn FileOps>, Errno> {
        Ok(Box::new(NullFile))
    }

    fn readlink<'a>(&'a self) -> Result<FsString, Errno> {
        Ok(self.target.clone())
    }

    fn initialize(&self, node: &FsNodeHandle) {
        self.fs.upgrade().map(|fs| fs.lock().register(node));
    }

    fn unlinked(&self, node: &FsNodeHandle) {
        self.fs.upgrade().map(|fs| fs.lock().unregister(node));
    }
}

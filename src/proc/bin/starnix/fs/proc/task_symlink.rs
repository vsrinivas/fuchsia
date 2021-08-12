// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::fs::FsNodeOps;
use crate::fs::*;
use crate::task::Task;
use crate::types::*;

/// A node that represents a symlink to `proc/<pid>` where <pid> is the pid of the task that
/// reads the symlink.
pub struct TaskSymlink {}

impl TaskSymlink {
    pub fn new() -> Self {
        TaskSymlink {}
    }
}

impl FsNodeOps for TaskSymlink {
    fn open(&self, _node: &FsNode, _flags: OpenFlags) -> Result<Box<dyn FileOps>, Errno> {
        unreachable!("Symlink nodes cannot be opened.");
    }

    fn readlink(&self, _node: &FsNode, task: &Task) -> Result<FsString, Errno> {
        Ok(format!("{}", task.id).as_bytes().to_vec())
    }
}

// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;
use crate::types::*;

pub struct DevptsFs;
impl FileSystemOps for DevptsFs {}
impl DevptsFs {
    pub fn new() -> FileSystemHandle {
        FileSystem::new(DevptsFs, FsNode::new_root(DevptsDirectory), None, false)
    }
}

struct DevptsDirectory;
impl FsNodeOps for DevptsDirectory {
    fn open(&self, _node: &FsNode, _flags: OpenFlags) -> Result<Box<dyn FileOps>, Errno> {
        Err(ENOSYS)
    }
}

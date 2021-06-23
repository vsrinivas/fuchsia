// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use super::*;
use crate::devices::*;
use crate::types::*;

pub struct TmpfsDirectory;

impl FsNodeOps for TmpfsDirectory {
    fn mkdir(&self, _name: &FsStr) -> Result<Box<dyn FsNodeOps>, Errno> {
        Ok(Box::new(Self))
    }
    fn open(&self) -> Result<Box<dyn FileOps>, Errno> {
        Err(ENOSYS)
    }
}

pub fn new_tmpfs() -> FsNodeHandle {
    let tmpfs_dev = AnonNodeDevice::new(0);
    FsNode::new_root(TmpfsDirectory, tmpfs_dev)
}

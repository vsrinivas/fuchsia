// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;
use crate::devices::AnonNodeDevice;
use crate::fd_impl_seekable;
use crate::fs::NullFile;
use crate::task::*;
use crate::types::*;

pub struct DevfsDirectory;

impl FsNodeOps for DevfsDirectory {
    fn mkdir(&self, _node: &FsNode, _name: &FsStr) -> Result<Box<dyn FsNodeOps>, Errno> {
        Err(ENOSYS)
    }

    fn open(&self, _node: &FsNode) -> Result<Box<dyn FileOps>, Errno> {
        Ok(Box::new(NullFile))
    }

    fn create(&self, _node: &FsNode, name: &FsStr) -> Result<Box<dyn FsNodeOps>, Errno> {
        if name == b"null" {
            Ok(Box::new(DevNullFileNode))
        } else {
            Err(ENOENT)
        }
    }

    fn lookup(&self, _node: &FsNode, name: &FsStr) -> Result<Box<dyn FsNodeOps>, Errno> {
        if name == b"null" {
            Ok(Box::new(DevNullFileNode))
        } else {
            Err(ENOENT)
        }
    }
}

pub fn new_devfs() -> FsNodeHandle {
    let devfs_dev = AnonNodeDevice::new(0);
    FsNode::new_root(DevfsDirectory, devfs_dev)
}

/// A `DevNullFileNode` returns a new `DevNullFileObject` for `open`.
struct DevNullFileNode;

/// A `DevNullFileObject` is a file that can't be read, and any writes are discarded.
struct DevNullFileObject;

impl FsNodeOps for DevNullFileNode {
    fn open(&self, _node: &FsNode) -> Result<Box<dyn FileOps>, Errno> {
        Ok(Box::new(DevNullFileObject))
    }
}

impl FileOps for DevNullFileObject {
    fd_impl_seekable!();

    fn write_at(
        &self,
        _file: &FileObject,
        _task: &Task,
        _offset: usize,
        data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        Ok(data.len())
    }

    fn read_at(
        &self,
        _file: &FileObject,
        _task: &Task,
        _offset: usize,
        _data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        Err(EINVAL)
    }
}

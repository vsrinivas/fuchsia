// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::sync::Arc;

use super::*;
use crate::devices::AnonNodeDevice;
use crate::fd_impl_seekable;
use crate::fs::NullFile;
use crate::task::*;
use crate::types::*;

pub struct Devfs {
    root: FsNodeHandle,
}

impl Devfs {
    pub fn new() -> FileSystemHandle {
        let devfs_dev = AnonNodeDevice::new(0);
        Arc::new(Devfs { root: FsNode::new_root(DevfsDirectory, devfs_dev) })
    }
}

impl FileSystem for Devfs {
    fn root(&self) -> &FsNodeHandle {
        &self.root
    }
}

struct DevfsDirectory;

impl FsNodeOps for DevfsDirectory {
    fn open(&self, _node: &FsNode, _flags: OpenFlags) -> Result<Box<dyn FileOps>, Errno> {
        Ok(Box::new(NullFile))
    }

    fn lookup(
        &self,
        _node: &FsNode,
        name: &FsStr,
        info: &mut FsNodeInfo,
    ) -> Result<Box<dyn FsNodeOps>, Errno> {
        if name == b"null" {
            info.mode = FileMode::IFCHR | FileMode::ALLOW_ALL;
            Ok(Box::new(DevNullFileNode))
        } else {
            Err(ENOENT)
        }
    }
}

/// A `DevNullFileNode` returns a new `DevNullFileObject` for `open`.
struct DevNullFileNode;

/// A `DevNullFileObject` is a file that can't be read, and any writes are discarded.
struct DevNullFileObject;

impl FsNodeOps for DevNullFileNode {
    fn open(&self, _node: &FsNode, _flags: OpenFlags) -> Result<Box<dyn FileOps>, Errno> {
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

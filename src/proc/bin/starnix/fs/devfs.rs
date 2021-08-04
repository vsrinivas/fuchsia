// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;
use crate::fd_impl_seekable;
use crate::fs::NullFile;
use crate::task::*;
use crate::types::*;

pub struct DevFs;
impl FileSystemOps for DevFs {}

impl DevFs {
    pub fn new() -> FileSystemHandle {
        FileSystem::new(DevFs, DevfsDirectory)
    }
}

struct DevfsDirectory;

impl FsNodeOps for DevfsDirectory {
    fn open(&self, _node: &FsNode, _flags: OpenFlags) -> Result<Box<dyn FileOps>, Errno> {
        Ok(Box::new(NullFile))
    }

    fn lookup(&self, _parent: &FsNode, mut child: FsNode) -> Result<FsNodeHandle, Errno> {
        if child.local_name() == b"null" {
            child.info_mut().mode = FileMode::IFCHR | FileMode::ALLOW_ALL;
            child.set_ops(DevNullFileNode);
            Ok(child.into_handle())
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

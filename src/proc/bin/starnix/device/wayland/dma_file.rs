// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::errno;
use crate::error;
use crate::fd_impl_nonseekable;
use crate::fs::*;
use crate::task::Task;
use crate::types::*;

pub struct DmaNode {}
impl FsNodeOps for DmaNode {
    fn open(&self, _node: &FsNode, _flags: OpenFlags) -> Result<Box<dyn FileOps>, Errno> {
        DmaFile::new()
    }
}

/// A file that is responsible for allocating and managing memory on behalf of wayland clients.
///
/// Clients send ioctls to this file to create DMA buffers, and the file communicates with graphics
/// and sysmem allocators to do so.
pub struct DmaFile {}

impl DmaFile {
    /// Creates a new `DmaFile`.
    ///
    /// Returns an error if the file could not establish connections to `fsysmem::Allocator` and
    /// `fuicomp::Allocator`.
    pub fn new() -> Result<Box<dyn FileOps>, Errno> {
        Ok(Box::new(Self {}))
    }
}

impl FileOps for DmaFile {
    fd_impl_nonseekable!();

    fn read(&self, _file: &FileObject, _task: &Task, _data: &[UserBuffer]) -> Result<usize, Errno> {
        error!(EINVAL)
    }

    fn write(
        &self,
        _file: &FileObject,
        _task: &Task,
        _data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        error!(EINVAL)
    }
}

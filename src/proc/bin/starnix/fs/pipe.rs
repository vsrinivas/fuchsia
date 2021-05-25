// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::devices::*;
use crate::fd_impl_nonseekable;
use crate::fs::*;
use crate::task::*;
use crate::types::*;

const ATOMIC_IO_BYTES: usize = 4096;

pub struct PipeReadEndpoint {}

pub struct PipeWriteEndpoint {}

pub struct PipeNode {}

impl PipeNode {
    pub fn new(task: &Task) -> Result<(FileHandle, FileHandle), Errno> {
        let pipe_device = task
            .thread_group
            .kernel
            .devices
            .get_anonymous_node_device(AnonymousNodeDeviceName::Pipe);
        let node = FsNode::new_root(PipeNode {}, pipe_device);
        let read = FileObject::new_with_node(Box::new(PipeReadEndpoint {}), Some(node.clone()));
        let write = FileObject::new_with_node(Box::new(PipeWriteEndpoint {}), Some(node));
        Ok((read, write))
    }
}

impl FsNodeOps for PipeNode {
    fn open(&self) -> Result<Box<dyn FileOps>, Errno> {
        Err(ENOSYS)
    }
}

impl FileOps for PipeReadEndpoint {
    fd_impl_nonseekable!();

    fn write(&self, _fd: &FileObject, _task: &Task, _data: &[iovec_t]) -> Result<usize, Errno> {
        Err(ENOSYS)
    }

    fn read(&self, _fd: &FileObject, _task: &Task, _data: &[iovec_t]) -> Result<usize, Errno> {
        Err(ENOSYS)
    }

    fn fstat(&self, fd: &FileObject, _task: &Task) -> Result<stat_t, Errno> {
        Ok(stat_t { st_blksize: ATOMIC_IO_BYTES as i64, ..fd.node.as_ref().unwrap().fstat() })
    }
}

impl FileOps for PipeWriteEndpoint {
    fd_impl_nonseekable!();

    fn write(&self, _fd: &FileObject, _task: &Task, _data: &[iovec_t]) -> Result<usize, Errno> {
        Err(ENOSYS)
    }

    fn read(&self, _fd: &FileObject, _task: &Task, _data: &[iovec_t]) -> Result<usize, Errno> {
        Err(ENOSYS)
    }

    fn fstat(&self, fd: &FileObject, _task: &Task) -> Result<stat_t, Errno> {
        Ok(stat_t { st_blksize: ATOMIC_IO_BYTES as i64, ..fd.node.as_ref().unwrap().fstat() })
    }
}

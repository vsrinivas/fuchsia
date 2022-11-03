// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::sync::Arc;

use super::*;
use crate::fs::{fileops_impl_nonblocking, fileops_impl_seekable};
use crate::mm::MemoryAccessorExt;
use crate::task::*;
use crate::types::*;

pub struct SimpleFileNode<F, O>
where
    F: Fn() -> Result<O, Errno>,
    O: FileOps,
{
    create_file_ops: F,
}
impl<F, O> SimpleFileNode<F, O>
where
    F: Fn() -> Result<O, Errno> + Send + Sync,
    O: FileOps,
{
    pub fn new(create_file_ops: F) -> SimpleFileNode<F, O> {
        SimpleFileNode { create_file_ops }
    }
}
impl<F, O> FsNodeOps for SimpleFileNode<F, O>
where
    F: Fn() -> Result<O, Errno> + Send + Sync + 'static,
    O: FileOps,
{
    fn create_file_ops(
        &self,
        _node: &FsNode,
        _flags: OpenFlags,
    ) -> Result<Box<dyn FileOps>, Errno> {
        Ok(Box::new((self.create_file_ops)()?))
    }
}

pub struct ByteVecFile(Arc<Vec<u8>>);
impl ByteVecFile {
    pub fn new_node(data: Vec<u8>) -> impl FsNodeOps {
        let data = Arc::new(data);
        SimpleFileNode::new(move || Ok(ByteVecFile(Arc::clone(&data))))
    }
}
impl FileOps for ByteVecFile {
    fileops_impl_seekable!();
    fileops_impl_nonblocking!();

    fn read_at(
        &self,
        _file: &FileObject,
        current_task: &CurrentTask,
        offset: usize,
        data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        current_task.mm.write_all(data, &self.0[offset..])
    }

    fn write_at(
        &self,
        _file: &FileObject,
        _current_task: &CurrentTask,
        _offset: usize,
        _data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        error!(ENOSYS)
    }
}

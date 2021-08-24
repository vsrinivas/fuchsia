// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::sync::Arc;

use super::*;
use crate::errno;
use crate::error;
use crate::fd_impl_seekable;
use crate::task::*;
use crate::types::*;

pub struct SimpleFileNode<F, O>
where
    F: Fn() -> Result<O, Errno>,
    O: FileOps,
{
    open: F,
}
impl<F, O> SimpleFileNode<F, O>
where
    F: Fn() -> Result<O, Errno> + Send + Sync,
    O: FileOps + 'static,
{
    pub fn new(open: F) -> SimpleFileNode<F, O> {
        SimpleFileNode { open }
    }
}
impl<F, O> FsNodeOps for SimpleFileNode<F, O>
where
    F: Fn() -> Result<O, Errno> + Send + Sync,
    O: FileOps + 'static,
{
    fn open(&self, _node: &FsNode, _flags: OpenFlags) -> Result<Box<dyn FileOps>, Errno> {
        Ok(Box::new((self.open)()?))
    }
}

pub struct ByteVecFile(Arc<Vec<u8>>);
impl ByteVecFile {
    pub fn new(data: Vec<u8>) -> impl FsNodeOps {
        let data = Arc::new(data);
        SimpleFileNode::new(move || Ok(ByteVecFile(Arc::clone(&data))))
    }
}
impl FileOps for ByteVecFile {
    fd_impl_seekable!();
    fn read_at(
        &self,
        _file: &FileObject,
        task: &Task,
        offset: usize,
        data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        let buf_len = UserBuffer::get_total_length(data);
        if offset >= self.0.len() {
            return Ok(0);
        }
        let mut out_len = self.0.len() - offset;
        if out_len > buf_len {
            out_len = buf_len;
        }
        task.mm.write_all(data, &self.0[offset..offset + out_len])?;
        Ok(out_len)
    }

    fn write_at(
        &self,
        _file: &FileObject,
        _task: &Task,
        _offset: usize,
        _data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        Err(ENOSYS)
    }
}

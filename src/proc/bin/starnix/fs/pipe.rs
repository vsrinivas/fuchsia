// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use parking_lot::Mutex;
use std::sync::Arc;

use crate::devices::*;
use crate::fd_impl_nonseekable;
use crate::fs::*;
use crate::mm::PAGE_SIZE;
use crate::syscalls::*;
use crate::task::*;
use crate::types::*;

const ATOMIC_IO_BYTES: usize = 4096;

fn round_up(value: usize, increment: usize) -> usize {
    (value + (increment - 1)) & !(increment - 1)
}

struct Pipe {
    /// The maximum number of bytes that can be stored inside this pipe.
    size: usize,

    /// The number of bytes actually stored in inside the pipe currently.
    used: usize,
}

impl Pipe {
    fn new() -> Arc<Mutex<Pipe>> {
        // Pipes default to a size of 16 pages.
        let default_pipe_size = (*PAGE_SIZE * 16) as usize;

        Arc::new(Mutex::new(Pipe { size: default_pipe_size, used: 0 }))
    }

    fn get_size(&self) -> usize {
        self.size
    }

    fn set_size(&mut self, mut requested_size: usize) -> Result<(), Errno> {
        if requested_size < self.used {
            return Err(EBUSY);
        }
        let page_size = *PAGE_SIZE as usize;
        if requested_size < page_size {
            requested_size = page_size;
        }
        self.size = round_up(requested_size, page_size);
        Ok(())
    }

    fn fcntl(
        &mut self,
        _file: &FileObject,
        _task: &Task,
        cmd: u32,
        arg: u64,
    ) -> Result<SyscallResult, Errno> {
        match cmd {
            F_GETPIPE_SZ => Ok(self.get_size().into()),
            F_SETPIPE_SZ => {
                self.set_size(arg as usize)?;
                Ok(self.get_size().into())
            }
            _ => Err(EINVAL),
        }
    }
}

pub struct PipeNode {}

pub struct PipeReadEndpoint {
    pipe: Arc<Mutex<Pipe>>,
}

pub struct PipeWriteEndpoint {
    pipe: Arc<Mutex<Pipe>>,
}

impl PipeNode {
    pub fn new(task: &Task) -> Result<(FileHandle, FileHandle), Errno> {
        let pipe_device = task
            .thread_group
            .kernel
            .devices
            .get_anonymous_node_device(AnonymousNodeDeviceName::Pipe);
        let pipe = Pipe::new();
        let node = FsNode::new_root(PipeNode {}, pipe_device);
        let read = FileObject::new_with_node(
            Box::new(PipeReadEndpoint { pipe: pipe.clone() }),
            Some(node.clone()),
        );
        let write = FileObject::new_with_node(Box::new(PipeWriteEndpoint { pipe }), Some(node));
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

    fn write(&self, _file: &FileObject, _task: &Task, _data: &[iovec_t]) -> Result<usize, Errno> {
        Err(ENOSYS)
    }

    fn read(&self, _file: &FileObject, _task: &Task, _data: &[iovec_t]) -> Result<usize, Errno> {
        Err(ENOSYS)
    }

    fn fstat(&self, file: &FileObject, _task: &Task) -> Result<stat_t, Errno> {
        Ok(stat_t { st_blksize: ATOMIC_IO_BYTES as i64, ..file.node.as_ref().unwrap().fstat() })
    }

    fn fcntl(
        &self,
        file: &FileObject,
        task: &Task,
        cmd: u32,
        arg: u64,
    ) -> Result<SyscallResult, Errno> {
        self.pipe.lock().fcntl(file, task, cmd, arg)
    }
}

impl FileOps for PipeWriteEndpoint {
    fd_impl_nonseekable!();

    fn write(&self, _file: &FileObject, _task: &Task, _data: &[iovec_t]) -> Result<usize, Errno> {
        Err(ENOSYS)
    }

    fn read(&self, _file: &FileObject, _task: &Task, _data: &[iovec_t]) -> Result<usize, Errno> {
        Err(ENOSYS)
    }

    fn fstat(&self, file: &FileObject, _task: &Task) -> Result<stat_t, Errno> {
        Ok(stat_t { st_blksize: ATOMIC_IO_BYTES as i64, ..file.node.as_ref().unwrap().fstat() })
    }

    fn fcntl(
        &self,
        file: &FileObject,
        task: &Task,
        cmd: u32,
        arg: u64,
    ) -> Result<SyscallResult, Errno> {
        self.pipe.lock().fcntl(file, task, cmd, arg)
    }
}

// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use parking_lot::Mutex;
use std::collections::VecDeque;
use std::convert::TryInto;
use std::sync::Arc;

use crate::devices::*;
use crate::fd_impl_nonseekable;
use crate::fs::*;
use crate::mm::PAGE_SIZE;
use crate::signals::{signal_handling::send_signal, *};
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

    /// The bytes stored inside the pipe.
    ///
    /// Write are added at the end of the queue. Read consume from the front of
    /// the queue.
    buffers: VecDeque<Vec<u8>>,

    /// The reader end of this pipe is open.
    has_reader: bool,

    /// The writer end of this pipe is open.
    has_writer: bool,
}

impl Pipe {
    fn new() -> Arc<Mutex<Pipe>> {
        // Pipes default to a size of 16 pages.
        let default_pipe_size = (*PAGE_SIZE * 16) as usize;

        Arc::new(Mutex::new(Pipe {
            size: default_pipe_size,
            used: 0,
            buffers: VecDeque::new(),
            has_reader: true,
            has_writer: true,
        }))
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

    fn get_available(&self) -> usize {
        self.size - self.used
    }

    fn pop_front(&mut self) -> Option<Vec<u8>> {
        if let Some(front) = self.buffers.pop_front() {
            self.used -= front.len();
            return Some(front);
        }
        None
    }

    fn push_front(&mut self, buffer: Vec<u8>) {
        self.used += buffer.len();
        self.buffers.push_front(buffer);
    }

    fn push_back(&mut self, buffer: Vec<u8>) {
        self.used += buffer.len();
        self.buffers.push_back(buffer);
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

    fn ioctl(
        &self,
        _file: &FileObject,
        task: &Task,
        request: u32,
        in_addr: UserAddress,
        _out_addr: UserAddress,
    ) -> Result<SyscallResult, Errno> {
        match request {
            FIONREAD => {
                let addr = UserRef::<i32>::new(in_addr);
                let value: i32 = self.used.try_into().map_err(|_| EINVAL)?;
                task.mm.write_object(addr, &value)?;
                Ok(SUCCESS)
            }
            _ => default_ioctl(request),
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

impl Drop for PipeReadEndpoint {
    fn drop(&mut self) {
        let mut pipe = self.pipe.lock();
        pipe.has_reader = false;
    }
}

impl FileOps for PipeReadEndpoint {
    fd_impl_nonseekable!();

    fn write(
        &self,
        _file: &FileObject,
        _task: &Task,
        _data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        Err(EBADF)
    }

    fn read(&self, _file: &FileObject, task: &Task, data: &[UserBuffer]) -> Result<usize, Errno> {
        let mut pipe = self.pipe.lock();
        if pipe.used == 0 {
            if pipe.has_writer {
                return Err(EAGAIN);
            } else {
                return Ok(0);
            }
        }
        let actual = std::cmp::min(pipe.used, UserBuffer::get_total_length(data));
        if actual == 0 {
            return Ok(0);
        }
        let mut remaining = actual;

        let mut iter = data.iter();
        let mut dst = iter.next().ok_or(EFAULT)?;
        let mut dst_offset = 0;

        let mut src = pipe.pop_front().ok_or(EFAULT)?;
        let mut src_offset = 0;
        loop {
            while dst_offset == dst.length {
                dst = iter.next().ok_or(EFAULT)?;
                dst_offset = 0;
            }

            while src_offset == src.len() {
                src = pipe.pop_front().ok_or(EFAULT)?;
                src_offset = 0;
            }

            let chunk_size = std::cmp::min(dst.length - dst_offset, src.len() - src_offset);
            let src_end = src_offset + chunk_size;
            let result = task.mm.write_memory(dst.address + dst_offset, &src[src_offset..src_end]);

            remaining -= chunk_size;

            if result.is_err() || remaining == 0 {
                if src_end != src.len() {
                    let leftover = src[src_end..].to_vec();
                    pipe.push_front(leftover);
                }
                result?;
                break;
            }

            dst_offset += chunk_size;
            src_offset += chunk_size;
        }
        Ok(actual)
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

    fn ioctl(
        &self,
        file: &FileObject,
        task: &Task,
        request: u32,
        in_addr: UserAddress,
        out_addr: UserAddress,
    ) -> Result<SyscallResult, Errno> {
        self.pipe.lock().ioctl(file, task, request, in_addr, out_addr)
    }
}

impl Drop for PipeWriteEndpoint {
    fn drop(&mut self) {
        let mut pipe = self.pipe.lock();
        pipe.has_writer = false;
    }
}

impl FileOps for PipeWriteEndpoint {
    fd_impl_nonseekable!();

    fn write(&self, _file: &FileObject, task: &Task, data: &[UserBuffer]) -> Result<usize, Errno> {
        let mut pipe = self.pipe.lock();
        if !pipe.has_reader {
            send_signal(&task, &UncheckedSignal::from(SIGPIPE))?;
            return Err(EPIPE);
        }
        let available = pipe.get_available();
        if available == 0 {
            return Err(EAGAIN);
        }
        let actual = std::cmp::min(available, UserBuffer::get_total_length(data));
        if actual == 0 {
            return Ok(0);
        }
        let mut bytes = vec![0u8; actual];
        task.mm.read_all(data, &mut bytes)?;
        pipe.push_back(bytes);
        Ok(actual)
    }

    fn read(&self, _file: &FileObject, _task: &Task, _data: &[UserBuffer]) -> Result<usize, Errno> {
        Err(EBADF)
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

    fn ioctl(
        &self,
        file: &FileObject,
        task: &Task,
        request: u32,
        in_addr: UserAddress,
        out_addr: UserAddress,
    ) -> Result<SyscallResult, Errno> {
        self.pipe.lock().ioctl(file, task, request, in_addr, out_addr)
    }
}

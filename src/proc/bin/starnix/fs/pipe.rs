// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use parking_lot::Mutex;
use std::collections::VecDeque;
use std::convert::TryInto;
use std::sync::Arc;

use crate::fd_impl_nonseekable;
use crate::fs::*;
use crate::mm::PAGE_SIZE;
use crate::signals::{signal_handling::send_signal, *};
use crate::syscalls::*;
use crate::task::*;
use crate::types::*;

const ATOMIC_IO_BYTES: usize = 4096;
const PIPE_MAX_SIZE: usize = 1048576; // From pipe.go in gVisor.

fn round_up(value: usize, increment: usize) -> usize {
    (value + (increment - 1)) & !(increment - 1)
}

pub struct Pipe {
    /// The maximum number of bytes that can be stored inside this pipe.
    size: usize,

    /// The number of bytes actually stored in inside the pipe currently.
    used: usize,

    /// The bytes stored inside the pipe.
    ///
    /// Write are added at the end of the queue. Read consume from the front of
    /// the queue.
    buffers: VecDeque<Vec<u8>>,

    /// The number of open readers.
    reader_count: usize,

    /// The number of open writers.
    writer_count: usize,
}

impl Pipe {
    pub fn new() -> Arc<Mutex<Pipe>> {
        // Pipes default to a size of 16 pages.
        let default_pipe_size = (*PAGE_SIZE * 16) as usize;

        Arc::new(Mutex::new(Pipe {
            size: default_pipe_size,
            used: 0,
            buffers: VecDeque::new(),
            reader_count: 0,
            writer_count: 0,
        }))
    }

    pub fn open(pipe: &Arc<Mutex<Self>>, flags: OpenFlags) -> Box<dyn FileOps> {
        {
            let mut pipe = pipe.lock();
            if flags.can_read() {
                pipe.reader_count += 1;
            }
            if flags.can_write() {
                pipe.writer_count += 1;
            }
        }
        Box::new(PipeFileObject { pipe: Arc::clone(pipe) })
    }

    fn get_size(&self) -> usize {
        self.size
    }

    fn set_size(&mut self, mut requested_size: usize) -> Result<(), Errno> {
        if requested_size > PIPE_MAX_SIZE {
            return Err(EINVAL);
        }
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

/// Creates a new pipe between the two returned FileObjects.
///
/// The first FileObject is the read endpoint of the pipe. The second is the
/// write endpoint of the pipe. This order matches the order expected by
/// sys_pipe2().
pub fn new_pipe(kernel: &Kernel) -> (FileHandle, FileHandle) {
    let pipe = Pipe::new();
    let node = Anon::new_node(kernel, AnonNodeType::Pipe);
    node.info_mut().blksize = ATOMIC_IO_BYTES;

    let open = |flags: OpenFlags| {
        let ops = Pipe::open(&pipe, flags);
        FileObject::new_anonymous(ops, Arc::clone(&node), flags)
    };

    (open(OpenFlags::RDONLY), open(OpenFlags::WRONLY))
}

struct PipeFileObject {
    pipe: Arc<Mutex<Pipe>>,
}

impl FileOps for PipeFileObject {
    fd_impl_nonseekable!();

    fn close(&self, file: &FileObject) {
        let events = {
            let mut events = FdEvents::empty();
            let mut pipe = self.pipe.lock();
            if file.flags().can_read() {
                assert!(pipe.reader_count > 0);
                pipe.reader_count -= 1;
                if pipe.reader_count == 0 {
                    events |= FdEvents::POLLOUT;
                }
            }
            if file.flags().can_write() {
                assert!(pipe.writer_count > 0);
                pipe.writer_count -= 1;
                if pipe.writer_count == 0 {
                    events |= FdEvents::POLLIN;
                }
            }
            events
        };
        if events != FdEvents::empty() {
            file.node().observers.notify(events);
        }
    }

    fn read(&self, file: &FileObject, task: &Task, data: &[UserBuffer]) -> Result<usize, Errno> {
        let mut pipe = self.pipe.lock();
        if pipe.used == 0 {
            if pipe.writer_count > 0 {
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
        file.node().observers.notify(FdEvents::POLLOUT);
        Ok(actual)
    }

    fn write(&self, file: &FileObject, task: &Task, data: &[UserBuffer]) -> Result<usize, Errno> {
        let mut pipe = self.pipe.lock();
        if pipe.reader_count == 0 {
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

        file.node().observers.notify(FdEvents::POLLIN);
        Ok(actual)
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

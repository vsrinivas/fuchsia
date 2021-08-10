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
use crate::signals::{signal_handling::send_checked_signal, *};
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

    fn read(&mut self, task: &Task, it: &mut UserBufferIterator<'_>) -> Result<usize, Errno> {
        // If there isn't any data to read from the pipe, then the behavior
        // depends on whether there are any open writers. If there is an
        // open writer, then we return EAGAIN, to signal that the callers
        // should wait for the writer to write something into the pipe.
        // Otherwise, we'll fall through the rest of this function and
        // return that we have read zero bytes, which will let the caller
        // know that they're done reading the pipe.
        if self.used == 0 && self.writer_count > 0 {
            return Err(EAGAIN);
        }

        let mut dst = UserBuffer::default();
        let mut dst_offset = 0;

        let mut src = vec![];
        let mut src_offset = 0;

        // We hold the lock on the pipe, which means we can compute exactly
        // how much we can read: either all there is to read or all that will
        // fit in the user's buffer.
        let actual = std::cmp::min(self.used, it.remaining());
        let mut remaining = actual;
        while remaining > 0 {
            // If dst_offset has reached dst.length, we're done with this
            // user buffer and we need to move to the next one. Such a buffer
            // should always exist because we the iterator promised us enough
            // space to reach |remaining|.
            if dst_offset == dst.length {
                dst = it.next(remaining).ok_or(EFAULT)?;
                dst_offset = 0;
            }

            // If src_offset has reached src.len(), we're done with this
            // run of data in the pipe and we need to move on to the next
            // one. Such a chunk should always exist because the pipe promised
            // us enough data to reach |remaining|.
            if src_offset == src.len() {
                src = self.pop_front().ok_or(EFAULT)?;
                src_offset = 0;
            }

            // The amount we can read in this iteration is either whatever
            // remains in this user buffer or whatever remains in this run of
            // data in the pipe.
            let chunk_size = std::cmp::min(dst.length - dst_offset, src.len() - src_offset);
            let src_end = src_offset + chunk_size;

            // Actually write this chunk out to user memory.
            let result = task.mm.write_memory(dst.address + dst_offset, &src[src_offset..src_end]);

            remaining -= chunk_size;

            // We're done if we fail to write to user memory or if we have
            // read everything we planned to read.
            if result.is_err() || remaining == 0 {
                // It's possible that we did not complete reading the last Vec
                // we popped off the front of the pipe. In order to not lose
                // that data, we need to remove the part we have already read
                // and put the rest back on the front of the pipe.
                //
                // This algorithm can have some pathological behavior if there
                // is a large run at the start of the pipe and the user issues
                // tiny reads because each one will end up copying most of the
                // large run. We can optimize that case by keeping an offset
                // into the first run rather than copying the data each time.
                if src_end != src.len() {
                    let leftover = src[src_end..].to_vec();
                    self.push_front(leftover);
                }
                result?;
                break;
            }

            dst_offset += chunk_size;
            src_offset += chunk_size;
        }

        Ok(actual)
    }

    fn write(&mut self, task: &Task, it: &mut UserBufferIterator<'_>) -> Result<usize, Errno> {
        if self.reader_count == 0 {
            send_checked_signal(task, Signal::SIGPIPE);
            return Err(EPIPE);
        }

        let mut remaining = self.get_available();
        let actual = std::cmp::min(remaining, it.remaining());

        while let Some(buffer) = it.next(remaining) {
            let mut bytes = vec![0u8; buffer.length];
            task.mm.read_memory(buffer.address, &mut bytes[..])?;
            self.push_back(bytes);
            remaining -= buffer.length;
        }

        Ok(actual)
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
    let node = FsNode::new(Box::new(Anon), FileMode::from_bits(0o600), pipe_fs(kernel));
    node.info_write().blksize = ATOMIC_IO_BYTES;

    let open = |flags: OpenFlags| {
        let ops = Pipe::open(&pipe, flags);
        FileObject::new_anonymous(ops, Arc::clone(&node), flags)
    };

    (open(OpenFlags::RDONLY), open(OpenFlags::WRONLY))
}

struct PipeFs;
impl FileSystemOps for PipeFs {}
fn pipe_fs(kernel: &Kernel) -> &FileSystemHandle {
    kernel.pipe_fs.get_or_init(|| FileSystem::new_no_root(PipeFs))
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
        let mut it = UserBufferIterator::new(data);
        let actual = self.pipe.lock().read(task, &mut it)?;
        if actual > 0 {
            file.node().observers.notify(FdEvents::POLLOUT);
        }
        Ok(actual)
    }

    fn write(&self, file: &FileObject, task: &Task, data: &[UserBuffer]) -> Result<usize, Errno> {
        let requested = UserBuffer::get_total_length(data);
        let mut actual = 0;
        let mut it = UserBufferIterator::new(data);
        file.blocking_op(
            task,
            || {
                actual += match self.pipe.lock().write(task, &mut it) {
                    Ok(chunk) => {
                        if chunk > 0 {
                            file.node().observers.notify(FdEvents::POLLIN);
                        }
                        chunk
                    }
                    Err(EPIPE) if actual > 0 => return Ok(actual),
                    Err(errno) => return Err(errno),
                };
                if actual < requested {
                    return Err(EAGAIN);
                }
                Ok(actual)
            },
            FdEvents::POLLOUT,
        )
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

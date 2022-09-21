// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon as zx;

use crate::fs::{buffers::*, socket::*, *};
use crate::syscalls::SyscallResult;
use crate::task::*;
use crate::types::*;

pub struct SocketFile {
    socket: SocketHandle,
}

impl FileOps for SocketFile {
    fileops_impl_nonseekable!();

    fn read(
        &self,
        file: &FileObject,
        current_task: &CurrentTask,
        data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        // The behavior of recv differs from read: recv will block if given a zero-size buffer when
        // there's no data available, but read will immediately return 0.
        if UserBuffer::get_total_length(data)? == 0 {
            return Ok(0);
        }
        let info = self.recvmsg(current_task, file, data, SocketMessageFlags::empty(), None)?;
        Ok(info.bytes_read)
    }

    fn write(
        &self,
        file: &FileObject,
        current_task: &CurrentTask,
        data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        self.sendmsg(current_task, file, data, None, vec![], SocketMessageFlags::empty())
    }

    fn wait_async(
        &self,
        _file: &FileObject,
        current_task: &CurrentTask,
        waiter: &Waiter,
        events: FdEvents,
        handler: EventHandler,
        options: WaitAsyncOptions,
    ) -> WaitKey {
        self.socket.wait_async(current_task, waiter, events, handler, options)
    }

    fn cancel_wait(&self, current_task: &CurrentTask, waiter: &Waiter, key: WaitKey) {
        self.socket.cancel_wait(current_task, waiter, key);
    }

    fn query_events(&self, current_task: &CurrentTask) -> FdEvents {
        self.socket.query_events(current_task)
    }

    fn ioctl(
        &self,
        _file: &FileObject,
        current_task: &CurrentTask,
        request: u32,
        user_addr: UserAddress,
    ) -> Result<SyscallResult, Errno> {
        self.socket.ioctl(current_task, request, user_addr)
    }

    fn close(&self, _file: &FileObject) {
        self.socket.close();
    }
}

impl SocketFile {
    pub fn new(socket: SocketHandle) -> Box<Self> {
        Box::new(SocketFile { socket })
    }

    /// Writes the provided data into the socket in this file.
    ///
    /// The provided control message is
    ///
    /// # Parameters
    /// - `task`: The task that the user buffers belong to.
    /// - `file`: The file that will be used for the `blocking_op`.
    /// - `data`: The user buffers to read data from.
    /// - `control_bytes`: Control message bytes to write to the socket.
    pub fn sendmsg(
        &self,
        current_task: &CurrentTask,
        file: &FileObject,
        data: &[UserBuffer],
        mut dest_address: Option<SocketAddress>,
        mut ancillary_data: Vec<AncillaryData>,
        flags: SocketMessageFlags,
    ) -> Result<usize, Errno> {
        // TODO: Implement more `flags`.

        let requested = UserBuffer::get_total_length(data)?;
        let mut actual = 0;
        let mut user_buffers = UserBufferIterator::new(data);

        let mut op = || {
            let bytes_written = match self.socket.write(
                current_task,
                &mut user_buffers,
                &mut dest_address,
                &mut ancillary_data,
            ) {
                Err(e) if e == ENOTCONN && actual > 0 => {
                    // If the error is ENOTCONN (that is, the write failed because the socket was
                    // disconnected), then return the amount of bytes that were written before
                    // the disconnect.
                    return Ok(BlockableOpsResult::Done(actual));
                }
                result => result,
            }?;

            actual += bytes_written;

            if actual < requested {
                Ok(BlockableOpsResult::Partial(actual))
            } else {
                Ok(BlockableOpsResult::Done(actual))
            }
        };

        if flags.contains(SocketMessageFlags::DONTWAIT) {
            op().map(BlockableOpsResult::value)
        } else {
            let deadline = self.socket.send_timeout().map(zx::Time::after);
            file.blocking_op(current_task, op, FdEvents::POLLOUT | FdEvents::POLLHUP, deadline)
        }
    }

    /// Reads data from the socket in this file into `data`.
    ///
    /// # Parameters
    /// - `file`: The file that will be used to wait if necessary.
    /// - `task`: The task that the user buffers belong to.
    /// - `data`: The user buffers to write to.
    ///
    /// Returns the number of bytes read, as well as any control message that was encountered.
    pub fn recvmsg(
        &self,
        current_task: &CurrentTask,
        file: &FileObject,
        data: &[UserBuffer],
        flags: SocketMessageFlags,
        deadline: Option<zx::Time>,
    ) -> Result<MessageReadInfo, Errno> {
        // TODO: Implement more `flags`.

        let op = || {
            let mut user_buffers = UserBufferIterator::new(data);
            self.socket.read(current_task, &mut user_buffers, flags).map(BlockableOpsResult::Done)
        };
        if flags.contains(SocketMessageFlags::DONTWAIT) {
            op().map(BlockableOpsResult::value)
        } else {
            let deadline = deadline.or_else(|| self.socket.receive_timeout().map(zx::Time::after));
            file.blocking_op(current_task, op, FdEvents::POLLIN | FdEvents::POLLHUP, deadline)
        }
    }
}

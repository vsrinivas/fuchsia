// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::sync::Arc;

use crate::errno;
use crate::error;
use crate::fd_impl_nonseekable;
use crate::fs::buffers::*;
use crate::fs::socket::*;
use crate::fs::*;
use crate::task::*;
use crate::types::*;

pub struct SocketFile {
    socket: SocketHandle,
}

impl FileOps for SocketFile {
    fd_impl_nonseekable!();

    fn read(&self, file: &FileObject, task: &Task, data: &[UserBuffer]) -> Result<usize, Errno> {
        self.recvmsg(task, file, data).map(|(bytes_read, _, _)| bytes_read)
    }

    fn write(&self, file: &FileObject, task: &Task, data: &[UserBuffer]) -> Result<usize, Errno> {
        self.sendmsg(task, file, data, None)
    }

    fn wait_async(
        &self,
        _file: &FileObject,
        waiter: &Arc<Waiter>,
        events: FdEvents,
        handler: EventHandler,
    ) {
        self.socket.lock().wait_async(waiter, events, handler)
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
        task: &Task,
        file: &FileObject,
        data: &[UserBuffer],
        mut ancillary_data: Option<AncillaryData>,
    ) -> Result<usize, Errno> {
        let requested = UserBuffer::get_total_length(data);
        let mut actual = 0;
        let mut user_buffers = UserBufferIterator::new(data);
        file.blocking_op(
            task,
            || {
                let socket = self.socket.lock();

                let bytes_written = match socket.write(task, &mut user_buffers, &mut ancillary_data)
                {
                    Err(e) if e == ENOTCONN && actual > 0 => {
                        // If the error is ENOTCONN (that is, the write failed because the socket was
                        // disconnected), then return the amount of bytes that were written before
                        // the disconnect.
                        return Ok(actual);
                    }
                    result => result,
                }?;

                actual += bytes_written;

                if actual < requested {
                    return error!(EAGAIN);
                }

                Ok(actual)
            },
            FdEvents::POLLOUT | FdEvents::POLLHUP,
        )
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
        task: &Task,
        file: &FileObject,
        data: &[UserBuffer],
    ) -> Result<(usize, Option<SocketAddress>, Option<AncillaryData>), Errno> {
        file.blocking_op(
            task,
            || {
                let mut user_buffers = UserBufferIterator::new(data);
                self.socket.lock().read(task, &mut user_buffers)
            },
            FdEvents::POLLIN | FdEvents::POLLHUP,
        )
    }
}

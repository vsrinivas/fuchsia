// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;

use crate::errno;
use crate::error;
use crate::fd_impl_nonseekable;
use crate::fs::{FdEvents, FileObject, FileOps, SeekOrigin};
use crate::task::Task;
use crate::types::*;

pub struct SocketFile {
    /// The socket associated with this file, used to read and write data.
    socket: SocketHandle,
}

impl FileOps for SocketFile {
    fd_impl_nonseekable!();

    fn read(&self, file: &FileObject, task: &Task, data: &[UserBuffer]) -> Result<usize, Errno> {
        let mut socket = self.socket.lock();
        let pipe = socket.pipe();

        let mut it = UserBufferIterator::new(data);
        let bytes_read = pipe.read(task, &mut it)?;
        if bytes_read > 0 {
            file.node().observers.notify(FdEvents::POLLOUT);
            socket.connected_node().map(|connected_node| {
                connected_node.observers.notify(FdEvents::POLLOUT);
            });
        }

        Ok(bytes_read)
    }

    fn write(&self, file: &FileObject, task: &Task, data: &[UserBuffer]) -> Result<usize, Errno> {
        let requested = UserBuffer::get_total_length(data);
        let mut actual = 0;
        let mut it = UserBufferIterator::new(data);
        file.blocking_op(
            task,
            || {
                let connected_node = match self.socket.lock().connected_node() {
                    Some(node) => node,
                    _ => return Ok(actual),
                };
                let mut connected_socket = match connected_node.socket() {
                    Some(socket) => socket.lock(),
                    _ => return Ok(actual),
                };
                actual += match connected_socket.pipe().write(task, &mut it) {
                    Ok(chunk) => {
                        if chunk > 0 {
                            connected_node.observers.notify(FdEvents::POLLIN);
                        }
                        chunk
                    }
                    Err(errno) if errno == EPIPE && actual > 0 => return Ok(actual),
                    Err(errno) => return Err(errno),
                };
                if actual < requested {
                    return error!(EAGAIN);
                }
                Ok(actual)
            },
            FdEvents::POLLOUT,
        )
    }
}

impl SocketFile {
    pub fn new(socket: SocketHandle) -> Box<Self> {
        Box::new(SocketFile { socket })
    }
}

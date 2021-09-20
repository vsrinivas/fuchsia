// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;

use crate::errno;
use crate::error;
use crate::fd_impl_nonseekable;
use crate::fs::buffers::*;
use crate::fs::{FdEvents, FileObject, FileOps, SeekOrigin};
use crate::signals::{signal_handling::send_checked_signal, *};
use crate::task::Task;
use crate::types::*;

pub struct SocketFile {
    /// The socket associated with this file, used to read and write data.
    socket: SocketHandle,
}

impl FileOps for SocketFile {
    fd_impl_nonseekable!();

    fn read(&self, _file: &FileObject, task: &Task, data: &[UserBuffer]) -> Result<usize, Errno> {
        let mut socket = self.socket.lock();
        let mut it = UserBufferIterator::new(data);
        let bytes_read =
            SocketFile::read_to_user_buffer(socket.incoming_messages(), task, &mut it)?;

        if bytes_read == 0 && socket.is_connected() {
            // If no bytes were read, but the socket is connected, return an error indicating that
            // the reader can wait.
            return error!(EAGAIN);
        }

        if bytes_read > 0 {
            // If bytes were read, notify the connected node that there are bytes to read.
            socket.connected_node().map(|connected_node| {
                connected_node.notify(FdEvents::POLLOUT);
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
                let socket = self.get_socket_for_writing(task)?;

                let bytes_written = match SocketFile::write_from_user_buffer(
                    socket.lock().incoming_messages(),
                    task,
                    &mut it,
                ) {
                    Err(e) if e == EPIPE && actual > 0 => {
                        // If the error is EPIPE (that is, the write failed because the socket was
                        // disconnected), then return the amount of bytes that were written before
                        // the disconnect.
                        return Ok(actual);
                    }
                    result => result,
                }?;

                if bytes_written > 0 {
                    socket.lock().notify_write();
                }

                actual += bytes_written;
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

    /// Returns the file's socket after verifying that it is still connected.
    ///
    /// If the socket is not connected, signals a SIGPIPE to `task` and returns EPIPE.
    pub fn get_socket_for_writing(&self, task: &Task) -> Result<SocketHandle, Errno> {
        match self.socket.lock().connected_socket() {
            Some(socket) => Ok(socket),
            None => {
                send_checked_signal(task, Signal::SIGPIPE);
                error!(EPIPE)
            }
        }
    }

    /// Reads from `message_buffer` into the user memory in `it`.
    ///
    /// # Parameters
    /// - `message_buffer`: The message buffer to read from.
    /// - `task`: The task that is performing the read, and to which the data will be written.
    /// - `it`: The `UserBufferIterator` to write the data to.
    ///
    /// Returns the number of bytes that were read from the socket.
    fn read_to_user_buffer(
        message_buffer: &mut MessageBuffer,
        task: &Task,
        it: &mut UserBufferIterator<'_>,
    ) -> Result<usize, Errno> {
        let mut total_bytes_read = 0;

        while !message_buffer.is_empty() {
            if let Some(mut user_buffer) = it.next(usize::MAX) {
                // Try to read enough bytes to fill the current user buffer.
                let (messages, bytes_read) = message_buffer.read_bytes(user_buffer.length);

                for message in messages {
                    match message {
                        Message::Packet(packet) => {
                            task.mm.write_memory(user_buffer.address, packet.bytes())?;
                            // Update the user address to write to.
                            user_buffer.address += packet.len();
                            total_bytes_read += packet.len();
                        }
                        // Immediately return when a control message is encountered.
                        // TODO: The control message needs to be returned out to the reader.
                        _ => return Ok(total_bytes_read),
                    }
                }

                if bytes_read < user_buffer.length {
                    // If the buffer was not filled, break out of the loop.
                    break;
                }
            }
        }

        Ok(total_bytes_read)
    }

    /// Writes the provided `UserBufferIterator` into the write socket for this file.
    ///
    /// # Parameters
    /// - `task`: The task to read memory from.
    /// - `it`: The `UserBufferIterator` to read the data from.
    ///
    /// Returns the number of bytes that were written to the socket.
    fn write_from_user_buffer(
        message_buffer: &mut MessageBuffer,
        task: &Task,
        it: &mut UserBufferIterator<'_>,
    ) -> Result<usize, Errno> {
        let mut bytes_written = 0;
        while let Some(buffer) = it.next(usize::MAX) {
            let mut bytes = vec![0u8; buffer.length];
            task.mm.read_memory(buffer.address, &mut bytes[..])?;

            bytes_written += bytes.len();
            message_buffer.write(Message::packet(bytes));
        }

        Ok(bytes_written)
    }
}

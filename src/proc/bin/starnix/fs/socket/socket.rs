// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::errno;
use crate::error;
use crate::fd_impl_nonseekable;
use crate::fs::pipe::*;
use crate::fs::*;
use crate::mm::PAGE_SIZE;
use crate::mode;
use crate::task::Kernel;
use crate::task::Task;
use crate::types::locking::*;
use crate::types::*;

use parking_lot::Mutex;
use std::sync::{Arc, Weak};

/// `SocketFs` is the file system where anonymous socket nodes are created, for example in
/// `sys_socket`.
pub struct SocketFs {}
impl FileSystemOps for SocketFs {
    fn statfs(&self, _fs: &FileSystem) -> Result<statfs, Errno> {
        let mut stat = statfs::default();
        stat.f_type = SOCKFS_MAGIC as i64;
        stat.f_bsize = *PAGE_SIZE;
        stat.f_namelen = NAME_MAX as i64;
        Ok(stat)
    }
}

/// Returns a handle to the `SocketFs` instance in `kernel`, initializing it if needed.
pub fn socket_fs(kernel: &Kernel) -> &FileSystemHandle {
    kernel.socket_fs.get_or_init(|| FileSystem::new(SocketFs {}))
}

/// Creates a `FileHandle` where the associated `FsNode` contains a socket.
///
/// # Parameters
/// - `kernel`: The kernel that is used to fetch `SocketFs`, to store the created socket node.
/// - `open_flags`: The `OpenFlags` which are used to create the `FileObject`.
pub fn new_socket(kernel: &Kernel, open_flags: OpenFlags) -> FileHandle {
    let fs = socket_fs(kernel);
    let mode = mode!(IFSOCK, 0o777);
    let socket = Socket::new();
    let node = fs.create_node(Box::new(Anon), mode);
    node.set_socket(socket.clone());

    FileObject::new_anonymous(Box::new(SocketFile { socket }), Arc::clone(&node), open_flags)
}

#[derive(Default)]
pub struct Socket {
    /// The `FsNode` that contains the socket that is connected to this socket, if such a node
    /// has set via `Socket::connect`.
    connected_node: Weak<FsNode>,

    /// The ancilarry data currently associated with the `write_pipe` of this socket.
    /// TODO: This should be sent in-band to be able to implement the expected socket semantics.
    control_message: Option<Vec<u8>>,

    /// The pipe that is used to send data from this socket. The `write_pipe` of the socket in
    /// `connected_node` is used to receive data.
    // TODO: Extract the byte-shuffling code from Pipe, and reuse that here. This will allow us to
    // send `ancillary_data` in-band, support datagrams, etc.
    write_pipe: Pipe,
}

/// A `SocketHandle` is a `Socket` wrapped in a `Arc<Mutex<..>>`. This is used to share sockets
/// between file nodes.
pub type SocketHandle = Arc<Mutex<Socket>>;

impl Socket {
    pub fn new() -> SocketHandle {
        Arc::new(Mutex::new(Socket::default()))
    }

    /// Connects the sockets in the provided `FsNodeHandle`s together.
    ///
    /// After the connection has been established, the sockets in each node will read/write from
    /// each other.
    ///
    /// WARNING: It's an error to call `connect` with nodes that do not contain sockets.
    ///
    /// Returns an error if any of the sockets are already connected.
    pub fn connect(first_node: &FsNodeHandle, second_node: &FsNodeHandle) -> Result<(), Errno> {
        // Sort the nodes to determine which order to lock them in.
        let mut ordered_nodes: [&FsNodeHandle; 2] = [first_node, second_node];
        sort_for_locking(&mut ordered_nodes, |node| node.socket().unwrap());

        let first_node = ordered_nodes[0];
        let second_node = ordered_nodes[1];

        // Lock the sockets in a consistent order.
        let mut first_socket = first_node.socket().ok_or(ENOTSOCK)?.lock();
        if first_socket.connected_node.upgrade().is_some() {
            return error!(EISCONN);
        }

        // Make sure not to deadlock in the case where the two references point to the same node.
        if std::ptr::eq(first_node, second_node) {
            first_socket.connected_node = Arc::downgrade(first_node);
            first_socket.write_pipe.add_reader();
            first_socket.write_pipe.add_writer();
            return Ok(());
        }

        let mut second_socket = second_node.socket().ok_or(ENOTSOCK)?.lock();
        if second_socket.connected_node.upgrade().is_some() {
            return error!(EISCONN);
        }

        first_socket.connected_node = Arc::downgrade(second_node);
        second_socket.connected_node = Arc::downgrade(first_node);

        // TODO: This is here to make the Pipe's reading/writing logic match the socket's
        // expectations. Remove once the Pipe byte-sending logic is extracted.
        first_socket.write_pipe.add_reader();
        first_socket.write_pipe.add_writer();
        second_socket.write_pipe.add_reader();
        second_socket.write_pipe.add_writer();

        Ok(())
    }

    /// Disconnects this socket from the socket in the currently connected file node.
    pub fn disconnect(&mut self) -> Option<FsNodeHandle> {
        self.write_pipe.remove_reader();
        self.write_pipe.remove_writer();

        let connected_node = self.connected_node.upgrade();
        connected_node.as_ref().map(|node| node.observers.notify(FdEvents::POLLHUP));
        self.connected_node = Weak::new();
        connected_node
    }

    /// Gets the ancillary data associated with this socket.
    ///
    /// If ancillary data is returned, the ancillary data is cleared out and subsequent calls will
    /// return `None` until `set_ancillary_message` is called again.
    // TODO: This data should be sent in-band.
    pub fn take_control_message(&mut self) -> Option<Vec<u8>> {
        self.control_message.take()
    }

    /// Sets the ancillary data associated with this socket.
    // TODO: This data should be sent in-band.
    pub fn set_control_message(&mut self, message: Vec<u8>) {
        assert!(self.control_message.is_none());
        self.control_message = Some(message);
    }

    pub fn connected_socket(&self) -> Option<SocketHandle> {
        self.connected_node.upgrade().and_then(|node| node.socket().map(|s| s.clone()))
    }
}

struct SocketFile {
    /// The socket associated with this file, used to read and write data.
    socket: SocketHandle,
}

impl FileOps for SocketFile {
    fd_impl_nonseekable!();

    fn read(&self, _file: &FileObject, task: &Task, data: &[UserBuffer]) -> Result<usize, Errno> {
        // TODO: There may still be data in the pipe at this point, but the connected node might
        // have been released. Might have to store the pipe separately, with a strong reference,
        // to deal with this properly.
        let other_node = match self.socket.lock().connected_node.upgrade() {
            Some(node) => node,
            _ => return Ok(0),
        };
        let mut socket = match other_node.socket() {
            Some(socket) => socket.lock(),
            _ => return Ok(0),
        };

        let pipe = &mut socket.write_pipe;
        let mut it = UserBufferIterator::new(data);
        let bytes_read = pipe.read(task, &mut it)?;
        if bytes_read > 0 {
            other_node.observers.notify(FdEvents::POLLOUT);
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
                let mut socket = self.socket.lock();
                actual += match socket.write_pipe.write(task, &mut it) {
                    Ok(chunk) => {
                        if chunk > 0 {
                            if let Some(other_node) = socket.connected_node.upgrade() {
                                other_node.observers.notify(FdEvents::POLLIN);
                            }
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

// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;

use crate::error;
use crate::fs::buffers::*;
use crate::fs::*;
use crate::mode;
use crate::task::{Kernel, Task};
use crate::types::locking::*;
use crate::types::*;

use parking_lot::Mutex;
use std::sync::{Arc, Weak};

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

    FileObject::new_anonymous(SocketFile::new(socket), Arc::clone(&node), open_flags)
}

// TODO: Sockets should have a maximum capacity.
pub struct Socket {
    /// The `FsNode` that contains the socket that is connected to this socket, if such a node
    /// has set via `Socket::connect`.
    connected_node: Weak<FsNode>,

    /// The `MessageBuffer` that contains incoming messages for this socket.
    ///
    /// A socket writes to `connected_node`'s `incoming_messages`.
    incoming_messages: MessageBuffer,

    /// Whether or not the socket has been bound to an address.
    // TODO: Set this correctly when the socket is bound to an address/infer this from connection
    // state.
    is_bound: bool,
}

impl Default for Socket {
    fn default() -> Self {
        Socket {
            connected_node: Weak::default(),
            // TODO: Set the capacity to a more accurate value.
            incoming_messages: MessageBuffer::new(usize::MAX),
            is_bound: false,
        }
    }
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
        assert!(first_node.socket().is_some());
        assert!(second_node.socket().is_some());

        // Sort the nodes to determine which order to lock them in.
        let mut ordered_nodes: [&FsNodeHandle; 2] = [first_node, second_node];
        sort_for_locking(&mut ordered_nodes, |node| node.socket().unwrap());

        let first_node = ordered_nodes[0];
        let second_node = ordered_nodes[1];

        // Lock the sockets in a consistent order.
        let mut first_socket = first_node.socket().unwrap().lock();
        if first_socket.connected_node.upgrade().is_some() {
            return error!(EISCONN);
        }

        // Make sure not to deadlock in the case where the two references point to the same node.
        if std::ptr::eq(first_node, second_node) {
            first_socket.connected_node = Arc::downgrade(first_node);
            return Ok(());
        }

        let mut second_socket = second_node.socket().unwrap().lock();
        if second_socket.connected_node.upgrade().is_some() {
            return error!(EISCONN);
        }

        first_socket.connected_node = Arc::downgrade(second_node);
        second_socket.connected_node = Arc::downgrade(first_node);

        Ok(())
    }

    /// Disconnects the two sockets in the provided file nodes.
    ///
    /// WARNING: It is an error to call this function with nodes that do not contain sockets.
    pub fn disconnect(first_node: &FsNodeHandle, second_node: &FsNodeHandle) {
        assert!(first_node.socket().is_some());
        assert!(second_node.socket().is_some());

        // Sort the nodes to determine which order to lock them in.
        let mut ordered_nodes: [&FsNodeHandle; 2] = [first_node, second_node];
        sort_for_locking(&mut ordered_nodes, |node| node.socket().unwrap());

        let first_node = ordered_nodes[0];
        let second_node = ordered_nodes[1];
        let mut first_socket = first_node.socket().unwrap().lock();
        let mut second_socket = second_node.socket().unwrap().lock();

        first_socket.connected_node = Weak::new();
        second_socket.connected_node = Weak::new();

        // Notify the node observers to wake them up if they are waiting on a read/write.
        first_node.notify(FdEvents::POLLHUP);
        second_node.notify(FdEvents::POLLHUP);
    }

    /// Returns the node that contains the socket that is connected to this socket, if such a node
    /// exists.
    pub fn connected_node(&self) -> Option<FsNodeHandle> {
        self.connected_node.upgrade()
    }

    /// Returns the socket that is connected to this socket, if such a socket exists.
    pub fn connected_socket(&self) -> Option<SocketHandle> {
        self.connected_node.upgrade().and_then(|node| node.socket().map(|s| s.clone()))
    }

    /// Returns true if this socket has been bound to an address.
    pub fn is_bound(&self) -> bool {
        self.is_bound
    }

    /// Returns true if this socket is connected to another socket.
    pub fn is_connected(&self) -> bool {
        self.connected_node().is_some()
    }

    /// Notifies the observers of the connected node that data has been written to the connected
    /// socket.
    pub fn notify_write(&self) {
        self.connected_node().map(|node| {
            node.notify(FdEvents::POLLIN);
        });
    }

    /// Writes the the contents of `UserBufferIterator` into this socket.
    ///
    /// # Parameters
    /// - `task`: The task to read memory from.
    /// - `user_buffers`: The `UserBufferIterator` to read the data from.
    ///
    /// Returns the number of bytes that were written to the socket.
    pub fn write(
        &mut self,
        task: &Task,
        user_buffers: &mut UserBufferIterator<'_>,
    ) -> Result<usize, Errno> {
        self.incoming_messages.write(task, user_buffers)
    }

    /// Reads the the contents of this socket into `UserBufferIterator`.
    ///
    /// # Parameters
    /// - `task`: The task to read memory from.
    /// - `user_buffers`: The `UserBufferIterator` to write the data to.
    ///
    /// Returns the number of bytes that were read into the buffer, and a control message if one was
    /// read from the socket.
    pub fn read(
        &mut self,
        task: &Task,
        user_buffers: &mut UserBufferIterator<'_>,
    ) -> Result<(usize, Option<Control>), Errno> {
        let bytes_read = self.incoming_messages.read(task, user_buffers)?;
        Ok((bytes_read, self.incoming_messages.read_if_control()))
    }

    /// Writes a `Message::Control` containing the provided bytes to the socket.
    pub fn write_control(&mut self, bytes: Vec<u8>) {
        if bytes.len() > 0 {
            self.incoming_messages.write_message(Message::control(bytes));
        }
    }
}

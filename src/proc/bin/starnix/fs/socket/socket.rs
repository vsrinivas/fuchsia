// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;

use crate::error;
use crate::fs::pipe::*;
use crate::fs::*;
use crate::mode;
use crate::task::Kernel;
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

#[derive(Default)]
pub struct Socket {
    /// The `FsNode` that contains the socket that is connected to this socket, if such a node
    /// has set via `Socket::connect`.
    connected_node: Weak<FsNode>,

    /// The ancilarry data currently associated with the `write_pipe` of this socket.
    /// TODO: This should be sent in-band to be able to implement the expected socket semantics.
    control_message: Option<Vec<u8>>,

    /// The pipe that is used to read data for this socket. The `read_pipe` of the socket in
    /// `connected_node` is used to write data.
    // TODO: Extract the byte-shuffling code from Pipe, and reuse that here. This will allow us to
    // send `control_message` in-band, support datagrams, etc.
    read_pipe: Pipe,

    /// Whether or not the socket accepts incoming connections.
    accepts_connections: bool,
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
            first_socket.read_pipe.add_reader();
            first_socket.read_pipe.add_writer();
            return Ok(());
        }

        let mut second_socket = second_node.socket().unwrap().lock();
        if second_socket.connected_node.upgrade().is_some() {
            return error!(EISCONN);
        }

        first_socket.connected_node = Arc::downgrade(second_node);
        second_socket.connected_node = Arc::downgrade(first_node);

        // TODO: This is here to make the Pipe's reading/writing logic match the socket's
        // expectations. Remove once the Pipe byte-sending logic is extracted.
        first_socket.read_pipe.add_reader();
        first_socket.read_pipe.add_writer();
        second_socket.read_pipe.add_reader();
        second_socket.read_pipe.add_writer();

        Ok(())
    }

    /// Disconnects this socket from the socket in the currently connected file node.
    pub fn disconnect(&mut self) -> Option<FsNodeHandle> {
        self.read_pipe.remove_reader();

        let connected_node = self.connected_node.upgrade();
        connected_node.as_ref().map(|node| {
            node.socket().map(|s| s.lock().read_pipe.remove_writer());
            node.notify(FdEvents::POLLHUP);
        });
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

    pub fn connected_node(&self) -> Option<FsNodeHandle> {
        self.connected_node.upgrade()
    }

    pub fn connected_socket(&self) -> Option<SocketHandle> {
        self.connected_node.upgrade().and_then(|node| node.socket().map(|s| s.clone()))
    }

    /// Returns whether or not this socket is configured to accept connections.
    pub fn accepts_connections(&self) -> bool {
        self.accepts_connections
    }

    pub fn pipe(&mut self) -> &mut Pipe {
        &mut self.read_pipe
    }
}

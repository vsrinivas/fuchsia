// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::fs::*;
use crate::mm::PAGE_SIZE;
use crate::mode;
use crate::task::Kernel;
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
    let node = fs.create_node(Box::new(SpecialNode), mode);
    node.set_socket(Socket::new());

    FileObject::new_anonymous(Box::new(NullFile), Arc::clone(&node), open_flags)
}

#[derive(Default)]
pub struct Socket {
    /// The `FsNode` that contains the socket that is connected to this socket, if such a node
    /// has set via `Socket::connect`.
    connected_node: Weak<FsNode>,
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
    /// Returns an error if both nodes do not contain sockets.
    pub fn connect(first_node: &FsNodeHandle, second_node: &FsNodeHandle) -> Result<(), Errno> {
        // TODO: fix locks
        let mut first_socket = first_node.socket().ok_or(ENOTSOCK)?.lock();
        let mut second_socket = second_node.socket().ok_or(ENOTSOCK)?.lock();

        // TODO: Return a proper error when a socket is already connected.
        assert!(first_socket.connected_node.upgrade().is_none());
        assert!(second_socket.connected_node.upgrade().is_none());

        first_socket.connected_node = Arc::downgrade(second_node);
        second_socket.connected_node = Arc::downgrade(first_node);
        Ok(())
    }

    /// Returns the socket that is connected to this socket, if such a socket exists.
    #[cfg(test)]
    pub fn connected_socket(&self) -> Option<SocketHandle> {
        self.connected_node.upgrade().and_then(|node| node.socket().map(|s| s.clone()))
    }
}

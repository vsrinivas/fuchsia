// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use zerocopy::AsBytes;

use super::*;

use crate::error;
use crate::fs::buffers::*;
use crate::fs::*;
use crate::mode;
use crate::task::{Kernel, Task};
use crate::types::*;

use parking_lot::Mutex;
use std::sync::{Arc, Weak};

/// A `Socket` represents one endpoint of a bidirectional communication channel.
///
/// The `Socket` enum represents the state that the socket is in. When a socket is created it is
/// `Unbound`, when the socket is bound (e.g., via `sys_bind`) `Bound`, etc.
///
/// A `Socket` always contains a local `SocketEndpoint`. Most of the socket's state is stored in
/// its `SocketEndpoint`.
///
/// When a socket is connected, the remote `SocketEndpoint` can be retrieved from its
/// `SocketConnection`. This can be used to, for example, write to the remote socket.
pub enum Socket {
    /// A socket that is `Unbound` has just been created, but not been bound to an address or
    /// connected.
    Unbound(SocketEndpointHandle),

    /// A `Bound` socket has been bound to a address, but is not listening.
    ///
    /// The `Weak<FsNode>` is a pointer to the node at which the socket is bound.
    Bound(SocketEndpointHandle),

    /// An `Active` socket is connected, and is considered the `active` socket in that connection.
    ///
    /// The sockets are peers, and both can be written to and read from. The distinction is used
    /// to determine which endpoint to read and write from.
    ///
    /// Both the connected sockets share the same `SocketConnection`.
    Active(SocketConnectionHandle),

    /// A `Passive` socket is connected, and is considered the `passive` socket in that connection.
    ///
    /// The sockets are peers, and both can be written to and read from. The distinction is used
    /// to determine which endpoint to read and write from.
    ///
    /// Both the connected sockets share the same `SocketConnection`.
    Passive(SocketConnectionHandle),

    /// A `Shutdown` socket has been connected at some point, but the connection has been
    /// shut down.
    Shutdown(SocketEndpointHandle),
}

/// A `SocketHandle` is a `Socket` wrapped in a `Arc<Mutex<..>>`.
pub type SocketHandle = Arc<Mutex<Socket>>;

impl Socket {
    /// Creates a new socket associated with the provided `FsNode`.
    ///
    /// # Parameters
    /// - `node`: The node that the socket is associated with.
    /// - `domain`: The domain of the socket (e.g., `AF_UNIX`).
    pub fn new(node: Weak<FsNode>, domain: SocketDomain) -> SocketHandle {
        Arc::new(Mutex::new(Socket::Unbound(SocketEndpoint::new(node, domain))))
    }

    /// Creates a `FileHandle` where the associated `FsNode` contains a socket.
    ///
    /// # Parameters
    /// - `kernel`: The kernel that is used to fetch `SocketFs`, to store the created socket node.
    /// - `domain`: The domain of the socket (e.g., `AF_UNIX`).
    /// - `open_flags`: The `OpenFlags` which are used to create the `FileObject`.
    pub fn new_file(kernel: &Kernel, domain: SocketDomain, open_flags: OpenFlags) -> FileHandle {
        let fs = socket_fs(kernel);
        let mode = mode!(IFSOCK, 0o777);
        let node = fs.create_node(Box::new(Anon), mode);
        let socket = Socket::new(Arc::downgrade(&node), domain);
        node.set_socket(socket.clone());

        FileObject::new_anonymous(SocketFile::new(socket), node, open_flags)
    }

    /// Binds this socket to `node`.
    ///
    /// After this call any reads and writes to the socket will notify `node`.
    ///
    /// Returns an error if the socket could not be bound.
    pub fn bind(&mut self, socket_address: SocketAddress) -> Result<(), Errno> {
        let endpoint = match self {
            Socket::Unbound(endpoint) => Ok(endpoint),
            _ => error!(EINVAL),
        }?;
        {
            let mut locked_endpoint = endpoint.lock();
            locked_endpoint.address = socket_address;
        }
        *self = Socket::Bound(endpoint.clone());

        Ok(())
    }

    /// Unbinds the socket.
    ///
    /// This clears out the bound address of the endpoint, regardless of which state the socket is
    /// in.
    pub fn unbind(&mut self) {
        let mut endpoint = self.endpoint().lock();
        endpoint.address = SocketAddress::Unspecified;
    }

    /// Connects this socket to the provided socket.
    ///
    /// Note that this operation is bidirectional, and after the method returns both `self` and
    /// `socket` will be bound.
    ///
    /// # Parameters
    /// - `socket`: The socket to connect to this socket. The sockets will share a
    ///             `SocketConnectionHandle` once the connection completes.
    ///
    /// Returns an error if the sockets could not be connected (e.g., if one of the sockets is
    /// already connected).
    pub fn connect(&mut self, socket: &mut Socket) -> Result<(), Errno> {
        let (active, passive) = match (&self, &socket) {
            (Socket::Active(_), _) => error!(EISCONN),
            (Socket::Passive(_), _) => error!(EISCONN),
            (Socket::Unbound(active), Socket::Bound(passive)) => {
                Ok((active.clone(), passive.clone()))
            }
            _ => error!(ECONNREFUSED),
        }?;

        let connection = SocketConnection::new(active, passive);
        *self = Socket::Active(connection.clone());
        *socket = Socket::Passive(connection);

        Ok(())
    }

    /// Returns the socket endpoint that is connected to this socket, if such an endpoint exists.
    fn connected_endpoint(&self) -> Result<&SocketEndpointHandle, Errno> {
        match self {
            Socket::Active(connection) => Ok(&connection.passive),
            Socket::Passive(connection) => Ok(&connection.active),
            _ => error!(ENOTCONN),
        }
    }

    /// Returns the endpoint associated with this socket.
    ///
    /// If the socket is connected, this method will return the endpoint that this socket reads
    /// from (e.g., `connection.active` if the socket is `Active`).
    fn endpoint(&self) -> &SocketEndpointHandle {
        match self {
            Socket::Active(connection) => &connection.active,
            Socket::Passive(connection) => &connection.passive,
            Socket::Unbound(endpoint) => endpoint,
            Socket::Bound(endpoint) => endpoint,
            Socket::Shutdown(endpoint) => endpoint,
        }
    }

    /// Returns the name of this socket.
    ///
    /// The name is derived from the endpoint's address and domain. A socket will always have a
    /// name, even if it is not bound to an address.
    pub fn getsockname(&self) -> Vec<u8> {
        let endpoint = self.endpoint().lock();
        endpoint.name()
    }

    /// Returns the name of the peer of this socket, if such a peer exists.
    ///
    /// Returns an error if the socket is not connected.
    pub fn getpeername(&self) -> Result<Vec<u8>, Errno> {
        let endpoint = match self {
            Socket::Active(connection) => Ok(&connection.passive),
            _ => error!(ENOTCONN),
        }?
        .lock();
        Ok(endpoint.name())
    }

    /// Reads the specified number of bytes from the socket, if possible.
    ///
    /// # Parameters
    /// - `task`: The task to which the user buffers belong (i.e., the task to which the read bytes
    ///           are written.
    /// - `user_buffers`: The buffers to write the read data into.
    ///
    /// Returns the number of bytes that were written to the user buffers, as well as any ancillary
    /// data associated with the read messages.
    pub fn read(
        &self,
        task: &Task,
        user_buffers: &mut UserBufferIterator<'_>,
    ) -> Result<(usize, Option<AncillaryData>), Errno> {
        let endpoint = self.endpoint();
        endpoint.lock().read(task, user_buffers)
    }

    /// Shuts down this socket, preventing any future reads and/or writes.
    ///
    /// TODO: This should take a "how" parameter to indicate which operations should be prevented.
    ///
    /// Returns the file system node that this socket was connected to (this is useful to be able
    /// to notify the peer about the shutdown).
    pub fn shutdown(&mut self) -> Result<Option<FsNodeHandle>, Errno> {
        let (local_endpoint, remote_endpoint) = match &self {
            Socket::Active(connection) => {
                Ok((connection.active.clone(), connection.passive.clone()))
            }
            Socket::Passive(connection) => {
                Ok((connection.passive.clone(), connection.active.clone()))
            }
            _ => error!(ENOTCONN),
        }?;

        *self = Socket::Shutdown(local_endpoint.clone());
        local_endpoint.lock().node.upgrade().map(|node| {
            node.notify(FdEvents::POLLHUP);
        });

        let remote_node = remote_endpoint.lock().node.upgrade();
        Ok(remote_node)
    }

    /// Writes the data in the provided user buffers to this socket.
    ///
    /// # Parameters
    /// - `task`: The task to which the user buffers belong, used to read the memory.
    /// - `user_buffers`: The data to write to the socket.
    /// - `ancillary_data`: Optional ancillary data (a.k.a., control message) to write.
    ///
    /// Returns the number of bytes that were read from the user buffers and written to the socket,
    /// not counting the ancillary data.
    pub fn write(
        &self,
        task: &Task,
        user_buffers: &mut UserBufferIterator<'_>,
        ancillary_data: &mut Option<AncillaryData>,
    ) -> Result<usize, Errno> {
        let mut endpoint = self.connected_endpoint()?.lock();
        endpoint.write(task, user_buffers, ancillary_data)
    }
}

#[derive(Debug, Copy, Clone, PartialEq, Eq)]
pub enum SocketDomain {
    /// The `Unix` socket domain contains sockets that were created with the `AF_UNIX` domain. These
    /// sockets communicate locally, with other sockets on the same host machine.
    Unix,
}

#[derive(Debug, Clone)]
pub enum SocketAddress {
    /// An unspecified socket address means that the socket has yet to be bound to an address.
    /// When an address is not specified, the `SocketDomain` can be inspected to get the type of
    /// socket.
    Unspecified,

    /// A `Unix` socket address contains the filesystem path that was used to bind the socket.
    Unix(FsString),
}

/// A `SocketConnection` contains two connected sockets.
///
/// The `passive` endpoint was bound, and the `active` endpoint connected to the bound endpoint.
///
/// The distinction is not always important, but allows both endpoints to be stored on a single
/// connection instance.
///
/// Both the connected sockets share the same `SocketConnection`.
pub struct SocketConnection {
    /// The socket that initiated the connection.
    active: SocketEndpointHandle,

    /// The socket that was connected to.
    passive: SocketEndpointHandle,
}

/// A `SocketConnectionHandle` is a `SocketConnection` wrapped in a `Arc<Mutex<..>>`.
pub type SocketConnectionHandle = Arc<SocketConnection>;

impl SocketConnection {
    fn new(active: SocketEndpointHandle, passive: SocketEndpointHandle) -> SocketConnectionHandle {
        Arc::new(SocketConnection { active, passive })
    }
}

/// `SocketEndpoint` stores the state associated with a single endpoint of a socket.
pub struct SocketEndpoint {
    /// The `MessageBuffer` that contains messages for this socket endpoint.
    messages: MessageBuffer,

    /// The file system node that is associated with this socket endpoint. This node will be
    /// notified on reads, writes, disconnects etc.
    node: Weak<FsNode>,

    /// The domain of this socket endpoint.
    domain: SocketDomain,

    /// The address that this socket has been bound to, if it has been bound.
    address: SocketAddress,
}

/// A `SocketHandle` is a `Socket` wrapped in a `Arc<Mutex<..>>`. This is used to share sockets
/// between file nodes.
pub type SocketEndpointHandle = Arc<Mutex<SocketEndpoint>>;

impl SocketEndpoint {
    /// Creates a new socket endpoint within the specified socket domain.
    ///
    /// The socket endpoint's address is set to a default value for the specified domain.
    pub fn new(node: Weak<FsNode>, domain: SocketDomain) -> SocketEndpointHandle {
        let address = match domain {
            SocketDomain::Unix => SocketAddress::Unix(b"".to_vec()),
        };
        Arc::new(Mutex::new(SocketEndpoint {
            messages: MessageBuffer::new(usize::MAX),
            node,
            domain,
            address,
        }))
    }

    /// Returns the name of the endpoint.
    fn name(&self) -> Vec<u8> {
        let default_name = match self.domain {
            SocketDomain::Unix => AF_UNIX.to_ne_bytes().to_vec(),
        };
        match &self.address {
            SocketAddress::Unix(path) => {
                if path.len() > 0 {
                    let mut address = sockaddr_un::default();
                    let path_len = std::cmp::min(address.sun_path.len() - 1, path.len());

                    address.sun_family = AF_UNIX;
                    address.sun_path[..path_len].copy_from_slice(&path[..path_len]);
                    address.as_bytes().to_vec()
                } else {
                    default_name
                }
            }
            SocketAddress::Unspecified => default_name,
        }
    }

    /// Reads the the contents of this socket into `UserBufferIterator`.
    ///
    /// Will stop reading if a message with ancillary data is encountered (after the message with
    /// ancillary data has been read).
    ///
    /// # Parameters
    /// - `task`: The task to read memory from.
    /// - `user_buffers`: The `UserBufferIterator` to write the data to.
    ///
    /// Returns the number of bytes that were read into the buffer, and any ancillary data that was
    /// read from the socket.
    pub fn read(
        &mut self,
        task: &Task,
        user_buffers: &mut UserBufferIterator<'_>,
    ) -> Result<(usize, Option<AncillaryData>), Errno> {
        let (bytes_read, ancillary_data) = self.messages.read(task, user_buffers)?;
        self.did_read(bytes_read);
        Ok((bytes_read, ancillary_data))
    }

    /// Notifies the `FsNode` associated with this endpoint that a read completed.
    ///
    /// # Parameters
    /// - `bytes_read`: The number of bytes that were read. This is used to determine whether or not
    ///                 a notification should be sent.
    fn did_read(&mut self, bytes_read: usize) {
        if bytes_read > 0 {
            self.node.upgrade().map(|node| {
                node.notify(FdEvents::POLLOUT);
            });
        }
    }

    /// Writes the the contents of `UserBufferIterator` into this socket.
    ///
    /// # Parameters
    /// - `task`: The task to read memory from.
    /// - `user_buffers`: The `UserBufferIterator` to read the data from.
    /// - `ancillary_data`: Any ancillary data to write to the socket. Note that the ancillary data
    ///                     will only be written if the entirety of the requested write completes.
    ///
    /// Returns the number of bytes that were written to the socket.
    pub fn write(
        &mut self,
        task: &Task,
        user_buffers: &mut UserBufferIterator<'_>,
        ancillary_data: &mut Option<AncillaryData>,
    ) -> Result<usize, Errno> {
        let bytes_written = self.messages.write(task, user_buffers, ancillary_data)?;
        self.did_write(bytes_written);
        Ok(bytes_written)
    }

    /// Notifies the `FsNode` associated with this endpoint that a write completed.
    ///
    /// # Parameters
    /// - `bytes_written`: The number of bytes that were written. This is used to determine whether
    ///                    or not a notification should be sent.
    fn did_write(&mut self, bytes_written: usize) {
        if bytes_written > 0 {
            self.node.upgrade().map(|node| {
                node.notify(FdEvents::POLLIN);
            });
        }
    }
}

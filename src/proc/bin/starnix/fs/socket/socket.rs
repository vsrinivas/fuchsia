// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::VecDeque;

use super::*;

use crate::errno;
use crate::error;
use crate::fs::buffers::*;
use crate::fs::*;
use crate::mode;
use crate::task::*;
use crate::types::*;

use parking_lot::Mutex;
use std::sync::Arc;

/// A `Socket` represents one endpoint of a bidirectional communication channel.
///
/// The `Socket` enum represents the state that the socket is in.
///
/// A `Socket` always contains a local `SocketState`. Most of the socket's state is stored in
/// its `SocketState`.
///
/// When a socket is connected, the remote `SocketState` can be retrieved from its
/// `SocketConnection`. This can be used to, for example, write to the remote socket.
pub enum Socket {
    /// A socket that is neither listening for connections nor connected to another socket.
    Disconnected(SocketStateHandle),

    /// A `Listening` socket is a passive socket that is waiting for incoming connections.
    Listening(ListeningSocket),

    /// A `Connected` socket is connected to a remote socket.
    ///
    /// The connected sockets are peers. Reads are send to the local socket and writes
    /// are sent to the remote socket.
    Connected(SocketConnection),

    /// A `Shutdown` socket has been connected at some point, but the connection has been
    /// shut down.
    Shutdown(SocketStateHandle),
}

/// A `SocketHandle` is a `Socket` wrapped in a `Arc<Mutex<..>>`.
pub type SocketHandle = Arc<Mutex<Socket>>;

/// Creates a SocketHandle from a Socket.
///
/// Used when creating new sockets.
fn new_handle(socket: Socket) -> SocketHandle {
    Arc::new(Mutex::new(socket))
}

impl Socket {
    /// Creates a new unbound socket.
    ///
    /// # Parameters
    /// - `domain`: The domain of the socket (e.g., `AF_UNIX`).
    pub fn new(domain: SocketDomain, socket_type: SocketType) -> SocketHandle {
        new_handle(Socket::Disconnected(SocketState::new(domain, socket_type, None)))
    }

    /// Creates a pair of connected sockets.
    ///
    /// # Parameters
    /// - `domain`: The domain of the socket (e.g., `AF_UNIX`).
    /// - `socket_type`: The type of the socket (e.g., `SOCK_STREAM`).
    pub fn new_pair(domain: SocketDomain, socket_type: SocketType) -> (SocketHandle, SocketHandle) {
        let left_state = SocketState::new(domain, socket_type, None);
        let right_state = SocketState::new(domain, socket_type, None);
        let (left, right) = SocketConnection::connect(left_state, right_state);
        (new_handle(left), new_handle(right))
    }

    /// Creates a `FileHandle` where the associated `FsNode` contains a socket.
    ///
    /// # Parameters
    /// - `kernel`: The kernel that is used to fetch `SocketFs`, to store the created socket node.
    /// - `socket`: The socket to store in the `FsNode`.
    /// - `open_flags`: The `OpenFlags` which are used to create the `FileObject`.
    pub fn new_file(kernel: &Kernel, socket: SocketHandle, open_flags: OpenFlags) -> FileHandle {
        let fs = socket_fs(kernel);
        let mode = mode!(IFSOCK, 0o777);
        let node = fs.create_node(Box::new(Anon), mode);
        node.set_socket(socket.clone());
        FileObject::new_anonymous(SocketFile::new(socket), node, open_flags)
    }

    #[allow(dead_code)]
    pub fn domain(&self) -> SocketDomain {
        self.local_state().lock().domain
    }

    pub fn socket_type(&self) -> SocketType {
        self.local_state().lock().socket_type
    }

    /// Binds this socket to a `socket_address`.
    ///
    /// Returns an error if the socket could not be bound.
    pub fn bind(&mut self, socket_address: SocketAddress) -> Result<(), Errno> {
        let mut state = self.local_state().lock();
        if state.address.is_some() {
            return error!(EINVAL);
        }
        state.address = Some(socket_address);
        Ok(())
    }

    /// Listen for incoming connections on this socket.
    ///
    /// Returns an error if the socket is not bound.
    pub fn listen(&mut self, backlog: u32) -> Result<(), Errno> {
        match self {
            Socket::Disconnected(state) => {
                if state.lock().address.is_none() {
                    return error!(EINVAL);
                }
                *self = ListeningSocket::new(state.clone(), backlog);
                Ok(())
            }
            Socket::Listening(passive) => {
                passive.set_backlog(backlog)?;
                Ok(())
            }
            _ => error!(EINVAL),
        }
    }

    /// Accept an incoming connection on this socket.
    ///
    /// The socket holds a queue of incoming connections. This function reads
    /// the first entry from the queue (if any) and returns the server end of
    /// the connection.
    ///
    /// Returns an error if the socket is not listening or if the queue is
    /// empty.
    pub fn accept(&mut self) -> Result<SocketHandle, Errno> {
        let state = match self {
            Socket::Listening(state) => Ok(state),
            _ => error!(EINVAL),
        }?;
        state.accept()
    }

    /// Connects this socket to the provided socket.
    ///
    /// The given `socket` must be in a listening state.
    ///
    /// If there is enough room the listening socket's queue of incoming
    /// connections, this socket becomes connected to a new server state,
    /// which is placed in the queue for the listening socket to accept.
    ///
    /// Returns an error if the connection cannot be established (e.g., if one
    /// of the sockets is already connected).
    pub fn connect(&mut self, socket: &mut Socket) -> Result<(), Errno> {
        let client = match self {
            Socket::Disconnected(state) => Ok(state),
            Socket::Connected(_) => error!(EISCONN),
            _ => error!(ECONNREFUSED),
        }?;
        let passive = match socket {
            Socket::Listening(passive) => Ok(passive),
            _ => error!(ECONNREFUSED),
        }?;
        *self = passive.connect(client)?;
        Ok(())
    }

    /// Returns the socket state that is connected to this socket, if such
    /// an state exists.
    fn remote_state(&self) -> Result<&SocketStateHandle, Errno> {
        match self {
            Socket::Connected(connection) => Ok(&connection.remote),
            _ => error!(ENOTCONN),
        }
    }

    /// Returns the state associated with this socket.
    ///
    /// If the socket is connected, this method will return the state that
    /// this socket reads from (e.g., `connection.local` if the socket is
    /// `Connected`).
    fn local_state(&self) -> &SocketStateHandle {
        match self {
            Socket::Disconnected(state) => state,
            Socket::Listening(listener) => &listener.state,
            Socket::Connected(connection) => &connection.local,
            Socket::Shutdown(state) => state,
        }
    }

    /// Returns the name of this socket.
    ///
    /// The name is derived from the address and domain. A socket
    /// will always have a name, even if it is not bound to an address.
    pub fn getsockname(&self) -> Vec<u8> {
        self.local_state().lock().name()
    }

    /// Returns the name of the peer of this socket, if such a peer exists.
    ///
    /// Returns an error if the socket is not connected.
    pub fn getpeername(&self) -> Result<Vec<u8>, Errno> {
        Ok(self.remote_state()?.lock().name())
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
    ) -> Result<(usize, Option<SocketAddress>, Option<AncillaryData>), Errno> {
        let state = self.local_state();
        state.lock().read(task, user_buffers)
    }

    /// Shuts down this socket, preventing any future reads and/or writes.
    ///
    /// TODO: This should take a "how" parameter to indicate which operations should be prevented.
    ///
    /// Returns the file system node that this socket was connected to (this is useful to be able
    /// to notify the peer about the shutdown).
    pub fn shutdown(&mut self) -> Result<(), Errno> {
        let connection = match &self {
            Socket::Connected(connection) => Ok(connection),
            _ => error!(ENOTCONN),
        }?;

        connection.local.lock().shutdown();
        connection.remote.lock().shutdown();
        *self = Socket::Shutdown(connection.local.clone());
        Ok(())
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
        let mut state = self.remote_state()?.lock();
        state.write(task, user_buffers, self.local_state().lock().address.clone(), ancillary_data)
    }

    pub fn wait_async(&self, waiter: &Arc<Waiter>, events: FdEvents, handler: EventHandler) {
        let mut state = self.local_state().lock();
        state.waiters.wait_async_mask(waiter, events.mask(), handler)
    }
}

#[derive(Debug, Copy, Clone, PartialEq, Eq)]
pub enum SocketDomain {
    /// The `Unix` socket domain contains sockets that were created with the `AF_UNIX` domain. These
    /// sockets communicate locally, with other sockets on the same host machine.
    Unix,
}

impl SocketDomain {
    pub fn from_raw(raw: u16) -> Option<SocketDomain> {
        match raw {
            AF_UNIX => Some(SocketDomain::Unix),
            _ => None,
        }
    }
}

#[derive(Debug, Copy, Clone, PartialEq, Eq)]
pub enum SocketType {
    Stream,
    Datagram,
    SeqPacket,
}

impl SocketType {
    pub fn from_raw(raw: u32) -> Option<SocketType> {
        match raw {
            SOCK_STREAM => Some(SocketType::Stream),
            SOCK_DGRAM => Some(SocketType::Datagram),
            SOCK_SEQPACKET => Some(SocketType::SeqPacket),
            _ => None,
        }
    }

    pub fn as_raw(&self) -> u32 {
        match self {
            SocketType::Stream => SOCK_STREAM,
            SocketType::Datagram => SOCK_DGRAM,
            SocketType::SeqPacket => SOCK_SEQPACKET,
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum SocketAddress {
    /// An address in the AF_UNSPEC domain.
    #[allow(dead_code)]
    Unspecified,

    /// A `Unix` socket address contains the filesystem path that was used to bind the socket.
    Unix(FsString),
}

pub const SA_FAMILY_SIZE: usize = std::mem::size_of::<uapi::__kernel_sa_family_t>();

impl SocketAddress {
    pub fn to_bytes(&self) -> Vec<u8> {
        match self {
            SocketAddress::Unspecified => AF_UNSPEC.to_ne_bytes().to_vec(),
            SocketAddress::Unix(name) => {
                if name.len() > 0 {
                    let template = sockaddr_un::default();
                    let path_length = std::cmp::min(template.sun_path.len() - 1, name.len());
                    let mut bytes = vec![0u8; SA_FAMILY_SIZE + path_length + 1];
                    bytes[..SA_FAMILY_SIZE].copy_from_slice(&AF_UNIX.to_ne_bytes());
                    bytes[SA_FAMILY_SIZE..(SA_FAMILY_SIZE + path_length)]
                        .copy_from_slice(&name[..path_length]);
                    bytes
                } else {
                    AF_UNIX.to_ne_bytes().to_vec()
                }
            }
        }
    }

    fn is_abstract_unix(&self) -> bool {
        match self {
            SocketAddress::Unix(name) => name.first() == Some(&b'\0'),
            _ => false,
        }
    }
}

/// A `SocketConnection` contains two connected sockets.
pub struct SocketConnection {
    /// The socket that initiated the connection.
    local: SocketStateHandle,

    /// The socket that was connected to.
    remote: SocketStateHandle,
}

impl SocketConnection {
    /// Create a new `SocketConnection` object with the given socket handles.
    fn new(local: SocketStateHandle, remote: SocketStateHandle) -> Socket {
        Socket::Connected(SocketConnection { local, remote })
    }

    /// Create a pair of connected `SocketConnection` objects with the given socket handles.
    fn connect(
        left_socket: SocketStateHandle,
        right_socket: SocketStateHandle,
    ) -> (Socket, Socket) {
        let left = SocketConnection::new(left_socket.clone(), right_socket.clone());
        let right = SocketConnection::new(right_socket, left_socket);
        (left, right)
    }
}

/// `SocketState` stores the state associated with a socket.
pub struct SocketState {
    /// The `MessageQueue` that contains messages for this socket.
    messages: MessageQueue,

    /// This queue will be notified on reads, writes, disconnects etc.
    waiters: WaitQueue,

    /// The domain of this socket.
    domain: SocketDomain,

    /// The type of this socket.
    socket_type: SocketType,

    /// The address that this socket has been bound to, if it has been bound.
    address: Option<SocketAddress>,

    /// Whether this socket is readable.
    readable: bool,

    /// Whether this socket is writable.
    writable: bool,
}

pub type SocketStateHandle = Arc<Mutex<SocketState>>;

impl SocketState {
    /// Creates a new socket state.
    ///
    /// The socket's address is set to a default value for the specified domain.
    fn new(
        domain: SocketDomain,
        socket_type: SocketType,
        address: Option<SocketAddress>,
    ) -> SocketStateHandle {
        Arc::new(Mutex::new(SocketState {
            messages: MessageQueue::new(usize::MAX),
            waiters: WaitQueue::default(),
            domain,
            socket_type,
            address,
            readable: true,
            writable: true,
        }))
    }

    /// Returns the local name of the socket.
    fn name(&self) -> Vec<u8> {
        if let Some(address) = &self.address {
            address.to_bytes()
        } else {
            match self.domain {
                SocketDomain::Unix => AF_UNIX.to_ne_bytes().to_vec(),
            }
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
    ) -> Result<(usize, Option<SocketAddress>, Option<AncillaryData>), Errno> {
        if !self.readable {
            return Ok((0, None, None));
        }
        let (bytes_read, address, ancillary_data) = self.messages.read(task, user_buffers)?;
        if bytes_read == 0 && ancillary_data.is_none() && user_buffers.remaining() > 0 {
            return error!(EAGAIN);
        }
        if bytes_read > 0 {
            self.waiters.notify_events(FdEvents::POLLOUT);
        }
        Ok((bytes_read, address, ancillary_data))
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
        address: Option<SocketAddress>,
        ancillary_data: &mut Option<AncillaryData>,
    ) -> Result<usize, Errno> {
        if !self.writable {
            return Err(ENOTCONN);
        }
        let bytes_written = self.messages.write(task, user_buffers, address, ancillary_data)?;
        if bytes_written > 0 {
            self.waiters.notify_events(FdEvents::POLLIN);
        }
        Ok(bytes_written)
    }

    fn shutdown(&mut self) {
        self.readable = false;
        self.writable = false;
        self.waiters.notify_events(FdEvents::POLLIN | FdEvents::POLLOUT | FdEvents::POLLHUP);
    }
}

struct AcceptQueue {
    sockets: VecDeque<SocketHandle>,
    backlog: usize,
}

/// A socket that is listening for incoming connections.
pub struct ListeningSocket {
    /// The socket state.
    state: SocketStateHandle,

    /// The queue of incoming connections.
    ///
    /// A `SocketHandle` in this queue is the end of the connection that will be returned to the
    /// server when the connection is accepted.
    queue: Mutex<AcceptQueue>,
}

impl ListeningSocket {
    /// Wrap the given socket state as a listening socket with an incoming queue
    /// of the given size.
    fn new(state: SocketStateHandle, backlog: u32) -> Socket {
        let backlog = backlog as usize;
        Socket::Listening(ListeningSocket {
            state,
            queue: Mutex::new(AcceptQueue { sockets: VecDeque::with_capacity(backlog), backlog }),
        })
    }

    /// Accept one of the incoming connections.
    fn accept(&self) -> Result<SocketHandle, Errno> {
        let mut queue = self.queue.lock();
        queue.sockets.pop_front().ok_or_else(|| errno!(EAGAIN))
    }

    /// Connect to this socket.
    ///
    /// This function creates a new server socket for the connection and
    /// establishes the connection between the client and the server sockets.
    /// The connected client socket is returned. The connected server
    /// socket is placed in the queue for this socket to be accepted by the
    /// server later.
    fn connect(&self, client_state: &SocketStateHandle) -> Result<Socket, Errno> {
        let mut passive = self.state.lock();
        let mut active = client_state.lock();

        if active.domain != passive.domain || active.socket_type != passive.socket_type {
            // According to ConnectWithWrongType in accept_bind_test, abstract
            // UNIX domain sockets return ECONNREFUSED rather than EPROTOTYPE.
            if let Some(address) = &passive.address {
                if address.is_abstract_unix() {
                    return error!(ECONNREFUSED);
                }
            }
            return error!(EPROTOTYPE);
        }

        let mut queue = self.queue.lock();
        if queue.sockets.len() >= queue.backlog {
            return error!(EAGAIN);
        }

        let server_state =
            SocketState::new(active.domain, active.socket_type, passive.address.clone());
        let (client, server) =
            SocketConnection::connect(client_state.clone(), server_state.clone());
        queue.sockets.push_back(new_handle(server));

        passive.waiters.notify_events(FdEvents::POLLIN);
        active.waiters.notify_events(FdEvents::POLLOUT);

        Ok(client)
    }

    fn set_backlog(&self, backlog: u32) -> Result<(), Errno> {
        let backlog = backlog as usize;
        let mut queue = self.queue.lock();
        if queue.sockets.len() > backlog {
            return error!(EINVAL);
        }
        queue.backlog = backlog;
        Ok(())
    }
}

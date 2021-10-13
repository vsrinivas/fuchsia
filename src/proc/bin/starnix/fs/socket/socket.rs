// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::VecDeque;

use super::*;

use crate::fs::buffers::*;
use crate::fs::*;
use crate::mode;
use crate::task::*;
use crate::types::*;
use crate::{errno, error};

use parking_lot::Mutex;
use std::sync::Arc;

/// A `Socket` represents one endpoint of a bidirectional communication channel.
pub struct Socket(Mutex<SocketInner>);

pub struct SocketInner {
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

    /// Socket state: a queue if this is a listening socket, or a peer if this is a connected
    /// socket.
    state: SocketState,
}

enum SocketState {
    /// The socket has not been connected.
    Disconnected,

    /// The socket has had `listen` called and can accept incoming connections.
    Listening(AcceptQueue),

    /// The socket is connected to a peer.
    Connected(SocketHandle),

    /// The socket has been disconnected, and can't be reconnected.
    Shutdown,
}

pub type SocketHandle = Arc<Socket>;

impl Socket {
    /// Creates a new unbound socket.
    ///
    /// # Parameters
    /// - `domain`: The domain of the socket (e.g., `AF_UNIX`).
    pub fn new(domain: SocketDomain, socket_type: SocketType) -> SocketHandle {
        Arc::new(Socket(Mutex::new(SocketInner {
            messages: MessageQueue::new(usize::MAX),
            waiters: WaitQueue::default(),
            domain,
            socket_type,
            address: None,
            readable: true,
            writable: true,
            state: SocketState::Disconnected,
        })))
    }

    /// Creates a pair of connected sockets.
    ///
    /// # Parameters
    /// - `domain`: The domain of the socket (e.g., `AF_UNIX`).
    /// - `socket_type`: The type of the socket (e.g., `SOCK_STREAM`).
    pub fn new_pair(
        kernel: &Kernel,
        domain: SocketDomain,
        socket_type: SocketType,
        open_flags: OpenFlags,
    ) -> (FileHandle, FileHandle) {
        let left = Socket::new(domain, socket_type);
        let right = Socket::new(domain, socket_type);
        left.lock().state = SocketState::Connected(right.clone());
        right.lock().state = SocketState::Connected(left.clone());
        let left = Socket::new_file(kernel, left, open_flags);
        let right = Socket::new_file(kernel, right, open_flags);
        (left, right)
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

    /// Locks and returns the inner state of the Socket.
    // TODO(tbodt): Make this private. A good time to do this is in the refactor that will be
    // necessary to support AF_INET.
    pub fn lock(&self) -> parking_lot::MutexGuard<'_, SocketInner> {
        self.0.lock()
    }

    /// Returns the name of this socket.
    ///
    /// The name is derived from the address and domain. A socket
    /// will always have a name, even if it is not bound to an address.
    pub fn getsockname(&self) -> Vec<u8> {
        self.lock().name()
    }

    /// Returns the name of the peer of this socket, if such a peer exists.
    ///
    /// Returns an error if the socket is not connected.
    pub fn getpeername(&self) -> Result<Vec<u8>, Errno> {
        let peer = self.lock().peer()?.clone();
        let name = peer.lock().name();
        Ok(name)
    }

    /// Initiate a connection from this socket to the given server socket.
    ///
    /// The given `socket` must be in a listening state.
    ///
    /// If there is enough room the listening socket's queue of incoming connections, this socket
    /// is connected to a new socket, which is placed in the queue for the listening socket to
    /// accept.
    ///
    /// Returns an error if the connection cannot be established (e.g., if one of the sockets is
    /// already connected).
    pub fn connect(self: &SocketHandle, listener: &SocketHandle) -> Result<(), Errno> {
        // Only hold one lock at a time until we make sure the lock ordering is right: client
        // before listener
        match listener.lock().state {
            SocketState::Listening(_) => {}
            _ => return error!(ECONNREFUSED),
        }
        let mut client = self.lock();
        match client.state {
            SocketState::Disconnected => {}
            SocketState::Connected(_) => return error!(EISCONN),
            _ => return error!(EINVAL),
        };
        let mut listener = listener.lock();
        // Must check this again because we released the listener lock for a moment
        let queue = match &listener.state {
            SocketState::Listening(queue) => queue,
            _ => return error!(ECONNREFUSED),
        };

        if client.domain != listener.domain || client.socket_type != listener.socket_type {
            // According to ConnectWithWrongType in accept_bind_test, abstract
            // UNIX domain sockets return ECONNREFUSED rather than EPROTOTYPE.
            // TODO(tbodt): it's possible this entire file is only applicable to UNIX domain
            // sockets, in which case this can be simplified.
            if let Some(address) = &listener.address {
                if address.is_abstract_unix() {
                    return error!(ECONNREFUSED);
                }
            }
            return error!(EPROTOTYPE);
        }

        if queue.sockets.len() >= queue.backlog {
            return error!(EAGAIN);
        }

        let server = Socket::new(listener.domain, listener.socket_type);
        client.state = SocketState::Connected(server.clone());
        {
            let mut server = server.lock();
            server.state = SocketState::Connected(self.clone());
            server.address = listener.address.clone();
        }

        // We already checked that the socket is in Listening state...but the borrow checker cannot
        // be convinced that it's ok to combine these checks
        let queue = match listener.state {
            SocketState::Listening(ref mut queue) => queue,
            _ => panic!("something changed the server socket state while I held a lock on it"),
        };
        queue.sockets.push_back(server);
        listener.waiters.notify_events(FdEvents::POLLIN);
        Ok(())
    }

    /// Accept an incoming connection on this socket.
    ///
    /// The socket holds a queue of incoming connections. This function reads the first entry from
    /// the queue (if any) and creates and returns the server end of the connection.
    ///
    /// Returns an error if the socket is not listening or if the queue is empty.
    pub fn accept(&self) -> Result<SocketHandle, Errno> {
        let mut inner = self.lock();
        let queue = match &mut inner.state {
            SocketState::Listening(queue) => queue,
            _ => return error!(EINVAL),
        };
        queue.sockets.pop_front().ok_or(errno!(EAGAIN))
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
        self.lock().read(task, user_buffers)
    }

    /// Reads all the available messages out of this socket.
    ///
    /// If no data is available, or this socket is not readable, then an empty vector is returned.
    #[cfg(test)]
    pub fn read_kernel(&self) -> Vec<Message> {
        self.lock().read_kernel()
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
        let (peer, local_address) = {
            let inner = self.lock();
            (inner.peer()?.clone(), inner.address.clone())
        };
        let mut peer = peer.lock();
        peer.write(task, user_buffers, local_address, ancillary_data)
    }

    /// Writes the provided message into this socket. If the write succeeds, all the bytes were
    /// written.
    ///
    /// # Parameters
    /// - `message`: The message to write.
    ///
    /// Returns an error if the socket is not connected.
    #[cfg(test)]
    pub fn write_kernel(&self, message: Message) -> Result<(), Errno> {
        let peer = {
            let inner = self.lock();
            inner.peer()?.clone()
        };
        let mut peer = peer.lock();
        peer.write_kernel(message)
    }

    pub fn wait_async(&self, waiter: &Arc<Waiter>, events: FdEvents, handler: EventHandler) {
        let mut inner = self.lock();
        inner.waiters.wait_async_mask(waiter, events.mask(), handler)
    }

    /// Shuts down this socket, preventing any future reads and/or writes.
    ///
    /// TODO: This should take a "how" parameter to indicate which operations should be prevented.
    pub fn shutdown(&self) -> Result<(), Errno> {
        let peer = self.lock().peer()?.clone();
        self.lock().shutdown_one_end();
        peer.lock().shutdown_one_end();
        Ok(())
    }
}

impl SocketInner {
    /// Returns the local name of the socket.
    pub fn name(&self) -> Vec<u8> {
        self.address
            .clone()
            .unwrap_or_else(|| SocketAddress::default_for_domain(self.domain))
            .to_bytes()
    }

    pub fn socket_type(&self) -> SocketType {
        self.socket_type
    }

    /// Binds this socket to a `socket_address`.
    ///
    /// Returns an error if the socket could not be bound.
    pub fn bind(&mut self, socket_address: SocketAddress) -> Result<(), Errno> {
        if self.address.is_some() {
            return error!(EINVAL);
        }
        self.address = Some(socket_address);
        Ok(())
    }

    /// Listen for incoming connections on this socket.
    ///
    /// Returns an error if the socket is not bound.
    pub fn listen(&mut self, backlog: i32) -> Result<(), Errno> {
        let backlog = if backlog < 0 { 1024 } else { backlog as usize };
        match &mut self.state {
            SocketState::Disconnected if self.address.is_some() => {
                self.state = SocketState::Listening(AcceptQueue::new(backlog));
                Ok(())
            }
            SocketState::Listening(queue) => {
                queue.set_backlog(backlog)?;
                Ok(())
            }
            _ => error!(EINVAL),
        }
    }

    /// Returns the socket that is connected to this socket, if such a peer exists. Returns
    /// ENOTCONN otherwise.
    pub fn peer(&self) -> Result<&SocketHandle, Errno> {
        match &self.state {
            SocketState::Connected(peer) => Ok(peer),
            _ => error!(ENOTCONN),
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
    fn read(
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

    /// Reads all the available messages out of this socket.
    ///
    /// If no data is available, or this socket is not readable, then an empty vector is returned.
    #[cfg(test)]
    pub fn read_kernel(&mut self) -> Vec<Message> {
        if !self.readable {
            return vec![];
        }

        let (messages, bytes_read) = self.messages.read_bytes(&mut None, usize::MAX);

        if bytes_read > 0 {
            self.waiters.notify_events(FdEvents::POLLOUT);
        }

        messages
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
    fn write(
        &mut self,
        task: &Task,
        user_buffers: &mut UserBufferIterator<'_>,
        address: Option<SocketAddress>,
        ancillary_data: &mut Option<AncillaryData>,
    ) -> Result<usize, Errno> {
        if !self.writable {
            return error!(ENOTCONN);
        }
        let bytes_written = self.messages.write(task, user_buffers, address, ancillary_data)?;
        if bytes_written > 0 {
            self.waiters.notify_events(FdEvents::POLLIN);
        }
        Ok(bytes_written)
    }

    /// Writes the provided message into this socket. If the write succeeds, all the bytes were
    /// written.
    ///
    /// # Parameters
    /// - `message`: The message to write.
    ///
    /// Returns an error if the socket is not connected.
    #[cfg(test)]
    fn write_kernel(&mut self, message: Message) -> Result<(), Errno> {
        if !self.writable {
            return error!(ENOTCONN);
        }

        let bytes_written = message.data.len();
        self.messages.write_message(message);

        if bytes_written > 0 {
            self.waiters.notify_events(FdEvents::POLLIN);
        }

        Ok(())
    }

    fn shutdown_one_end(&mut self) {
        self.readable = false;
        self.writable = false;
        self.state = SocketState::Shutdown;
        self.waiters.notify_events(FdEvents::POLLIN | FdEvents::POLLOUT | FdEvents::POLLHUP);
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
    pub fn default_for_domain(domain: SocketDomain) -> SocketAddress {
        match domain {
            SocketDomain::Unix => SocketAddress::Unix(FsString::new()),
        }
    }

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

struct AcceptQueue {
    sockets: VecDeque<SocketHandle>,
    backlog: usize,
}

impl AcceptQueue {
    fn new(backlog: usize) -> AcceptQueue {
        AcceptQueue { sockets: VecDeque::with_capacity(backlog), backlog }
    }

    fn set_backlog(&mut self, backlog: usize) -> Result<(), Errno> {
        if self.sockets.len() > backlog {
            return error!(EINVAL);
        }
        self.backlog = backlog;
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_read_write_kernel() {
        let socket = Socket::new(SocketDomain::Unix, SocketType::Stream);
        socket.lock().bind(SocketAddress::Unix(b"\0".to_vec())).expect("Failed to bind socket.");
        socket.lock().listen(10).expect("Failed to listen.");
        let connecting_socket = Socket::new(SocketDomain::Unix, SocketType::Stream);
        connecting_socket.connect(&socket).expect("Failed to connect socket.");
        let server_socket = socket.accept().unwrap();

        let message = Message::new(vec![1, 2, 3].into(), None, None);
        server_socket.write_kernel(message.clone()).expect("Failed to write.");

        assert_eq!(connecting_socket.read_kernel(), vec![message]);
    }
}

// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon as zx;
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

// From unix.go in gVisor.
const SOCKET_MIN_SIZE: usize = 4 << 10;
const SOCKET_DEFAULT_SIZE: usize = 208 << 10;
const SOCKET_MAX_SIZE: usize = 4 << 20;

trait SocketOps: Send + Sync {
    fn connect(
        &self,
        socket: &SocketHandle,
        peer: &SocketHandle,
        credentials: ucred,
    ) -> Result<(), Errno>;

    fn listen(&self, socket: &Socket, backlog: i32) -> Result<(), Errno>;

    fn accept(&self, socket: &Socket, credentials: ucred) -> Result<SocketHandle, Errno>;
}

fn create_socket_ops(_domain: SocketDomain, socket_type: SocketType) -> Box<dyn SocketOps> {
    match socket_type {
        SocketType::Stream | SocketType::SeqPacket => ConnectionedSocket::new(),
        SocketType::Datagram | SocketType::Raw => ConnectionlessSocket::new(),
    }
}

enum SocketState {
    /// The socket has not been connected.
    Disconnected,

    /// The socket has had `listen` called and can accept incoming connections.
    Listening(AcceptQueue),

    /// The socket is connected to a peer.
    Connected(SocketHandle),

    /// The socket is closed.
    Closed,
}

pub struct SocketInner {
    /// The `MessageQueue` that contains messages for this socket.
    messages: MessageQueue,

    /// This queue will be notified on reads, writes, disconnects etc.
    waiters: WaitQueue,

    /// The address that this socket has been bound to, if it has been bound.
    address: Option<SocketAddress>,

    /// See SO_RCVTIMEO.
    receive_timeout: Option<zx::Duration>,

    /// See SO_SNDTIMEO.
    send_timeout: Option<zx::Duration>,

    /// See SO_LINGER.
    pub linger: uapi::linger,

    /// Unix credentials of the owner of this socket, for SO_PEERCRED.
    credentials: Option<ucred>,

    /// Socket state: a queue if this is a listening socket, or a peer if this is a connected
    /// socket.
    state: SocketState,
}

/// A `Socket` represents one endpoint of a bidirectional communication channel.
pub struct Socket {
    ops: Box<dyn SocketOps>,

    /// The domain of this socket.
    pub domain: SocketDomain,

    /// The type of this socket.
    pub socket_type: SocketType,

    inner: Mutex<SocketInner>,
}

pub type SocketHandle = Arc<Socket>;

impl Socket {
    /// Creates a new unbound socket.
    ///
    /// # Parameters
    /// - `domain`: The domain of the socket (e.g., `AF_UNIX`).
    pub fn new(domain: SocketDomain, socket_type: SocketType) -> SocketHandle {
        Arc::new(Socket {
            ops: create_socket_ops(domain, socket_type),
            domain,
            socket_type,
            inner: Mutex::new(SocketInner {
                messages: MessageQueue::new(SOCKET_DEFAULT_SIZE),
                waiters: WaitQueue::default(),
                address: None,
                receive_timeout: None,
                send_timeout: None,
                linger: uapi::linger::default(),
                credentials: None,
                state: SocketState::Disconnected,
            }),
        })
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
        credentials: ucred,
        open_flags: OpenFlags,
    ) -> (FileHandle, FileHandle) {
        let left = Socket::new(domain, socket_type);
        let right = Socket::new(domain, socket_type);
        left.lock().state = SocketState::Connected(right.clone());
        left.lock().credentials = Some(credentials.clone());
        right.lock().state = SocketState::Connected(left.clone());
        right.lock().credentials = Some(credentials);
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

    /// Returns the name of this socket.
    ///
    /// The name is derived from the address and domain. A socket
    /// will always have a name, even if it is not bound to an address.
    pub fn getsockname(&self) -> Vec<u8> {
        let inner = self.lock();
        if let Some(address) = &inner.address {
            address.to_bytes()
        } else {
            SocketAddress::default_for_domain(self.domain).to_bytes()
        }
    }

    /// Returns the name of the peer of this socket, if such a peer exists.
    ///
    /// Returns an error if the socket is not connected.
    pub fn getpeername(&self) -> Result<Vec<u8>, Errno> {
        let peer = self.lock().peer().ok_or_else(|| errno!(ENOTCONN))?.clone();
        Ok(peer.getsockname())
    }

    pub fn peer_cred(&self) -> Option<ucred> {
        let peer = self.lock().peer()?.clone();
        let peer = peer.lock();
        peer.credentials.clone()
    }

    pub fn get_receive_timeout(&self) -> Option<zx::Duration> {
        self.lock().receive_timeout
    }

    pub fn set_receive_timeout(&self, value: Option<zx::Duration>) {
        self.lock().receive_timeout = value;
    }

    pub fn get_send_timeout(&self) -> Option<zx::Duration> {
        self.lock().send_timeout
    }

    pub fn set_send_timeout(&self, value: Option<zx::Duration>) {
        self.lock().send_timeout = value;
    }

    pub fn get_receive_capacity(&self) -> usize {
        self.lock().messages.capacity()
    }

    pub fn set_receive_capacity(&self, requested_capacity: usize) {
        self.lock().set_capacity(requested_capacity);
    }

    pub fn get_send_capacity(&self) -> usize {
        if let Some(peer) = self.lock().peer() {
            peer.lock().messages.capacity()
        } else {
            0
        }
    }

    pub fn set_send_capacity(&self, requested_capacity: usize) {
        if let Some(peer) = self.lock().peer() {
            peer.lock().set_capacity(requested_capacity);
        }
    }

    /// Locks and returns the inner state of the Socket.
    // TODO(tbodt): Make this private. A good time to do this is in the refactor that will be
    // necessary to support AF_INET.
    pub fn lock(&self) -> parking_lot::MutexGuard<'_, SocketInner> {
        self.inner.lock()
    }

    /// Binds this socket to a `socket_address`.
    ///
    /// Returns an error if the socket could not be bound.
    pub fn bind(&self, socket_address: SocketAddress) -> Result<(), Errno> {
        self.lock().bind(socket_address)
    }

    pub fn connect(
        self: &SocketHandle,
        peer: &SocketHandle,
        credentials: ucred,
    ) -> Result<(), Errno> {
        self.ops.connect(self, peer, credentials)
    }

    pub fn listen(&self, backlog: i32) -> Result<(), Errno> {
        self.ops.listen(self, backlog)
    }

    pub fn accept(&self, credentials: ucred) -> Result<SocketHandle, Errno> {
        self.ops.accept(self, credentials)
    }

    pub fn is_listening(&self) -> bool {
        match self.lock().state {
            SocketState::Listening(_) => true,
            _ => false,
        }
    }

    fn check_type_for_connect(
        &self,
        peer: &Socket,
        peer_address: &Option<SocketAddress>,
    ) -> Result<(), Errno> {
        if self.domain != peer.domain || self.socket_type != peer.socket_type {
            // According to ConnectWithWrongType in accept_bind_test, abstract
            // UNIX domain sockets return ECONNREFUSED rather than EPROTOTYPE.
            // TODO(tbodt): it's possible this entire file is only applicable to UNIX domain
            // sockets, in which case this can be simplified.
            if let Some(address) = peer_address {
                if address.is_abstract_unix() {
                    return error!(ECONNREFUSED);
                }
            }
            return error!(EPROTOTYPE);
        }
        Ok(())
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
        flags: SocketMessageFlags,
    ) -> Result<MessageReadInfo, Errno> {
        self.lock().read(task, user_buffers, self.socket_type, flags)
    }

    /// Reads all the available messages out of this socket.
    ///
    /// If no data is available, or this socket is not readable, then an empty vector is returned.
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
        dest_address: &mut Option<SocketAddress>,
        ancillary_data: &mut Option<AncillaryData>,
    ) -> Result<usize, Errno> {
        let (peer, local_address) = {
            let inner = self.lock();
            (inner.peer().ok_or_else(|| errno!(EPIPE))?.clone(), inner.address.clone())
        };

        if dest_address.is_some() {
            return error!(EISCONN);
        }
        let mut peer = peer.lock();
        peer.write(task, user_buffers, local_address, ancillary_data, self.socket_type)
    }

    /// Writes the provided message into this socket. If the write succeeds, all the bytes were
    /// written.
    ///
    /// # Parameters
    /// - `message`: The message to write.
    ///
    /// Returns an error if the socket is not connected.
    pub fn write_kernel(&self, message: Message) -> Result<(), Errno> {
        let peer = {
            let inner = self.lock();
            inner.peer().ok_or_else(|| errno!(EPIPE))?.clone()
        };
        let mut peer = peer.lock();
        peer.write_kernel(message)
    }

    pub fn wait_async(&self, waiter: &Arc<Waiter>, events: FdEvents, handler: EventHandler) {
        let mut inner = self.lock();

        let present_events = inner.query_events();
        if events & present_events {
            waiter.wake_immediately(present_events.mask(), handler);
        } else {
            inner.waiters.wait_async_mask(waiter, events.mask(), handler);
        }
    }

    pub fn query_events(&self) -> FdEvents {
        // Note that self.lock() must be dropped before acquiring peer.inner.lock() to avoid
        // potential deadlocks.
        let (mut present_events, peer) = {
            let inner = self.lock();
            (inner.query_events(), inner.peer().map(|p| p.clone()))
        };

        if let Some(peer) = peer {
            let peer_inner = peer.inner.lock();
            let peer_events = peer_inner.messages.query_events();
            if peer_events & FdEvents::POLLOUT {
                present_events |= FdEvents::POLLOUT;
            }
        }

        present_events
    }

    /// Shuts down this socket according to how, preventing any future reads and/or writes.
    ///
    /// Used by the shutdown syscalls.
    pub fn shutdown(&self, how: SocketShutdownFlags) -> Result<(), Errno> {
        let peer = {
            let mut inner = self.lock();
            let peer = inner.peer().ok_or_else(|| errno!(ENOTCONN))?.clone();
            if how.contains(SocketShutdownFlags::READ) {
                inner.shutdown_read();
            }
            peer
        };
        if how.contains(SocketShutdownFlags::WRITE) {
            let mut peer_inner = peer.lock();
            peer_inner.shutdown_write();
        }
        Ok(())
    }

    /// Close this socket.
    ///
    /// Called by SocketFile when the file descriptor that is holding this
    /// socket is closed.
    ///
    /// Close differs from shutdown in two ways. First, close will call
    /// mark_peer_closed_with_unread_data if this socket has unread data,
    /// which changes how read() behaves on that socket. Second, close
    /// transitions the internal state of this socket to Closed, which breaks
    /// the reference cycle that exists in the connected state.
    pub fn close(&self) {
        let (maybe_peer, has_unread) = {
            let mut inner = self.lock();
            let maybe_peer = inner.peer().map(Arc::clone);
            inner.shutdown_read();
            (maybe_peer, !inner.messages.is_empty())
        };
        // If this is a connected socket type, also shut down the connected peer.
        if self.socket_type == SocketType::Stream || self.socket_type == SocketType::SeqPacket {
            if let Some(peer) = maybe_peer {
                let mut peer_inner = peer.lock();
                if has_unread {
                    peer_inner.messages.mark_peer_closed_with_unread_data();
                }
                peer_inner.shutdown_write();
            }
        }
        self.lock().state = SocketState::Closed;
    }
}

impl SocketInner {
    pub fn bind(&mut self, socket_address: SocketAddress) -> Result<(), Errno> {
        if self.address.is_some() {
            return error!(EINVAL);
        }
        self.address = Some(socket_address);
        Ok(())
    }

    fn set_capacity(&mut self, requested_capacity: usize) {
        let capacity = requested_capacity.clamp(SOCKET_MIN_SIZE, SOCKET_MAX_SIZE);
        let capacity = std::cmp::max(capacity, self.messages.len());
        // We have validated capacity sufficiently that set_capacity should always succeed.
        self.messages.set_capacity(capacity).unwrap();
    }

    /// Returns the socket that is connected to this socket, if such a peer exists. Returns
    /// ENOTCONN otherwise.
    fn peer(&self) -> Option<&SocketHandle> {
        match &self.state {
            SocketState::Connected(peer) => Some(peer),
            _ => None,
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
        socket_type: SocketType,
        flags: SocketMessageFlags,
    ) -> Result<MessageReadInfo, Errno> {
        let info = if socket_type == SocketType::Stream {
            if flags.contains(SocketMessageFlags::PEEK) {
                self.messages.peek_stream(task, user_buffers)?
            } else {
                self.messages.read_stream(task, user_buffers)?
            }
        } else {
            if flags.contains(SocketMessageFlags::PEEK) {
                self.messages.peek_datagram(task, user_buffers)?
            } else {
                self.messages.read_datagram(task, user_buffers)?
            }
        };
        if info.bytes_read == 0 && user_buffers.remaining() > 0 && !self.messages.is_closed() {
            return error!(EAGAIN);
        }
        if info.bytes_read > 0 {
            self.waiters.notify_events(FdEvents::POLLOUT);
        }

        Ok(info)
    }

    /// Reads all the available messages out of this socket.
    ///
    /// If no data is available, or this socket is not readable, then an empty vector is returned.
    pub fn read_kernel(&mut self) -> Vec<Message> {
        let bytes_read = self.messages.len();
        let messages = self.messages.take_messages();

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
        socket_type: SocketType,
    ) -> Result<usize, Errno> {
        let bytes_written = if socket_type == SocketType::Stream {
            self.messages.write_stream(task, user_buffers, address, ancillary_data)?
        } else {
            self.messages.write_datagram(task, user_buffers, address, ancillary_data)?
        };
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
    fn write_kernel(&mut self, message: Message) -> Result<(), Errno> {
        let bytes_written = message.data.len();
        self.messages.write_message(message);

        if bytes_written > 0 {
            self.waiters.notify_events(FdEvents::POLLIN);
        }

        Ok(())
    }

    fn shutdown_read(&mut self) {
        self.messages.close();
        self.waiters.notify_events(FdEvents::POLLIN | FdEvents::POLLOUT | FdEvents::POLLHUP);
    }

    fn shutdown_write(&mut self) {
        self.messages.close();
        self.waiters.notify_events(FdEvents::POLLIN | FdEvents::POLLOUT | FdEvents::POLLHUP);
    }

    fn query_events(&self) -> FdEvents {
        let mut present_events = FdEvents::empty();
        let local_events = self.messages.query_events();
        if local_events & FdEvents::POLLIN {
            present_events = FdEvents::POLLIN;
        }

        match &self.state {
            SocketState::Listening(queue) => {
                if queue.sockets.len() > 0 {
                    present_events |= FdEvents::POLLIN;
                }
            }
            SocketState::Closed => {
                present_events |= FdEvents::POLLHUP;
            }
            _ => {}
        }

        present_events
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

struct ConnectionedSocket;

impl ConnectionedSocket {
    fn new() -> Box<dyn SocketOps> {
        Box::new(ConnectionedSocket)
    }
}

impl SocketOps for ConnectionedSocket {
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
    fn connect(
        &self,
        socket: &SocketHandle,
        peer: &SocketHandle,
        credentials: ucred,
    ) -> Result<(), Errno> {
        // Only hold one lock at a time until we make sure the lock ordering is right: client
        // before listener
        match peer.lock().state {
            SocketState::Listening(_) => {}
            _ => return error!(ECONNREFUSED),
        }
        let mut client = socket.lock();
        match client.state {
            SocketState::Disconnected => {}
            SocketState::Connected(_) => return error!(EISCONN),
            _ => return error!(EINVAL),
        };
        let mut listener = peer.lock();
        // Must check this again because we released the listener lock for a moment
        let queue = match &listener.state {
            SocketState::Listening(queue) => queue,
            _ => return error!(ECONNREFUSED),
        };

        socket.check_type_for_connect(peer, &listener.address)?;

        if queue.sockets.len() >= queue.backlog {
            return error!(EAGAIN);
        }

        let server = Socket::new(peer.domain, peer.socket_type);
        server.lock().messages.set_capacity(listener.messages.capacity())?;

        client.state = SocketState::Connected(server.clone());
        client.credentials = Some(credentials);
        {
            let mut server = server.lock();
            server.state = SocketState::Connected(socket.clone());
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

    /// Listen for incoming connections on this socket.
    ///
    /// Returns an error if the socket is not bound.
    fn listen(&self, socket: &Socket, backlog: i32) -> Result<(), Errno> {
        let mut inner = socket.lock();
        let is_bound = inner.address.is_some();
        let backlog = if backlog < 0 { 1024 } else { backlog as usize };
        match &mut inner.state {
            SocketState::Disconnected if is_bound => {
                inner.state = SocketState::Listening(AcceptQueue::new(backlog));
                Ok(())
            }
            SocketState::Listening(queue) => {
                queue.set_backlog(backlog)?;
                Ok(())
            }
            _ => error!(EINVAL),
        }
    }

    /// Accept an incoming connection on this socket, with the provided credentials.
    ///
    /// The socket holds a queue of incoming connections. This function reads the first entry from
    /// the queue (if any) and creates and returns the server end of the connection.
    ///
    /// Returns an error if the socket is not listening or if the queue is empty.
    fn accept(&self, socket: &Socket, credentials: ucred) -> Result<SocketHandle, Errno> {
        let mut inner = socket.lock();
        let queue = match &mut inner.state {
            SocketState::Listening(queue) => queue,
            _ => return error!(EINVAL),
        };
        let socket = queue.sockets.pop_front().ok_or(errno!(EAGAIN))?;
        socket.lock().credentials = Some(credentials);
        Ok(socket)
    }
}

struct ConnectionlessSocket;

impl ConnectionlessSocket {
    fn new() -> Box<dyn SocketOps> {
        Box::new(ConnectionlessSocket)
    }
}

impl SocketOps for ConnectionlessSocket {
    fn connect(
        &self,
        socket: &SocketHandle,
        peer: &SocketHandle,
        _credentials: ucred,
    ) -> Result<(), Errno> {
        {
            let peer_inner = peer.lock();
            socket.check_type_for_connect(peer, &peer_inner.address)?;
        }
        socket.lock().state = SocketState::Connected(peer.clone());
        Ok(())
    }

    fn listen(&self, _socket: &Socket, _backlog: i32) -> Result<(), Errno> {
        error!(EOPNOTSUPP)
    }

    fn accept(&self, _socket: &Socket, _credentials: ucred) -> Result<SocketHandle, Errno> {
        error!(EOPNOTSUPP)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::testing::*;

    #[test]
    fn test_read_write_kernel() {
        let (_kernel, current_task) = create_kernel_and_task();
        let socket = Socket::new(SocketDomain::Unix, SocketType::Stream);
        socket.bind(SocketAddress::Unix(b"\0".to_vec())).expect("Failed to bind socket.");
        socket.listen(10).expect("Failed to listen.");
        assert_eq!(FdEvents::empty(), socket.query_events());
        let connecting_socket = Socket::new(SocketDomain::Unix, SocketType::Stream);
        connecting_socket
            .connect(&socket, current_task.as_ucred())
            .expect("Failed to connect socket.");
        assert_eq!(FdEvents::POLLIN, socket.query_events());
        let server_socket = socket.accept(current_task.as_ucred()).unwrap();

        let message = Message::new(vec![1, 2, 3].into(), None, None);
        server_socket.write_kernel(message.clone()).expect("Failed to write.");

        server_socket.close();
        assert_eq!(FdEvents::POLLHUP, server_socket.query_events());

        assert_eq!(connecting_socket.read_kernel(), vec![message]);
    }

    #[test]
    fn test_dgram_socket() {
        let (_kernel, current_task) = create_kernel_and_task();
        let bind_address = SocketAddress::Unix(b"dgram_test".to_vec());
        let rec_dgram = Socket::new(SocketDomain::Unix, SocketType::Datagram);
        rec_dgram.bind(bind_address).expect("failed to bind datagram socket");

        let xfer_value: u64 = 1234567819;
        let xfer_bytes = xfer_value.to_ne_bytes();
        let source_mem = map_memory(&current_task, UserAddress::default(), xfer_bytes.len() as u64);
        current_task.mm.write_memory(source_mem, &xfer_bytes).unwrap();

        let send = Socket::new(SocketDomain::Unix, SocketType::Datagram);
        let source_buf = [UserBuffer { address: source_mem, length: xfer_bytes.len() }];
        let mut source_iter = UserBufferIterator::new(&source_buf);
        let credentials = ucred { pid: current_task.get_pid(), uid: 0, gid: 0 };
        send.connect(&rec_dgram, credentials).unwrap();
        let no_write = send.write(&current_task, &mut source_iter, &mut None, &mut None).unwrap();
        assert_eq!(no_write, xfer_bytes.len());
        // Previously, this would cause the test to fail,
        // because rec_dgram was shut down.
        send.close();

        let rec_mem = map_memory(&current_task, UserAddress::default(), xfer_bytes.len() as u64);
        let mut recv_mem = [0u8; 8];
        current_task.mm.write_memory(rec_mem, &recv_mem).unwrap();
        let rec_buf = [UserBuffer { address: rec_mem, length: recv_mem.len() }];
        let mut rec_iter = UserBufferIterator::new(&rec_buf);
        let read_info =
            rec_dgram.read(&current_task, &mut rec_iter, SocketMessageFlags::empty()).unwrap();
        assert_eq!(read_info.bytes_read, xfer_bytes.len());
        current_task.mm.read_memory(rec_mem, &mut recv_mem).unwrap();
        assert_eq!(recv_mem, xfer_bytes);

        rec_dgram.close();
    }
}

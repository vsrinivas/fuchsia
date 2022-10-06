// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use zerocopy::{AsBytes, FromBytes};

use super::*;

use crate::fs::buffers::*;
use crate::fs::*;
use crate::lock::Mutex;
use crate::syscalls::{SyscallResult, SUCCESS};
use crate::task::*;
use crate::types::*;

use std::sync::Arc;

// From unix.go in gVisor.
const SOCKET_MIN_SIZE: usize = 4 << 10;
const SOCKET_DEFAULT_SIZE: usize = 208 << 10;
const SOCKET_MAX_SIZE: usize = 4 << 20;

pub struct UnixSocket {
    inner: Mutex<UnixSocketInner>,
}

fn downcast_socket_to_unix(socket: &Socket) -> &UnixSocket {
    // It is a programing error if we are downcasting
    // a different type of socket as sockets from different families
    // should not communicate, so unwrapping here
    // will let us know that.
    socket.downcast_socket::<UnixSocket>().unwrap()
}

enum UnixSocketState {
    /// The socket has not been connected.
    Disconnected,

    /// The socket has had `listen` called and can accept incoming connections.
    Listening(AcceptQueue),

    /// The socket is connected to a peer.
    Connected(SocketHandle),

    /// The socket is closed.
    Closed,
}

struct UnixSocketInner {
    /// The `MessageQueue` that contains messages sent to this socket.
    messages: MessageQueue,

    /// This queue will be notified on reads, writes, disconnects etc.
    waiters: WaitQueue,

    /// The address that this socket has been bound to, if it has been bound.
    address: Option<SocketAddress>,

    /// Whether this end of the socket has been shut down and can no longer receive message. It is
    /// still possible to send messages to the peer, if it exists and hasn't also been shut down.
    is_shutdown: bool,

    /// Whether the peer had unread data when it was closed. In this case, reads should return
    /// ECONNRESET instead of 0 (eof).
    peer_closed_with_unread_data: bool,

    /// See SO_LINGER.
    pub linger: uapi::linger,

    /// See SO_PASSCRED.
    pub passcred: bool,

    /// See SO_BROADCAST.
    pub broadcast: bool,

    /// See SO_NO_CHECK.
    pub no_check: bool,

    /// See SO_REUSEPORT.
    pub reuseport: bool,

    /// See SO_REUSEADDR.
    pub reuseaddr: bool,

    /// See SO_KEEPALIVE.
    pub keepalive: bool,

    /// Unix credentials of the owner of this socket, for SO_PEERCRED.
    credentials: Option<ucred>,

    /// Socket state: a queue if this is a listening socket, or a peer if this is a connected
    /// socket.
    state: UnixSocketState,
}

impl UnixSocket {
    pub fn new(_socket_type: SocketType) -> UnixSocket {
        UnixSocket {
            inner: Mutex::new(UnixSocketInner {
                messages: MessageQueue::new(SOCKET_DEFAULT_SIZE),
                waiters: WaitQueue::default(),
                address: None,
                is_shutdown: false,
                peer_closed_with_unread_data: false,
                linger: uapi::linger::default(),
                passcred: false,
                broadcast: false,
                no_check: false,
                reuseaddr: false,
                reuseport: false,
                keepalive: false,
                credentials: None,
                state: UnixSocketState::Disconnected,
            }),
        }
    }

    /// Creates a pair of connected sockets.
    ///
    /// # Parameters
    /// - `domain`: The domain of the socket (e.g., `AF_UNIX`).
    /// - `socket_type`: The type of the socket (e.g., `SOCK_STREAM`).
    pub fn new_pair(
        current_task: &CurrentTask,
        domain: SocketDomain,
        socket_type: SocketType,
        open_flags: OpenFlags,
    ) -> Result<(FileHandle, FileHandle), Errno> {
        let credentials = current_task.as_ucred();
        let left = Socket::new(domain, socket_type, SocketProtocol::default())?;
        let right = Socket::new(domain, socket_type, SocketProtocol::default())?;
        downcast_socket_to_unix(&left).lock().state = UnixSocketState::Connected(right.clone());
        downcast_socket_to_unix(&left).lock().credentials = Some(credentials.clone());
        downcast_socket_to_unix(&right).lock().state = UnixSocketState::Connected(left.clone());
        downcast_socket_to_unix(&right).lock().credentials = Some(credentials);
        let left = Socket::new_file(current_task, left, open_flags);
        let right = Socket::new_file(current_task, right, open_flags);
        Ok((left, right))
    }

    fn connect_stream(
        &self,
        socket: &SocketHandle,
        peer: &SocketHandle,
        credentials: ucred,
    ) -> Result<(), Errno> {
        // Only hold one lock at a time until we make sure the lock ordering
        // is right: client before listener
        match downcast_socket_to_unix(peer).lock().state {
            UnixSocketState::Listening(_) => {}
            _ => return error!(ECONNREFUSED),
        }

        let mut client = downcast_socket_to_unix(socket).lock();
        match client.state {
            UnixSocketState::Disconnected => {}
            UnixSocketState::Connected(_) => return error!(EISCONN),
            _ => return error!(EINVAL),
        };

        let mut listener = downcast_socket_to_unix(peer).lock();
        // Must check this again because we released the listener lock for a moment
        let queue = match &listener.state {
            UnixSocketState::Listening(queue) => queue,
            _ => return error!(ECONNREFUSED),
        };

        self.check_type_for_connect(socket, peer, &listener.address)?;

        if queue.sockets.len() >= queue.backlog {
            return error!(EAGAIN);
        }

        let server = Socket::new(peer.domain, peer.socket_type, SocketProtocol::default())?;
        client.state = UnixSocketState::Connected(server.clone());
        client.credentials = Some(credentials);
        {
            let mut server = downcast_socket_to_unix(&server).lock();
            server.state = UnixSocketState::Connected(socket.clone());
            server.address = listener.address.clone();
            server.messages.set_capacity(listener.messages.capacity())?;
            server.credentials = listener.credentials.clone();
        }

        // We already checked that the socket is in Listening state...but the borrow checker cannot
        // be convinced that it's ok to combine these checks
        let queue = match listener.state {
            UnixSocketState::Listening(ref mut queue) => queue,
            _ => panic!("something changed the server socket state while I held a lock on it"),
        };
        queue.sockets.push_back(server);
        listener.waiters.notify_events(FdEvents::POLLIN);
        Ok(())
    }

    fn connect_datagram(
        &self,
        socket: &SocketHandle,
        peer: &SocketHandle,
        _credentials: ucred,
    ) -> Result<(), Errno> {
        {
            let unix_socket = socket.downcast_socket::<UnixSocket>().unwrap();
            let peer_inner = unix_socket.lock();
            self.check_type_for_connect(socket, peer, &peer_inner.address)?;
        }
        let unix_socket = socket.downcast_socket::<UnixSocket>().unwrap();
        unix_socket.lock().state = UnixSocketState::Connected(peer.clone());
        Ok(())
    }

    pub fn check_type_for_connect(
        &self,
        socket: &Socket,
        peer: &Socket,
        peer_address: &Option<SocketAddress>,
    ) -> Result<(), Errno> {
        if socket.domain != peer.domain || socket.socket_type != peer.socket_type {
            // According to ConnectWithWrongType in accept_bind_test, abstract
            // UNIX domain sockets return ECONNREFUSED rather than EPROTOTYPE.
            if let Some(address) = peer_address {
                if address.is_abstract_unix() {
                    return error!(ECONNREFUSED);
                }
            }
            return error!(EPROTOTYPE);
        }
        Ok(())
    }

    /// Locks and returns the inner state of the Socket.
    fn lock(&self) -> crate::lock::MutexGuard<'_, UnixSocketInner> {
        self.inner.lock()
    }

    fn is_listening(&self, _socket: &Socket) -> bool {
        matches!(self.lock().state, UnixSocketState::Listening(_))
    }

    fn get_receive_capacity(&self) -> usize {
        self.lock().messages.capacity()
    }

    fn set_receive_capacity(&self, requested_capacity: usize) {
        self.lock().set_capacity(requested_capacity);
    }

    fn get_send_capacity(&self) -> usize {
        let peer = {
            if let Some(peer) = self.lock().peer() {
                peer.clone()
            } else {
                return 0;
            }
        };
        let unix_socket = downcast_socket_to_unix(&peer);
        let capacity = unix_socket.lock().messages.capacity();
        capacity
    }

    fn set_send_capacity(&self, requested_capacity: usize) {
        let peer = {
            if let Some(peer) = self.lock().peer() {
                peer.clone()
            } else {
                return;
            }
        };
        let unix_socket = downcast_socket_to_unix(&peer);
        unix_socket.lock().set_capacity(requested_capacity);
    }

    fn get_linger(&self) -> uapi::linger {
        let inner = self.lock();
        inner.linger
    }

    fn set_linger(&self, linger: uapi::linger) {
        let mut inner = self.lock();
        inner.linger = linger;
    }

    fn get_passcred(&self) -> bool {
        let inner = self.lock();
        inner.passcred
    }

    fn set_passcred(&self, passcred: bool) {
        let mut inner = self.lock();
        inner.passcred = passcred;
    }

    fn get_broadcast(&self) -> bool {
        let inner = self.lock();
        inner.broadcast
    }

    fn set_broadcast(&self, broadcast: bool) {
        let mut inner = self.lock();
        inner.broadcast = broadcast;
    }

    fn get_no_check(&self) -> bool {
        let inner = self.lock();
        inner.no_check
    }

    fn set_no_check(&self, no_check: bool) {
        let mut inner = self.lock();
        inner.no_check = no_check;
    }

    fn get_reuseaddr(&self) -> bool {
        let inner = self.lock();
        inner.reuseaddr
    }

    fn set_reuseaddr(&self, reuseaddr: bool) {
        let mut inner = self.lock();
        inner.reuseaddr = reuseaddr;
    }

    fn get_reuseport(&self) -> bool {
        let inner = self.lock();
        inner.reuseport
    }

    fn set_reuseport(&self, reuseport: bool) {
        let mut inner = self.lock();
        inner.reuseport = reuseport;
    }

    fn get_keepalive(&self) -> bool {
        let inner = self.lock();
        inner.keepalive
    }

    fn set_keepalive(&self, keepalive: bool) {
        let mut inner = self.lock();
        inner.keepalive = keepalive;
    }

    fn peer_cred(&self) -> Option<ucred> {
        let peer = {
            let inner = self.lock();
            inner.peer().cloned()
        };
        if let Some(peer) = peer {
            let unix_socket = downcast_socket_to_unix(&peer);
            let unix_socket = unix_socket.lock();
            unix_socket.credentials.clone()
        } else {
            None
        }
    }

    pub fn bind_socket_to_node(
        &self,
        socket: &SocketHandle,
        address: SocketAddress,
        node: &Arc<FsNode>,
    ) -> Result<(), Errno> {
        let unix_socket = downcast_socket_to_unix(socket);
        let mut inner = unix_socket.lock();
        inner.bind(address)?;
        node.set_socket(socket.clone());
        Ok(())
    }

    // These are used for Unix socket for efficiency
    pub fn blocking_read_kernel(&self, _socket: &Socket) -> Result<Vec<Message>, Errno> {
        loop {
            let mut inner = self.lock();
            let messages = inner.read_kernel()?;
            if !messages.is_empty() {
                return Ok(messages);
            }
            let waiter = Waiter::new();
            inner.waiters.wait_async_events(
                &waiter,
                FdEvents::POLLIN | FdEvents::POLLHUP,
                WaitCallback::none(),
            );
            drop(inner);
            waiter.wait_without_current_task_dont_use_if_possible()?;
        }
    }

    pub fn write_kernel(&self, message: Message) -> Result<(), Errno> {
        let peer = {
            let inner = self.lock();
            inner.peer().ok_or_else(|| errno!(EPIPE))?.clone()
        };
        let unix_socket = downcast_socket_to_unix(&peer);
        let mut peer = unix_socket.lock();
        peer.write_kernel(message)
    }
}

impl SocketOps for UnixSocket {
    fn connect(
        &self,
        socket: &SocketHandle,
        peer: SocketPeer,
        credentials: ucred,
    ) -> Result<(), Errno> {
        let peer = match peer {
            SocketPeer::Handle(handle) => handle,
            SocketPeer::Address(_) => return error!(EINVAL),
        };
        match socket.socket_type {
            SocketType::Stream | SocketType::SeqPacket => {
                self.connect_stream(socket, &peer, credentials)
            }
            SocketType::Datagram | SocketType::Raw => {
                self.connect_datagram(socket, &peer, credentials)
            }
        }
    }

    fn listen(&self, socket: &Socket, backlog: i32, credentials: ucred) -> Result<(), Errno> {
        match socket.socket_type {
            SocketType::Stream | SocketType::SeqPacket => {}
            SocketType::Datagram | SocketType::Raw => return error!(EOPNOTSUPP),
        }
        let mut inner = self.lock();
        inner.credentials = Some(credentials);
        let is_bound = inner.address.is_some();
        let backlog = if backlog < 0 { DEFAULT_LISTEN_BACKLOG } else { backlog as usize };
        match &mut inner.state {
            UnixSocketState::Disconnected if is_bound => {
                inner.state = UnixSocketState::Listening(AcceptQueue::new(backlog));
                Ok(())
            }
            UnixSocketState::Listening(queue) => {
                queue.set_backlog(backlog)?;
                Ok(())
            }
            _ => error!(EINVAL),
        }
    }

    fn accept(&self, socket: &Socket) -> Result<SocketHandle, Errno> {
        match socket.socket_type {
            SocketType::Stream | SocketType::SeqPacket => {}
            SocketType::Datagram | SocketType::Raw => return error!(EOPNOTSUPP),
        }
        let mut inner = self.lock();
        let queue = match &mut inner.state {
            UnixSocketState::Listening(queue) => queue,
            _ => return error!(EINVAL),
        };
        queue.sockets.pop_front().ok_or_else(|| errno!(EAGAIN))
    }

    fn remote_connection(&self, _socket: &Socket, _file: FileHandle) -> Result<(), Errno> {
        error!(EOPNOTSUPP)
    }

    fn bind(&self, _socket: &Socket, socket_address: SocketAddress) -> Result<(), Errno> {
        match socket_address {
            SocketAddress::Unix(_) => {}
            _ => return error!(EINVAL),
        }
        self.lock().bind(socket_address)
    }

    fn read(
        &self,
        socket: &Socket,
        current_task: &CurrentTask,
        user_buffers: &mut UserBufferIterator<'_>,
        flags: SocketMessageFlags,
    ) -> Result<MessageReadInfo, Errno> {
        let info = self.lock().read(current_task, user_buffers, socket.socket_type, flags)?;
        if info.bytes_read > 0 {
            let peer = {
                let inner = self.lock();
                inner.peer().cloned()
            };
            if let Some(socket) = peer {
                let unix_socket_peer = socket.downcast_socket::<UnixSocket>();
                if let Some(socket) = unix_socket_peer {
                    socket.lock().waiters.notify_events(FdEvents::POLLOUT);
                }
            }
        }
        Ok(info)
    }

    fn write(
        &self,
        socket: &Socket,
        current_task: &CurrentTask,
        user_buffers: &mut UserBufferIterator<'_>,
        dest_address: &mut Option<SocketAddress>,
        ancillary_data: &mut Vec<AncillaryData>,
    ) -> Result<usize, Errno> {
        let (peer, local_address, creds) = {
            let inner = self.lock();
            (
                inner.peer().ok_or_else(|| errno!(EPIPE))?.clone(),
                inner.address.clone(),
                inner.credentials.clone(),
            )
        };

        // TODO allow non-connected datagrams
        if dest_address.is_some() {
            return error!(EISCONN);
        }
        let unix_socket = downcast_socket_to_unix(&peer);
        let mut peer = unix_socket.lock();
        if peer.passcred {
            let creds = creds.unwrap_or_else(|| current_task.as_ucred());
            ancillary_data.push(AncillaryData::Unix(UnixControlData::Credentials(creds)));
        }
        peer.write(current_task, user_buffers, local_address, ancillary_data, socket.socket_type)
    }

    fn wait_async(
        &self,
        socket: &Socket,
        current_task: &CurrentTask,
        waiter: &Waiter,
        events: FdEvents,
        handler: EventHandler,
        options: WaitAsyncOptions,
    ) -> WaitKey {
        let present_events = self.query_events(socket, current_task);
        if events & present_events && !options.contains(WaitAsyncOptions::EDGE_TRIGGERED) {
            waiter.wake_immediately(present_events.mask(), handler)
        } else {
            self.lock().waiters.wait_async_mask(waiter, events.mask(), handler)
        }
    }

    fn cancel_wait(
        &self,
        _socket: &Socket,
        _current_task: &CurrentTask,
        _waiter: &Waiter,
        key: WaitKey,
    ) {
        let mut inner = self.lock();
        inner.waiters.cancel_wait(key);
    }

    fn query_events(&self, _socket: &Socket, _current_task: &CurrentTask) -> FdEvents {
        // Note that self.lock() must be dropped before acquiring peer.inner.lock() to avoid
        // potential deadlocks.
        let (mut present_events, peer) = {
            let inner = self.lock();
            (inner.query_events(), inner.peer().cloned())
        };

        if let Some(peer) = peer {
            let unix_socket = downcast_socket_to_unix(&peer);
            let peer_inner = unix_socket.lock();
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
    fn shutdown(&self, _socket: &Socket, how: SocketShutdownFlags) -> Result<(), Errno> {
        let peer = {
            let mut inner = self.lock();
            let peer = inner.peer().ok_or_else(|| errno!(ENOTCONN))?.clone();
            if how.contains(SocketShutdownFlags::READ) {
                inner.shutdown_one_end();
            }
            peer
        };
        if how.contains(SocketShutdownFlags::WRITE) {
            let unix_socket = downcast_socket_to_unix(&peer);
            unix_socket.lock().shutdown_one_end();
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
    fn close(&self, socket: &Socket) {
        let (maybe_peer, has_unread) = {
            let mut inner = self.lock();
            let maybe_peer = inner.peer().map(Arc::clone);
            inner.shutdown_one_end();
            (maybe_peer, !inner.messages.is_empty())
        };
        // If this is a connected socket type, also shut down the connected peer.
        if socket.socket_type == SocketType::Stream || socket.socket_type == SocketType::SeqPacket {
            if let Some(peer) = maybe_peer {
                let unix_socket = downcast_socket_to_unix(&peer);

                let mut peer_inner = unix_socket.lock();
                if has_unread {
                    peer_inner.peer_closed_with_unread_data = true;
                }
                peer_inner.shutdown_one_end();
            }
        }
        self.lock().state = UnixSocketState::Closed;
    }

    /// Returns the name of this socket.
    ///
    /// The name is derived from the address and domain. A socket
    /// will always have a name, even if it is not bound to an address.
    fn getsockname(&self, socket: &Socket) -> Vec<u8> {
        let inner = self.lock();
        if let Some(address) = &inner.address {
            address.to_bytes()
        } else {
            SocketAddress::default_for_domain(socket.domain).to_bytes()
        }
    }

    /// Returns the name of the peer of this socket, if such a peer exists.
    ///
    /// Returns an error if the socket is not connected.
    fn getpeername(&self, _socket: &Socket) -> Result<Vec<u8>, Errno> {
        let peer = self.lock().peer().ok_or_else(|| errno!(ENOTCONN))?.clone();
        Ok(peer.getsockname())
    }

    fn setsockopt(
        &self,
        _socket: &Socket,
        task: &Task,
        level: u32,
        optname: u32,
        user_opt: UserBuffer,
    ) -> Result<(), Errno> {
        fn read<T: Default + AsBytes + FromBytes>(
            task: &Task,
            user_opt: UserBuffer,
        ) -> Result<T, Errno> {
            let user_ref = UserRef::<T>::from_buf(user_opt).ok_or_else(|| errno!(EINVAL))?;
            task.mm.read_object(user_ref)
        }

        match level {
            SOL_SOCKET => match optname {
                SO_SNDBUF => {
                    let requested_capacity = read::<socklen_t>(task, user_opt)? as usize;
                    // See StreamUnixSocketPairTest.SetSocketSendBuf for why we multiply by 2 here.
                    self.set_send_capacity(requested_capacity * 2);
                }
                SO_RCVBUF => {
                    let requested_capacity = read::<socklen_t>(task, user_opt)? as usize;
                    self.set_receive_capacity(requested_capacity);
                }
                SO_LINGER => {
                    let mut linger = read::<uapi::linger>(task, user_opt)?;
                    if linger.l_onoff != 0 {
                        linger.l_onoff = 1;
                    }
                    self.set_linger(linger);
                }
                SO_PASSCRED => {
                    let passcred = read::<u32>(task, user_opt)?;
                    self.set_passcred(passcred != 0);
                }
                SO_BROADCAST => {
                    let broadcast = read::<u32>(task, user_opt)?;
                    self.set_broadcast(broadcast != 0);
                }
                SO_NO_CHECK => {
                    let no_check = read::<u32>(task, user_opt)?;
                    self.set_no_check(no_check != 0);
                }
                SO_REUSEADDR => {
                    let reuseaddr = read::<u32>(task, user_opt)?;
                    self.set_reuseaddr(reuseaddr != 0);
                }
                SO_REUSEPORT => {
                    let reuseport = read::<u32>(task, user_opt)?;
                    self.set_reuseport(reuseport != 0);
                }
                SO_KEEPALIVE => {
                    let keepalive = read::<u32>(task, user_opt)?;
                    self.set_keepalive(keepalive != 0);
                }
                _ => return error!(ENOPROTOOPT),
            },
            _ => return error!(ENOPROTOOPT),
        }
        Ok(())
    }

    fn getsockopt(
        &self,
        socket: &Socket,
        level: u32,
        optname: u32,
        _optlen: u32,
    ) -> Result<Vec<u8>, Errno> {
        let opt_value = match level {
            SOL_SOCKET => match optname {
                SO_PEERCRED => self
                    .peer_cred()
                    .unwrap_or(ucred { pid: 0, uid: uid_t::MAX, gid: gid_t::MAX })
                    .as_bytes()
                    .to_owned(),
                SO_PEERSEC => "unconfined".as_bytes().to_vec(),
                SO_ACCEPTCONN => {
                    if self.is_listening(socket) { 1u32 } else { 0u32 }.to_ne_bytes().to_vec()
                }
                SO_SNDBUF => (self.get_send_capacity() as socklen_t).to_ne_bytes().to_vec(),
                SO_RCVBUF => (self.get_receive_capacity() as socklen_t).to_ne_bytes().to_vec(),
                SO_LINGER => self.get_linger().as_bytes().to_vec(),
                SO_PASSCRED => (self.get_passcred() as u32).as_bytes().to_vec(),
                SO_BROADCAST => (self.get_broadcast() as u32).as_bytes().to_vec(),
                SO_NO_CHECK => (self.get_no_check() as u32).as_bytes().to_vec(),
                SO_REUSEADDR => (self.get_reuseaddr() as u32).as_bytes().to_vec(),
                SO_REUSEPORT => (self.get_reuseport() as u32).as_bytes().to_vec(),
                SO_KEEPALIVE => (self.get_keepalive() as u32).as_bytes().to_vec(),
                _ => return error!(ENOPROTOOPT),
            },
            _ => return error!(ENOPROTOOPT),
        };
        Ok(opt_value)
    }

    fn ioctl(
        &self,
        socket: &Socket,
        current_task: &CurrentTask,
        request: u32,
        user_addr: UserAddress,
    ) -> Result<SyscallResult, Errno> {
        match request {
            FIONREAD if socket.socket_type == SocketType::Stream => {
                let length: i32 =
                    self.lock().messages.len().try_into().map_err(|_| errno!(EINVAL))?;
                current_task.mm.write_object(UserRef::<i32>::new(user_addr), &length)?;
                Ok(SUCCESS)
            }
            _ => default_ioctl(current_task, request),
        }
    }
}

impl UnixSocketInner {
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
            UnixSocketState::Connected(peer) => Some(peer),
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
        current_task: &CurrentTask,
        user_buffers: &mut UserBufferIterator<'_>,
        socket_type: SocketType,
        flags: SocketMessageFlags,
    ) -> Result<MessageReadInfo, Errno> {
        if self.peer_closed_with_unread_data {
            return error!(ECONNRESET);
        }
        let mut info = if socket_type == SocketType::Stream {
            if flags.contains(SocketMessageFlags::PEEK) {
                self.messages.peek_stream(current_task, user_buffers)?
            } else {
                self.messages.read_stream(current_task, user_buffers)?
            }
        } else if flags.contains(SocketMessageFlags::PEEK) {
            self.messages.peek_datagram(current_task, user_buffers)?
        } else {
            self.messages.read_datagram(current_task, user_buffers)?
        };
        if info.message_length == 0 && !self.is_shutdown {
            return error!(EAGAIN);
        }

        // Remove any credentials message, so that it can be moved to the front if passcred is
        // enabled, or simply be removed if passcred is not enabled.
        let creds_message;
        if let Some(index) = info
            .ancillary_data
            .iter()
            .position(|m| matches!(m, AncillaryData::Unix(UnixControlData::Credentials { .. })))
        {
            creds_message = info.ancillary_data.remove(index)
        } else {
            // If passcred is enabled credentials are returned even if they were not sent.
            creds_message = AncillaryData::Unix(UnixControlData::unknown_creds());
        }
        if self.passcred {
            // Allow credentials to take priority if they are enabled, so insert at 0.
            info.ancillary_data.insert(0, creds_message);
        }

        Ok(info)
    }

    /// Reads all the available messages out of this socket.
    ///
    /// An empty vector is returned if no data is immediately available.
    /// An `Err` is returned if the socket was shutdown.
    pub fn read_kernel(&mut self) -> Result<Vec<Message>, Errno> {
        let bytes_read = self.messages.len();
        let messages = self.messages.take_messages();

        // Only signal a broken pipe once the messages have been drained.
        if messages.is_empty() && self.is_shutdown {
            return error!(EPIPE);
        }

        if bytes_read > 0 {
            self.waiters.notify_events(FdEvents::POLLOUT);
        }

        Ok(messages)
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
        current_task: &CurrentTask,
        user_buffers: &mut UserBufferIterator<'_>,
        address: Option<SocketAddress>,
        ancillary_data: &mut Vec<AncillaryData>,
        socket_type: SocketType,
    ) -> Result<usize, Errno> {
        if self.is_shutdown {
            return error!(EPIPE);
        }
        let bytes_written = if socket_type == SocketType::Stream {
            self.messages.write_stream(current_task, user_buffers, address, ancillary_data)?
        } else {
            self.messages.write_datagram(current_task, user_buffers, address, ancillary_data)?
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
        if self.is_shutdown {
            return error!(EPIPE);
        }

        let bytes_written = message.data.len();
        self.messages.write_message(message);

        if bytes_written > 0 {
            self.waiters.notify_events(FdEvents::POLLIN);
        }

        Ok(())
    }

    fn shutdown_one_end(&mut self) {
        self.is_shutdown = true;
        self.waiters.notify_events(FdEvents::POLLIN | FdEvents::POLLOUT | FdEvents::POLLHUP);
    }

    fn query_events(&self) -> FdEvents {
        let mut present_events = FdEvents::empty();
        let local_events = self.messages.query_events();
        if local_events & FdEvents::POLLIN {
            present_events = FdEvents::POLLIN;
        }

        if self.is_shutdown {
            present_events |= FdEvents::POLLIN | FdEvents::POLLOUT | FdEvents::POLLHUP;
        }

        match &self.state {
            UnixSocketState::Listening(queue) => {
                if !queue.sockets.is_empty() {
                    present_events |= FdEvents::POLLIN;
                }
            }
            UnixSocketState::Closed => {
                present_events |= FdEvents::POLLHUP;
            }
            _ => {}
        }

        present_events
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::testing::*;
    use std::convert::TryInto;

    #[::fuchsia::test]
    fn test_socket_send_capacity() {
        let (_kernel, current_task) = create_kernel_and_task();
        let socket = Socket::new(SocketDomain::Unix, SocketType::Stream, SocketProtocol::default())
            .expect("Failed to create socket.");
        socket.bind(SocketAddress::Unix(b"\0".to_vec())).expect("Failed to bind socket.");
        socket.listen(10, current_task.as_ucred()).expect("Failed to listen.");
        let connecting_socket =
            Socket::new(SocketDomain::Unix, SocketType::Stream, SocketProtocol::default())
                .expect("Failed to connect socket.");
        connecting_socket
            .connect(SocketPeer::Handle(socket.clone()), current_task.as_ucred())
            .expect("Failed to connect socket.");
        assert_eq!(FdEvents::POLLIN, socket.query_events(&current_task));
        let server_socket = socket.accept().unwrap();

        let opt_size = std::mem::size_of::<socklen_t>();
        let user_address = map_memory(&current_task, UserAddress::default(), opt_size as u64);
        let send_capacity: socklen_t = 4 * 4096;
        current_task.mm.write_memory(user_address, &send_capacity.to_ne_bytes()).unwrap();
        let user_buffer = UserBuffer { address: user_address, length: opt_size as usize };
        server_socket.setsockopt(&current_task, SOL_SOCKET, SO_SNDBUF, user_buffer).unwrap();

        let opt_bytes = server_socket.getsockopt(SOL_SOCKET, SO_SNDBUF, 0).unwrap();
        let retrieved_capacity = socklen_t::from_ne_bytes(opt_bytes.try_into().unwrap());
        // Setting SO_SNDBUF actually sets it to double the size
        assert_eq!(2 * send_capacity, retrieved_capacity);
    }
}

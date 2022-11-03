// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon as zx;
use std::collections::VecDeque;
use zerocopy::AsBytes;

use super::*;

use crate::fs::buffers::*;
use crate::fs::*;
use crate::lock::Mutex;
use crate::mm::MemoryAccessorExt;
use crate::syscalls::SyscallResult;
use crate::task::*;
use crate::types::as_any::*;
use crate::types::*;

use std::sync::Arc;

pub const DEFAULT_LISTEN_BACKLOG: usize = 1024;

pub trait SocketOps: Send + Sync + AsAny {
    /// Connect the `socket` to the listening `peer`. On success
    /// a new socket is created and added to the accept queue.
    fn connect(
        &self,
        socket: &SocketHandle,
        peer: SocketPeer,
        credentials: ucred,
    ) -> Result<(), Errno>;

    /// Start listening at the bound address for `connect` calls.
    fn listen(&self, socket: &Socket, backlog: i32, credentials: ucred) -> Result<(), Errno>;

    /// Returns the eariest socket on the accept queue of this
    /// listening socket. Returns EAGAIN if the queue is empty.
    fn accept(&self, socket: &Socket) -> Result<SocketHandle, Errno>;

    /// Used for connecting Zircon-based objects, such as RemotePipeObject
    /// to listening sockets. This differs from connect, in that the `socket`
    /// handed to `connect` is retained by the caller and used for communicating
    ///
    /// # Parameters
    ///
    /// - `socket`: A listening socket (for streams) or a datagram socket
    /// - `local_handle`: our side of a remote socket connection
    fn remote_connection(&self, socket: &Socket, local_handle: FileHandle) -> Result<(), Errno>;

    /// Binds this socket to a `socket_address`.
    ///
    /// Returns an error if the socket could not be bound.
    fn bind(&self, socket: &Socket, socket_address: SocketAddress) -> Result<(), Errno>;

    /// Reads the specified number of bytes from the socket, if possible.
    ///
    /// # Parameters
    /// - `task`: The task to which the user buffers belong (i.e., the task to which the read bytes
    ///           are written.
    /// - `user_buffers`: The buffers to write the read data into.
    ///
    /// Returns the number of bytes that were written to the user buffers, as well as any ancillary
    /// data associated with the read messages.
    fn read(
        &self,
        socket: &Socket,
        current_task: &CurrentTask,
        user_buffers: &mut UserBufferIterator<'_>,
        flags: SocketMessageFlags,
    ) -> Result<MessageReadInfo, Errno>;

    /// Writes the data in the provided user buffers to this socket.
    ///
    /// # Parameters
    /// - `task`: The task to which the user buffers belong, used to read the memory.
    /// - `user_buffers`: The data to write to the socket.
    /// - `ancillary_data`: Optional ancillary data (a.k.a., control message) to write.
    ///
    /// Returns the number of bytes that were read from the user buffers and written to the socket,
    /// not counting the ancillary data.
    fn write(
        &self,
        socket: &Socket,
        current_task: &CurrentTask,
        user_buffers: &mut UserBufferIterator<'_>,
        dest_address: &mut Option<SocketAddress>,
        ancillary_data: &mut Vec<AncillaryData>,
    ) -> Result<usize, Errno>;

    /// Queues an asynchronous wait for the specified `events`
    /// on the `waiter`. Note that no wait occurs until a
    /// wait functions is called on the `waiter`.
    ///
    /// # Parameters
    /// - `waiter`: The Waiter that can be waited on, for example by
    ///             calling Waiter::wait_until.
    /// - `events`: The events that will trigger the waiter to wake up.
    /// - `handler`: A handler that will be called on wake-up.
    /// Returns a WaitKey that can be used to cancel the wait with
    /// `cancel_wait`
    fn wait_async(
        &self,
        socket: &Socket,
        current_task: &CurrentTask,
        waiter: &Waiter,
        events: FdEvents,
        handler: EventHandler,
        options: WaitAsyncOptions,
    ) -> WaitKey;

    /// Cancel a wait previously set up with `wait_async`.
    /// Returns `true` if the wait was actually cancelled.
    /// If the wait has already been triggered, this returns `false`.
    fn cancel_wait(
        &self,
        socket: &Socket,
        current_task: &CurrentTask,
        waiter: &Waiter,
        key: WaitKey,
    );

    /// Return the events that are currently active on the `socket`.
    fn query_events(&self, socket: &Socket, current_task: &CurrentTask) -> FdEvents;

    /// Shuts down this socket according to how, preventing any future reads and/or writes.
    ///
    /// Used by the shutdown syscalls.
    fn shutdown(&self, socket: &Socket, how: SocketShutdownFlags) -> Result<(), Errno>;

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
    fn close(&self, socket: &Socket);

    /// Returns the name of this socket.
    ///
    /// The name is derived from the address and domain. A socket
    /// will always have a name, even if it is not bound to an address.
    fn getsockname(&self, socket: &Socket) -> Vec<u8>;

    /// Returns the name of the peer of this socket, if such a peer exists.
    ///
    /// Returns an error if the socket is not connected.
    fn getpeername(&self, socket: &Socket) -> Result<Vec<u8>, Errno>;

    /// Sets socket-specific options.
    fn setsockopt(
        &self,
        _socket: &Socket,
        _task: &Task,
        _level: u32,
        _optname: u32,
        _user_opt: UserBuffer,
    ) -> Result<(), Errno> {
        error!(ENOPROTOOPT)
    }

    /// Retrieves socket-specific options.
    fn getsockopt(
        &self,
        _socket: &Socket,
        _level: u32,
        _optname: u32,
        _optlen: u32,
    ) -> Result<Vec<u8>, Errno> {
        error!(ENOPROTOOPT)
    }

    /// Implements ioctl.
    fn ioctl(
        &self,
        _socket: &Socket,
        current_task: &CurrentTask,
        request: u32,
        _address: UserAddress,
    ) -> Result<SyscallResult, Errno> {
        default_ioctl(current_task, request)
    }
}

/// A `Socket` represents one endpoint of a bidirectional communication channel.
pub struct Socket {
    ops: Box<dyn SocketOps>,

    /// The domain of this socket.
    pub domain: SocketDomain,

    /// The type of this socket.
    pub socket_type: SocketType,

    state: Mutex<SocketState>,
}

#[derive(Default)]
struct SocketState {
    /// The value of SO_RCVTIMEO.
    receive_timeout: Option<zx::Duration>,

    /// The value for SO_SNDTIMEO.
    send_timeout: Option<zx::Duration>,
}

pub type SocketHandle = Arc<Socket>;

pub enum SocketPeer {
    Handle(SocketHandle),
    Address(SocketAddress),
}

fn create_socket_ops(
    domain: SocketDomain,
    socket_type: SocketType,
    protocol: SocketProtocol,
) -> Result<Box<dyn SocketOps>, Errno> {
    match domain {
        SocketDomain::Unix => Ok(Box::new(UnixSocket::new(socket_type))),
        SocketDomain::Vsock => Ok(Box::new(VsockSocket::new(socket_type))),
        SocketDomain::Inet | SocketDomain::Inet6 => {
            Ok(Box::new(InetSocket::new(domain, socket_type, protocol)?))
        }
        SocketDomain::Netlink => Ok(Box::new(NetlinkSocket::new(socket_type))),
    }
}

impl Socket {
    /// Creates a new unbound socket.
    ///
    /// # Parameters
    /// - `domain`: The domain of the socket (e.g., `AF_UNIX`).
    pub fn new(
        domain: SocketDomain,
        socket_type: SocketType,
        protocol: SocketProtocol,
    ) -> Result<SocketHandle, Errno> {
        let ops = create_socket_ops(domain, socket_type, protocol)?;
        Ok(Arc::new(Socket { ops, domain, socket_type, state: Mutex::default() }))
    }

    pub fn new_with_ops(
        domain: SocketDomain,
        socket_type: SocketType,
        ops: Box<dyn SocketOps>,
    ) -> SocketHandle {
        Arc::new(Socket { ops, domain, socket_type, state: Mutex::default() })
    }

    /// Creates a `FileHandle` where the associated `FsNode` contains a socket.
    ///
    /// # Parameters
    /// - `kernel`: The kernel that is used to fetch `SocketFs`, to store the created socket node.
    /// - `socket`: The socket to store in the `FsNode`.
    /// - `open_flags`: The `OpenFlags` which are used to create the `FileObject`.
    pub fn new_file(
        current_task: &CurrentTask,
        socket: SocketHandle,
        open_flags: OpenFlags,
    ) -> FileHandle {
        let fs = socket_fs(current_task.kernel());
        let mode = mode!(IFSOCK, 0o777);
        let node = fs.create_node(Box::new(Anon), mode, current_task.as_fscred());
        node.set_socket(socket.clone());
        FileObject::new_anonymous(SocketFile::new(socket), node, open_flags)
    }

    pub fn downcast_socket<T>(&self) -> Option<&T>
    where
        T: 'static,
    {
        let ops = &*self.ops;
        ops.as_any().downcast_ref::<T>()
    }

    pub fn getsockname(&self) -> Vec<u8> {
        self.ops.getsockname(self)
    }

    pub fn getpeername(&self) -> Result<Vec<u8>, Errno> {
        self.ops.getpeername(self)
    }

    pub fn setsockopt(
        &self,
        task: &Task,
        level: u32,
        optname: u32,
        user_opt: UserBuffer,
    ) -> Result<(), Errno> {
        let read_timeval = || {
            let timeval_ref =
                UserRef::<timeval>::from_buf(user_opt).ok_or_else(|| errno!(EINVAL))?;
            let duration = duration_from_timeval(task.mm.read_object(timeval_ref)?)?;
            Ok(if duration == zx::Duration::default() { None } else { Some(duration) })
        };

        match level {
            SOL_SOCKET => match optname {
                SO_RCVTIMEO => self.state.lock().receive_timeout = read_timeval()?,
                SO_SNDTIMEO => self.state.lock().send_timeout = read_timeval()?,
                _ => self.ops.setsockopt(self, task, level, optname, user_opt)?,
            },
            _ => self.ops.setsockopt(self, task, level, optname, user_opt)?,
        }
        Ok(())
    }

    pub fn getsockopt(&self, level: u32, optname: u32, optlen: u32) -> Result<Vec<u8>, Errno> {
        let value = match level {
            SOL_SOCKET => match optname {
                SO_TYPE => self.socket_type.as_raw().to_ne_bytes().to_vec(),
                SO_DOMAIN => self.domain.as_raw().to_ne_bytes().to_vec(),

                SO_RCVTIMEO => {
                    let duration = self.receive_timeout().unwrap_or_default();
                    timeval_from_duration(duration).as_bytes().to_owned()
                }
                SO_SNDTIMEO => {
                    let duration = self.send_timeout().unwrap_or_default();
                    timeval_from_duration(duration).as_bytes().to_owned()
                }
                _ => self.ops.getsockopt(self, level, optname, optlen)?,
            },
            _ => self.ops.getsockopt(self, level, optname, optlen)?,
        };
        Ok(value)
    }

    pub fn receive_timeout(&self) -> Option<zx::Duration> {
        self.state.lock().receive_timeout
    }

    pub fn send_timeout(&self) -> Option<zx::Duration> {
        self.state.lock().send_timeout
    }

    pub fn ioctl(
        &self,
        current_task: &CurrentTask,
        request: u32,
        address: UserAddress,
    ) -> Result<SyscallResult, Errno> {
        self.ops.ioctl(self, current_task, request, address)
    }

    pub fn bind(&self, socket_address: SocketAddress) -> Result<(), Errno> {
        self.ops.bind(self, socket_address)
    }

    pub fn connect(self: &SocketHandle, peer: SocketPeer, credentials: ucred) -> Result<(), Errno> {
        self.ops.connect(self, peer, credentials)
    }

    pub fn listen(&self, backlog: i32, credentials: ucred) -> Result<(), Errno> {
        self.ops.listen(self, backlog, credentials)
    }

    pub fn accept(&self) -> Result<SocketHandle, Errno> {
        self.ops.accept(self)
    }

    #[allow(dead_code)]
    pub fn remote_connection(&self, file: FileHandle) -> Result<(), Errno> {
        self.ops.remote_connection(self, file)
    }

    pub fn read(
        &self,
        current_task: &CurrentTask,
        user_buffers: &mut UserBufferIterator<'_>,
        flags: SocketMessageFlags,
    ) -> Result<MessageReadInfo, Errno> {
        self.ops.read(self, current_task, user_buffers, flags)
    }

    /// Reads all the available messages out of this socket, blocking if no messages are immediately
    /// available.
    ///
    /// Returns `Err` if the socket was shutdown or not a UnixSocket.
    pub fn blocking_read_kernel(&self) -> Result<Vec<Message>, Errno> {
        if let Some(socket) = self.downcast_socket::<UnixSocket>() {
            socket.blocking_read_kernel(self)
        } else {
            error!(EOPNOTSUPP)
        }
    }

    pub fn write(
        &self,
        current_task: &CurrentTask,
        user_buffers: &mut UserBufferIterator<'_>,
        dest_address: &mut Option<SocketAddress>,
        ancillary_data: &mut Vec<AncillaryData>,
    ) -> Result<usize, Errno> {
        self.ops.write(self, current_task, user_buffers, dest_address, ancillary_data)
    }

    /// Writes the provided message into this socket. If the write succeeds, all the bytes were
    /// written.
    ///
    /// # Parameters
    /// - `message`: The message to write.
    ///
    /// Returns an error if the socket is not connected or not a UnixSocket.
    pub fn write_kernel(&self, message: Message) -> Result<(), Errno> {
        if let Some(socket) = self.downcast_socket::<UnixSocket>() {
            socket.write_kernel(message)
        } else {
            error!(EOPNOTSUPP)
        }
    }

    pub fn wait_async(
        &self,
        current_task: &CurrentTask,
        waiter: &Waiter,
        events: FdEvents,
        handler: EventHandler,
        options: WaitAsyncOptions,
    ) -> WaitKey {
        self.ops.wait_async(self, current_task, waiter, events, handler, options)
    }

    pub fn cancel_wait(&self, current_task: &CurrentTask, waiter: &Waiter, key: WaitKey) {
        self.ops.cancel_wait(self, current_task, waiter, key)
    }

    pub fn query_events(&self, current_task: &CurrentTask) -> FdEvents {
        self.ops.query_events(self, current_task)
    }

    pub fn shutdown(&self, how: SocketShutdownFlags) -> Result<(), Errno> {
        self.ops.shutdown(self, how)
    }

    pub fn close(&self) {
        self.ops.close(self)
    }
}

pub struct AcceptQueue {
    pub sockets: VecDeque<SocketHandle>,
    pub backlog: usize,
}

impl AcceptQueue {
    pub fn new(backlog: usize) -> AcceptQueue {
        AcceptQueue { sockets: VecDeque::with_capacity(backlog), backlog }
    }

    pub fn set_backlog(&mut self, backlog: usize) -> Result<(), Errno> {
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
    use crate::mm::MemoryAccessor;
    use crate::testing::*;

    #[::fuchsia::test]
    fn test_read_write_kernel() {
        let (_kernel, current_task) = create_kernel_and_task();
        let socket = Socket::new(SocketDomain::Unix, SocketType::Stream, SocketProtocol::default())
            .expect("Failed to create socket.");
        socket.bind(SocketAddress::Unix(b"\0".to_vec())).expect("Failed to bind socket.");
        socket.listen(10, current_task.as_ucred()).expect("Failed to listen.");
        assert_eq!(FdEvents::empty(), socket.query_events(&current_task));
        let connecting_socket =
            Socket::new(SocketDomain::Unix, SocketType::Stream, SocketProtocol::default())
                .expect("Failed to create socket.");
        connecting_socket
            .connect(SocketPeer::Handle(socket.clone()), current_task.as_ucred())
            .expect("Failed to connect socket.");
        assert_eq!(FdEvents::POLLIN, socket.query_events(&current_task));
        let server_socket = socket.accept().unwrap();

        let message = Message::new(vec![1, 2, 3].into(), None, vec![]);
        let expected_message = message.clone();

        // Do a blocking read in another thread.
        let handle = std::thread::spawn(move || {
            assert_eq!(connecting_socket.blocking_read_kernel(), Ok(vec![expected_message]));
            assert_eq!(connecting_socket.blocking_read_kernel(), error!(EPIPE));
        });

        server_socket.write_kernel(message).expect("Failed to write.");
        server_socket.close();

        // The thread should finish now that messages have been written.
        handle.join().expect("failed to join thread");

        assert_eq!(
            FdEvents::POLLHUP | FdEvents::POLLIN | FdEvents::POLLOUT,
            server_socket.query_events(&current_task)
        );
    }

    #[::fuchsia::test]
    fn test_dgram_socket() {
        let (_kernel, current_task) = create_kernel_and_task();
        let bind_address = SocketAddress::Unix(b"dgram_test".to_vec());
        let rec_dgram =
            Socket::new(SocketDomain::Unix, SocketType::Datagram, SocketProtocol::default())
                .expect("Failed to create socket.");
        let passcred: u32 = 1;
        let opt_size = std::mem::size_of::<u32>();
        let user_address = map_memory(&current_task, UserAddress::default(), opt_size as u64);
        let opt_ref = UserRef::<u32>::new(user_address);
        current_task.mm.write_object(opt_ref, &passcred).unwrap();
        let opt_buf = UserBuffer { address: user_address, length: opt_size };
        rec_dgram.setsockopt(&current_task, SOL_SOCKET, SO_PASSCRED, opt_buf).unwrap();

        rec_dgram.bind(bind_address).expect("failed to bind datagram socket");

        let xfer_value: u64 = 1234567819;
        let xfer_bytes = xfer_value.to_ne_bytes();
        let source_mem = map_memory(&current_task, UserAddress::default(), xfer_bytes.len() as u64);
        current_task.mm.write_memory(source_mem, &xfer_bytes).unwrap();

        let send = Socket::new(SocketDomain::Unix, SocketType::Datagram, SocketProtocol::default())
            .expect("Failed to connect socket.");
        let source_buf = [UserBuffer { address: source_mem, length: xfer_bytes.len() }];
        let mut source_iter = UserBufferIterator::new(&source_buf);
        let task_pid = current_task.get_pid();
        let credentials = ucred { pid: task_pid, uid: 0, gid: 0 };
        send.connect(SocketPeer::Handle(rec_dgram.clone()), credentials.clone()).unwrap();
        let no_write = send.write(&current_task, &mut source_iter, &mut None, &mut vec![]).unwrap();
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
        assert_eq!(1, read_info.ancillary_data.len());
        assert_eq!(
            read_info.ancillary_data[0],
            AncillaryData::Unix(UnixControlData::Credentials(credentials))
        );

        rec_dgram.close();
    }
}

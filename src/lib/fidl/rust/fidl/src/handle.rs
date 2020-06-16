// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A portable representation of handle-like objects for fidl.

#[cfg(target_os = "fuchsia")]
pub use fuchsia_handles::*;

#[cfg(not(target_os = "fuchsia"))]
pub use non_fuchsia_handles::*;

/// Fuchsia implementation of handles just aliases the zircon library
#[cfg(target_os = "fuchsia")]
pub mod fuchsia_handles {
    use crate::invoke_for_handle_types;

    use fuchsia_async as fasync;
    use fuchsia_zircon as zx;

    pub use zx::AsHandleRef;
    pub use zx::Handle;
    pub use zx::HandleBased;
    pub use zx::HandleRef;
    pub use zx::MessageBuf;

    macro_rules! fuchsia_handle {
        ($x:tt, Stub) => {
            /// Stub implementation of Zircon handle type $x.
            #[derive(Debug, Eq, PartialEq, Ord, PartialOrd, Hash)]
            #[repr(transparent)]
            pub struct $x(zx::Handle);

            impl zx::AsHandleRef for $x {
                fn as_handle_ref(&self) -> HandleRef<'_> {
                    self.0.as_handle_ref()
                }
            }
            impl From<Handle> for $x {
                fn from(handle: Handle) -> Self {
                    $x(handle)
                }
            }
            impl From<$x> for Handle {
                fn from(x: $x) -> Handle {
                    x.0
                }
            }
            impl zx::HandleBased for $x {}
        };
        ($x:tt, $availability:tt) => {
            pub use zx::$x;
        };
    }

    invoke_for_handle_types!(fuchsia_handle);

    pub use fasync::Channel as AsyncChannel;
    pub use fasync::Socket as AsyncSocket;

    pub use zx::SocketOpts;
}

/// Non-Fuchsia implementation of handles
#[cfg(not(target_os = "fuchsia"))]
pub mod non_fuchsia_handles {
    use crate::invoke_for_handle_types;

    use fuchsia_zircon_status as zx_status;
    use futures::{future::poll_fn, ready, task::noop_waker_ref};
    use parking_lot::Mutex;
    use slab::Slab;
    use std::{
        borrow::BorrowMut,
        collections::VecDeque,
        pin::Pin,
        sync::atomic::{AtomicU64, Ordering},
        task::{Context, Poll, Waker},
    };

    /// Invalid handle value
    pub const INVALID_HANDLE: u32 = 0xffff_ffff;

    /// The type of a handle
    #[derive(Debug, PartialEq)]
    pub enum FidlHdlType {
        /// An invalid handle
        Invalid,
        /// A channel
        Channel,
        /// A socket
        Socket,
    }

    /// A borrowed reference to an underlying handle
    pub struct HandleRef<'a>(u32, std::marker::PhantomData<&'a u32>);

    /// A trait to get a reference to the underlying handle of an object.
    pub trait AsHandleRef {
        /// Get a reference to the handle.
        fn as_handle_ref(&self) -> HandleRef;

        /// Return true if this handle is invalid
        fn is_invalid(&self) -> bool {
            self.as_handle_ref().0 == INVALID_HANDLE
        }

        /// Interpret the reference as a raw handle (an integer type). Two distinct
        /// handles will have different raw values (so it can perhaps be used as a
        /// key in a data structure).
        fn raw_handle(&self) -> u32 {
            self.as_handle_ref().0
        }

        /// Non-fuchsia only: return the type of a handle
        fn handle_type(&self) -> FidlHdlType {
            if self.is_invalid() {
                FidlHdlType::Invalid
            } else {
                let (_, ty, _) = unpack_handle(self.as_handle_ref().0);
                match ty {
                    FidlHandleType::Channel => FidlHdlType::Channel,
                    FidlHandleType::StreamSocket => FidlHdlType::Socket,
                    FidlHandleType::DatagramSocket => FidlHdlType::Socket,
                }
            }
        }

        /// Non-fuchsia only: return a reference to the other end of a handle
        fn related(&self) -> HandleRef {
            if self.is_invalid() {
                HandleRef(INVALID_HANDLE, std::marker::PhantomData)
            } else {
                HandleRef(self.as_handle_ref().0 ^ 1, std::marker::PhantomData)
            }
        }

        /// Non-fuchsia only: return a "koid" like value
        fn emulated_koid_pair(&self) -> (u64, u64) {
            if self.is_invalid() {
                (0, 0)
            } else {
                let (slot, ty, side) = unpack_handle(self.as_handle_ref().0);
                let koid_left = match ty {
                    FidlHandleType::Channel => CHANNELS.lock()[slot].koid_left,
                    FidlHandleType::StreamSocket => STREAM_SOCKETS.lock()[slot].koid_left,
                    FidlHandleType::DatagramSocket => DATAGRAM_SOCKETS.lock()[slot].koid_left,
                };
                match side {
                    Side::Left => (koid_left, koid_left + 1),
                    Side::Right => (koid_left + 1, koid_left),
                }
            }
        }
    }

    impl AsHandleRef for HandleRef<'_> {
        fn as_handle_ref(&self) -> HandleRef {
            HandleRef(self.0, std::marker::PhantomData)
        }
    }

    /// A trait implemented by all handle-based types.
    pub trait HandleBased: AsHandleRef + From<Handle> + Into<Handle> {
        /// Creates an instance of this type from a handle.
        ///
        /// This is a convenience function which simply forwards to the `From` trait.
        fn from_handle(handle: Handle) -> Self {
            Self::from(handle)
        }

        /// Converts the value into its inner handle.
        ///
        /// This is a convenience function which simply forwards to the `Into` trait.
        fn into_handle(self) -> Handle {
            self.into()
        }
    }

    /// Representation of a handle-like object
    #[derive(PartialEq, Eq, Debug, Ord, PartialOrd, Hash)]
    pub struct Handle(u32);

    impl Drop for Handle {
        fn drop(&mut self) {
            hdl_close(self.0);
        }
    }

    impl AsHandleRef for Handle {
        fn as_handle_ref(&self) -> HandleRef {
            HandleRef(self.0, std::marker::PhantomData)
        }
    }

    impl HandleBased for Handle {}

    impl Handle {
        /// Return an invalid handle
        pub fn invalid() -> Handle {
            Handle(INVALID_HANDLE)
        }

        /// If a raw handle is obtained from some other source, this method converts
        /// it into a type-safe owned handle.
        pub unsafe fn from_raw(hdl: u32) -> Handle {
            Handle(hdl)
        }

        /// Take this handle and return a new handle (leaves this handle invalid)
        pub fn take(&mut self) -> Handle {
            let h = Handle(self.0);
            self.0 = INVALID_HANDLE;
            h
        }

        /// Take this handle and return a new raw handle (leaves this handle invalid)
        pub unsafe fn raw_take(&mut self) -> u32 {
            let h = self.0;
            self.0 = INVALID_HANDLE;
            h
        }
    }

    macro_rules! declare_unsupported_fidl_handle {
        ($name:ident) => {
            /// A Zircon-like $name
            #[derive(PartialEq, Eq, Debug, PartialOrd, Ord, Hash)]
            pub struct $name;

            impl From<$crate::handle::Handle> for $name {
                fn from(_: $crate::handle::Handle) -> $name {
                    $name
                }
            }
            impl From<$name> for Handle {
                fn from(_: $name) -> $crate::handle::Handle {
                    $crate::handle::Handle::invalid()
                }
            }
            impl HandleBased for $name {}
            impl AsHandleRef for $name {
                fn as_handle_ref(&self) -> HandleRef {
                    HandleRef(INVALID_HANDLE, std::marker::PhantomData)
                }
            }
        };
    }

    macro_rules! declare_fidl_handle {
        ($name:ident) => {
            /// A Zircon-like $name
            #[derive(PartialEq, Eq, Debug, PartialOrd, Ord, Hash)]
            pub struct $name(u32);

            impl From<$crate::handle::Handle> for $name {
                fn from(mut hdl: $crate::handle::Handle) -> $name {
                    let out = $name(hdl.0);
                    hdl.0 = INVALID_HANDLE;
                    out
                }
            }
            impl From<$name> for Handle {
                fn from(mut hdl: $name) -> $crate::handle::Handle {
                    let out = unsafe { $crate::handle::Handle::from_raw(hdl.0) };
                    hdl.0 = INVALID_HANDLE;
                    out
                }
            }
            impl HandleBased for $name {}
            impl AsHandleRef for $name {
                fn as_handle_ref(&self) -> HandleRef {
                    HandleRef(self.0, std::marker::PhantomData)
                }
            }

            impl Drop for $name {
                fn drop(&mut self) {
                    hdl_close(self.0);
                }
            }
        };
    }

    macro_rules! host_handle {
        ($x:tt, Everywhere) => {
            declare_fidl_handle! {$x}
        };
        ($x:tt, $availability:ident) => {
            declare_unsupported_fidl_handle! {$x}
        };
    }

    invoke_for_handle_types!(host_handle);

    impl Channel {
        /// Create a channel, resulting in a pair of `Channel` objects representing both
        /// sides of the channel. Messages written into one maybe read from the opposite.
        pub fn create() -> Result<(Channel, Channel), zx_status::Status> {
            let slot = new_handle_slot(&CHANNELS);
            let left = pack_handle(slot, FidlHandleType::Channel, Side::Left);
            let right = pack_handle(slot, FidlHandleType::Channel, Side::Right);
            Ok((Channel(left), Channel(right)))
        }

        /// Read a message from a channel.
        pub fn read(&self, buf: &mut MessageBuf) -> Result<(), zx_status::Status> {
            let (bytes, handles) = buf.split_mut();
            self.read_split(bytes, handles)
        }

        /// Read a message from a channel into a separate byte vector and handle vector.
        pub fn read_split(
            &self,
            bytes: &mut Vec<u8>,
            handles: &mut Vec<Handle>,
        ) -> Result<(), zx_status::Status> {
            match self.poll_read(&mut Context::from_waker(noop_waker_ref()), bytes, handles) {
                Poll::Ready(r) => r,
                Poll::Pending => Err(zx_status::Status::SHOULD_WAIT),
            }
        }

        fn poll_read(
            &self,
            cx: &mut Context<'_>,
            bytes: &mut Vec<u8>,
            handles: &mut Vec<Handle>,
        ) -> Poll<Result<(), zx_status::Status>> {
            let (slot, ty, side) = unpack_handle(self.0);
            assert_eq!(ty, FidlHandleType::Channel);
            let obj = &mut CHANNELS.lock()[slot];
            if let Some(mut msg) = obj.q.side_mut(side.opposite()).pop_front() {
                std::mem::swap(bytes, &mut msg.bytes);
                std::mem::swap(handles, &mut msg.handles);
                Poll::Ready(Ok(()))
            } else if obj.liveness.is_open() {
                *obj.wakers.side_mut(side.opposite()) = Some(cx.waker().clone());
                Poll::Pending
            } else {
                Poll::Ready(Err(zx_status::Status::PEER_CLOSED))
            }
        }

        /// Write a message to a channel.
        pub fn write(
            &self,
            bytes: &[u8],
            handles: &mut Vec<Handle>,
        ) -> Result<(), zx_status::Status> {
            let bytes = bytes.to_vec();
            let (slot, ty, side) = unpack_handle(self.0);
            assert_eq!(ty, FidlHandleType::Channel);
            let obj = &mut CHANNELS.lock()[slot];
            if !obj.liveness.is_open() {
                return Err(zx_status::Status::PEER_CLOSED);
            }
            obj.q.side_mut(side).push_back(ChannelMessage {
                bytes,
                handles: std::mem::replace(handles, Vec::new()),
            });
            obj.wakers.side_mut(side).take().map(|w| w.wake());
            Ok(())
        }
    }

    /// An I/O object representing a `Channel`.
    pub struct AsyncChannel {
        channel: Channel,
    }

    impl std::fmt::Debug for AsyncChannel {
        fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
            self.channel.fmt(f)
        }
    }
    impl AsyncChannel {
        /// Writes a message into the channel.
        pub fn write(
            &self,
            bytes: &[u8],
            handles: &mut Vec<Handle>,
        ) -> Result<(), zx_status::Status> {
            self.channel.write(bytes, handles)
        }

        /// Consumes self and returns the underlying Channel (named thusly for compatibility with
        /// fasync variant)
        pub fn into_zx_channel(self) -> Channel {
            self.channel
        }

        /// Receives a message on the channel and registers this `Channel` as
        /// needing a read on receiving a `io::std::ErrorKind::WouldBlock`.
        ///
        /// Identical to `recv_from` except takes separate bytes and handles buffers
        /// rather than a single `MessageBuf`.
        pub fn read(
            &self,
            cx: &mut Context<'_>,
            bytes: &mut Vec<u8>,
            handles: &mut Vec<Handle>,
        ) -> Poll<Result<(), zx_status::Status>> {
            self.channel.poll_read(cx, bytes, handles)
        }

        /// Receives a message on the channel and registers this `Channel` as
        /// needing a read on receiving a `io::std::ErrorKind::WouldBlock`.
        pub fn recv_from(
            &self,
            ctx: &mut Context<'_>,
            buf: &mut MessageBuf,
        ) -> Poll<Result<(), zx_status::Status>> {
            let (bytes, handles) = buf.split_mut();
            self.read(ctx, bytes, handles)
        }

        /// Creates a future that receive a message to be written to the buffer
        /// provided.
        ///
        /// The returned future will return after a message has been received on
        /// this socket and been placed into the buffer.
        pub fn recv_msg<'a>(&'a self, buf: &'a mut MessageBuf) -> RecvMsg<'a> {
            RecvMsg { channel: self, buf }
        }

        /// Creates a new `AsyncChannel` from a previously-created `Channel`.
        pub fn from_channel(channel: Channel) -> std::io::Result<AsyncChannel> {
            Ok(AsyncChannel { channel })
        }
    }

    /// A future used to receive a message from a channel.
    ///
    /// This is created by the `Channel::recv_msg` method.
    #[must_use = "futures do nothing unless polled"]
    pub struct RecvMsg<'a> {
        channel: &'a AsyncChannel,
        buf: &'a mut MessageBuf,
    }

    impl<'a> futures::Future for RecvMsg<'a> {
        type Output = Result<(), zx_status::Status>;

        fn poll(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
            let this = &mut *self;
            this.channel.recv_from(cx, this.buf)
        }
    }

    /// Socket options available portable
    #[derive(Clone, Copy)]
    pub enum SocketOpts {
        /// A bytestream style socket
        STREAM,
        /// A datagram style socket
        DATAGRAM,
    }

    impl Socket {
        /// Create a pair of sockets
        pub fn create(sock_opts: SocketOpts) -> Result<(Socket, Socket), zx_status::Status> {
            match sock_opts {
                SocketOpts::STREAM => {
                    let slot = new_handle_slot(&STREAM_SOCKETS);
                    let left = pack_handle(slot, FidlHandleType::StreamSocket, Side::Left);
                    let right = pack_handle(slot, FidlHandleType::StreamSocket, Side::Right);
                    Ok((Socket(left), Socket(right)))
                }
                SocketOpts::DATAGRAM => {
                    let slot = new_handle_slot(&DATAGRAM_SOCKETS);
                    let left = pack_handle(slot, FidlHandleType::DatagramSocket, Side::Left);
                    let right = pack_handle(slot, FidlHandleType::DatagramSocket, Side::Right);
                    Ok((Socket(left), Socket(right)))
                }
            }
        }

        /// Write the given bytes into the socket.
        /// Return value (on success) is number of bytes actually written.
        pub fn write(&self, bytes: &[u8]) -> Result<usize, zx_status::Status> {
            let (slot, ty, side) = unpack_handle(self.0);
            match ty {
                FidlHandleType::StreamSocket => {
                    let obj = &mut STREAM_SOCKETS.lock()[slot];
                    if !obj.liveness.is_open() {
                        return Err(zx_status::Status::PEER_CLOSED);
                    }
                    obj.q.side_mut(side).extend(bytes);
                    obj.wakers.side_mut(side).take().map(|w| w.wake());
                }
                FidlHandleType::DatagramSocket => {
                    let obj = &mut DATAGRAM_SOCKETS.lock()[slot];
                    if !obj.liveness.is_open() {
                        return Err(zx_status::Status::PEER_CLOSED);
                    }
                    obj.q.side_mut(side).push_back(bytes.to_vec());
                    obj.wakers.side_mut(side).take().map(|w| w.wake());
                }
                _ => panic!("Non socket passed to Socket::write"),
            }
            Ok(bytes.len())
        }

        /// Return how many bytes are buffered in the socket
        pub fn outstanding_read_bytes(&self) -> Result<usize, zx_status::Status> {
            let (slot, ty, side) = unpack_handle(self.0);
            let (len, open) = match ty {
                FidlHandleType::StreamSocket => {
                    let obj = &STREAM_SOCKETS.lock()[slot];
                    (obj.q.side(side.opposite()).len(), obj.liveness.is_open())
                }
                FidlHandleType::DatagramSocket => {
                    let obj = &DATAGRAM_SOCKETS.lock()[slot];
                    (
                        obj.q.side(side.opposite()).front().map(|frame| frame.len()).unwrap_or(0),
                        obj.liveness.is_open(),
                    )
                }
                _ => panic!("Non socket passed to Socket::outstanding_read_bytes"),
            };
            if len > 0 {
                return Ok(len);
            }
            if !open {
                return Err(zx_status::Status::PEER_CLOSED);
            }
            Ok(0)
        }

        fn poll_read(
            &self,
            bytes: &mut [u8],
            ctx: &mut Context<'_>,
        ) -> Poll<Result<usize, zx_status::Status>> {
            let (slot, ty, side) = unpack_handle(self.0);
            match ty {
                FidlHandleType::StreamSocket => {
                    let obj = &mut STREAM_SOCKETS.lock()[slot];
                    if bytes.is_empty() {
                        if obj.liveness.is_open() {
                            return Poll::Ready(Ok(0));
                        } else {
                            return Poll::Ready(Err(zx_status::Status::PEER_CLOSED));
                        }
                    }
                    let read = obj.q.side_mut(side.opposite());
                    let copy_bytes = std::cmp::min(bytes.len(), read.len());
                    if copy_bytes == 0 {
                        if obj.liveness.is_open() {
                            *obj.wakers.side_mut(side.opposite()) = Some(ctx.waker().clone());
                            return Poll::Pending;
                        } else {
                            return Poll::Ready(Err(zx_status::Status::PEER_CLOSED));
                        }
                    }
                    for (i, b) in read.drain(..copy_bytes).enumerate() {
                        bytes[i] = b;
                    }
                    Poll::Ready(Ok(copy_bytes))
                }
                FidlHandleType::DatagramSocket => {
                    let obj = &mut DATAGRAM_SOCKETS.lock()[slot];
                    if let Some(frame) = obj.q.side_mut(side.opposite()).pop_front() {
                        let n = std::cmp::min(bytes.len(), frame.len());
                        bytes[..n].clone_from_slice(&frame[..n]);
                        Poll::Ready(Ok(n))
                    } else if !obj.liveness.is_open() {
                        Poll::Ready(Err(zx_status::Status::PEER_CLOSED))
                    } else {
                        *obj.wakers.side_mut(side.opposite()) = Some(ctx.waker().clone());
                        Poll::Pending
                    }
                }
                _ => panic!("Non socket passed to Socket::read"),
            }
        }

        /// Read bytes from the socket.
        /// Return value (on success) is number of bytes actually read.
        pub fn read(&self, bytes: &mut [u8]) -> Result<usize, zx_status::Status> {
            match self.poll_read(bytes, &mut Context::from_waker(noop_waker_ref())) {
                Poll::Ready(r) => r,
                Poll::Pending => Err(zx_status::Status::SHOULD_WAIT),
            }
        }
    }

    /// An I/O object representing a `Socket`.
    pub struct AsyncSocket {
        socket: Socket,
    }

    impl std::fmt::Debug for AsyncSocket {
        fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
            self.socket.fmt(f)
        }
    }

    impl AsyncSocket {
        /// Construct an `AsyncSocket` from an existing `Socket`
        pub fn from_socket(socket: Socket) -> std::io::Result<AsyncSocket> {
            Ok(AsyncSocket { socket })
        }

        /// Convert AsyncSocket back into a regular socket
        pub fn into_zx_socket(self) -> Result<Socket, AsyncSocket> {
            Ok(self.socket)
        }

        /// Polls for the next data on the socket, appending it to the end of |out| if it has arrived.
        /// Not very useful for a non-datagram socket as it will return all available data
        /// on the socket.
        pub fn poll_datagram(
            &self,
            cx: &mut Context<'_>,
            out: &mut Vec<u8>,
        ) -> Poll<Result<usize, zx_status::Status>> {
            let avail = self.socket.outstanding_read_bytes()?;
            let len = out.len();
            out.resize(len + avail, 0);
            let (_, mut tail) = out.split_at_mut(len);
            match ready!(self.socket.poll_read(&mut tail, cx)) {
                Err(zx_status::Status::PEER_CLOSED) => Poll::Ready(Ok(0)),
                Err(e) => Poll::Ready(Err(e)),
                Ok(bytes) => {
                    if bytes == avail {
                        Poll::Ready(Ok(bytes))
                    } else {
                        Poll::Ready(Err(zx_status::Status::BAD_STATE))
                    }
                }
            }
        }

        /// Reads the next datagram that becomes available onto the end of |out|.  Note: Using this
        /// multiple times concurrently is an error and the first one will never complete.
        pub async fn read_datagram<'a>(
            &'a self,
            out: &'a mut Vec<u8>,
        ) -> Result<usize, zx_status::Status> {
            poll_fn(move |cx| self.poll_datagram(cx, out)).await
        }
    }

    impl futures::io::AsyncWrite for AsyncSocket {
        fn poll_write(
            self: Pin<&mut Self>,
            _cx: &mut std::task::Context<'_>,
            bytes: &[u8],
        ) -> Poll<Result<usize, std::io::Error>> {
            Poll::Ready(self.socket.write(bytes).map_err(|e| e.into()))
        }

        fn poll_flush(
            self: Pin<&mut Self>,
            _cx: &mut std::task::Context<'_>,
        ) -> Poll<Result<(), std::io::Error>> {
            Poll::Ready(Ok(()))
        }

        fn poll_close(
            mut self: Pin<&mut Self>,
            _cx: &mut std::task::Context<'_>,
        ) -> Poll<Result<(), std::io::Error>> {
            self.borrow_mut().socket = Socket::from_handle(Handle::invalid());
            Poll::Ready(Ok(()))
        }
    }

    impl futures::io::AsyncRead for AsyncSocket {
        fn poll_read(
            self: Pin<&mut Self>,
            cx: &mut std::task::Context<'_>,
            bytes: &mut [u8],
        ) -> Poll<Result<usize, std::io::Error>> {
            match ready!(self.socket.poll_read(bytes, cx)) {
                Err(zx_status::Status::PEER_CLOSED) => Poll::Ready(Ok(0)),
                Ok(x) => {
                    assert_ne!(x, 0);
                    Poll::Ready(Ok(x))
                }
                Err(x) => Poll::Ready(Err(x.into())),
            }
        }
    }

    /// A buffer for _receiving_ messages from a channel.
    ///
    /// A `MessageBuf` is essentially a byte buffer and a vector of
    /// handles, but move semantics for "taking" handles requires special handling.
    ///
    /// Note that for sending messages to a channel, the caller manages the buffers,
    /// using a plain byte slice and `Vec<Handle>`.
    #[derive(Debug, Default)]
    pub struct MessageBuf {
        bytes: Vec<u8>,
        handles: Vec<Handle>,
    }

    impl MessageBuf {
        /// Create a new, empty, message buffer.
        pub fn new() -> Self {
            Default::default()
        }

        /// Create a new non-empty message buffer.
        pub fn new_with(v: Vec<u8>, h: Vec<Handle>) -> Self {
            Self { bytes: v, handles: h }
        }

        /// Splits apart the message buf into a vector of bytes and a vector of handles.
        pub fn split_mut(&mut self) -> (&mut Vec<u8>, &mut Vec<Handle>) {
            (&mut self.bytes, &mut self.handles)
        }

        /// Splits apart the message buf into a vector of bytes and a vector of handles.
        pub fn split(self) -> (Vec<u8>, Vec<Handle>) {
            (self.bytes, self.handles)
        }

        /// Ensure that the buffer has the capacity to hold at least `n_bytes` bytes.
        pub fn ensure_capacity_bytes(&mut self, n_bytes: usize) {
            ensure_capacity(&mut self.bytes, n_bytes);
        }

        /// Ensure that the buffer has the capacity to hold at least `n_handles` handles.
        pub fn ensure_capacity_handles(&mut self, n_handles: usize) {
            ensure_capacity(&mut self.handles, n_handles);
        }

        /// Ensure that at least n_bytes bytes are initialized (0 fill).
        pub fn ensure_initialized_bytes(&mut self, n_bytes: usize) {
            if n_bytes <= self.bytes.len() {
                return;
            }
            self.bytes.resize(n_bytes, 0);
        }

        /// Get a reference to the bytes of the message buffer, as a `&[u8]` slice.
        pub fn bytes(&self) -> &[u8] {
            self.bytes.as_slice()
        }

        /// The number of handles in the message buffer. Note this counts the number
        /// available when the message was received; `take_handle` does not affect
        /// the count.
        pub fn n_handles(&self) -> usize {
            self.handles.len()
        }

        /// Take the handle at the specified index from the message buffer. If the
        /// method is called again with the same index, it will return `None`, as
        /// will happen if the index exceeds the number of handles available.
        pub fn take_handle(&mut self, index: usize) -> Option<Handle> {
            self.handles.get_mut(index).and_then(|handle| {
                if handle.is_invalid() {
                    None
                } else {
                    Some(std::mem::replace(handle, Handle::invalid()))
                }
            })
        }

        /// Clear the bytes and handles contained in the buf. This will drop any
        /// contained handles, resulting in their resources being freed.
        pub fn clear(&mut self) {
            self.bytes.clear();
            self.handles.clear();
        }
    }

    fn ensure_capacity<T>(vec: &mut Vec<T>, size: usize) {
        let len = vec.len();
        if size > len {
            vec.reserve(size - len);
        }
    }

    #[derive(Default)]
    struct Sided<T> {
        left: T,
        right: T,
    }

    impl<T> Sided<T> {
        fn side_mut(&mut self, side: Side) -> &mut T {
            match side {
                Side::Left => &mut self.left,
                Side::Right => &mut self.right,
            }
        }

        fn side(&self, side: Side) -> &T {
            match side {
                Side::Left => &self.left,
                Side::Right => &self.right,
            }
        }
    }

    struct ChannelMessage {
        bytes: Vec<u8>,
        handles: Vec<Handle>,
    }

    #[derive(Debug, Clone, Copy, PartialEq)]
    enum FidlHandleType {
        Channel,
        StreamSocket,
        DatagramSocket,
    }

    #[derive(Clone, Copy, Debug, PartialEq)]
    enum Side {
        Left,
        Right,
    }

    impl Side {
        fn opposite(self) -> Side {
            match self {
                Side::Left => Side::Right,
                Side::Right => Side::Left,
            }
        }
    }

    #[derive(Clone, Copy, Debug)]
    enum Liveness {
        Open,
        Left,
        Right,
    }

    impl Liveness {
        fn close(self, side: Side) -> Option<Liveness> {
            match (self, side) {
                (Liveness::Open, Side::Left) => Some(Liveness::Right),
                (Liveness::Open, Side::Right) => Some(Liveness::Left),
                (Liveness::Left, Side::Right) => unreachable!(),
                (Liveness::Right, Side::Left) => unreachable!(),
                (Liveness::Left, Side::Left) => None,
                (Liveness::Right, Side::Right) => None,
            }
        }

        fn is_open(self) -> bool {
            match self {
                Liveness::Open => true,
                _ => false,
            }
        }
    }

    type Wakers = Sided<Option<Waker>>;

    struct FidlHandle<T> {
        q: Sided<VecDeque<T>>,
        wakers: Wakers,
        liveness: Liveness,
        koid_left: u64,
    }

    type HandleTable<T> = Mutex<Slab<FidlHandle<T>>>;

    lazy_static::lazy_static! {
        static ref CHANNELS: HandleTable<ChannelMessage> = Mutex::new(Slab::new());
        static ref STREAM_SOCKETS: HandleTable<u8> = Mutex::new(Slab::new());
        static ref DATAGRAM_SOCKETS: HandleTable<Vec<u8>> = Mutex::new(Slab::new());
    }

    static NEXT_KOID: AtomicU64 = AtomicU64::new(1);

    fn new_handle_slot<T>(tbl: &HandleTable<T>) -> usize {
        let mut h = tbl.lock();
        h.insert(FidlHandle {
            q: Default::default(),
            wakers: Default::default(),
            liveness: Liveness::Open,
            koid_left: NEXT_KOID.fetch_add(2, Ordering::Relaxed),
        })
    }

    fn pack_handle(slot: usize, ty: FidlHandleType, side: Side) -> u32 {
        let side_bit = match side {
            Side::Left => 0,
            Side::Right => 1,
        };
        let ty_bits = match ty {
            FidlHandleType::Channel => 0,
            FidlHandleType::StreamSocket => 1,
            FidlHandleType::DatagramSocket => 2,
        };
        let slot_bits = slot as u32;
        (slot_bits << 3) | (ty_bits << 1) | side_bit
    }

    fn unpack_handle(handle: u32) -> (usize, FidlHandleType, Side) {
        let side = if handle & 1 == 0 { Side::Left } else { Side::Right };
        let ty = match (handle >> 1) & 0x3 {
            0 => FidlHandleType::Channel,
            1 => FidlHandleType::StreamSocket,
            2 => FidlHandleType::DatagramSocket,
            x => panic!("Bad handle type {}", x),
        };
        let slot = (handle >> 3) as usize;
        (slot, ty, side)
    }

    fn close_in_table<T>(tbl: &HandleTable<T>, slot: usize, side: Side) {
        let mut tbl = tbl.lock();
        let h = &mut tbl[slot];
        match h.liveness.close(side) {
            None => {
                tbl.remove(slot);
            }
            Some(liveness) => {
                h.liveness = liveness;
                h.wakers.side_mut(side).take().map(|w| w.wake());
            }
        }
    }

    /// Close the handle: no action if hdl==INVALID_HANDLE
    fn hdl_close(hdl: u32) {
        if hdl == INVALID_HANDLE {
            return;
        }
        let (slot, ty, side) = unpack_handle(hdl);
        match ty {
            FidlHandleType::Channel => close_in_table(&CHANNELS, slot, side),
            FidlHandleType::StreamSocket => close_in_table(&STREAM_SOCKETS, slot, side),
            FidlHandleType::DatagramSocket => close_in_table(&DATAGRAM_SOCKETS, slot, side),
        }
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use fuchsia_zircon_status as zx_status;
    use futures::io::{AsyncReadExt, AsyncWriteExt};
    use futures::task::{noop_waker, Context};
    use futures::Future;
    use std::pin::Pin;
    use zx_status::Status;

    #[cfg(target_os = "fuchsia")]
    use fuchsia_async as fasync;

    #[cfg(not(target_os = "fuchsia"))]
    use futures::executor::block_on;

    #[cfg(target_os = "fuchsia")]
    fn run_async_test(f: impl Future<Output = ()>) {
        fasync::Executor::new().unwrap().run_singlethreaded(f);
    }

    #[cfg(not(target_os = "fuchsia"))]
    fn run_async_test(f: impl Future<Output = ()>) {
        block_on(f);
    }

    #[test]
    fn channel_write_read() {
        let (a, b) = Channel::create().unwrap();
        let (c, d) = Channel::create().unwrap();
        let mut incoming = MessageBuf::new();

        assert_eq!(b.read(&mut incoming).err().unwrap(), Status::SHOULD_WAIT);
        d.write(&[4, 5, 6], &mut vec![]).unwrap();
        a.write(&[1, 2, 3], &mut vec![c.into(), d.into()]).unwrap();

        b.read(&mut incoming).unwrap();
        assert_eq!(incoming.bytes(), &[1, 2, 3]);
        assert_eq!(incoming.n_handles(), 2);
        let c: Channel = incoming.take_handle(0).unwrap().into();
        let d: Channel = incoming.take_handle(1).unwrap().into();
        c.read(&mut incoming).unwrap();
        drop(d);
        assert_eq!(incoming.bytes(), &[4, 5, 6]);
        assert_eq!(incoming.n_handles(), 0);
    }

    #[test]
    fn socket_write_read() {
        let (a, b) = Socket::create(SocketOpts::STREAM).unwrap();
        a.write(&[1, 2, 3]).unwrap();
        let mut buf = [0u8; 128];
        assert_eq!(b.read(&mut buf).unwrap(), 3);
        assert_eq!(&buf[0..3], &[1, 2, 3]);
    }

    #[test]
    fn async_channel_write_read() {
        run_async_test(async move {
            let (a, b) = Channel::create().unwrap();
            let (a, b) =
                (AsyncChannel::from_channel(a).unwrap(), AsyncChannel::from_channel(b).unwrap());
            let mut buf = MessageBuf::new();

            let waker = noop_waker();
            let mut cx = Context::from_waker(&waker);

            let mut rx = b.recv_msg(&mut buf);
            assert_eq!(Pin::new(&mut rx).poll(&mut cx), std::task::Poll::Pending);
            a.write(&[1, 2, 3], &mut vec![]).unwrap();
            rx.await.unwrap();
            assert_eq!(buf.bytes(), &[1, 2, 3]);

            let mut rx = a.recv_msg(&mut buf);
            assert!(Pin::new(&mut rx).poll(&mut cx).is_pending());
            b.write(&[1, 2, 3], &mut vec![]).unwrap();
            rx.await.unwrap();
            assert_eq!(buf.bytes(), &[1, 2, 3]);
        })
    }

    #[test]
    fn async_socket_write_read() {
        run_async_test(async move {
            let (a, b) = Socket::create(SocketOpts::STREAM).unwrap();
            let (mut a, mut b) =
                (AsyncSocket::from_socket(a).unwrap(), AsyncSocket::from_socket(b).unwrap());
            let mut buf = [0u8; 128];

            let waker = noop_waker();
            let mut cx = Context::from_waker(&waker);

            let mut rx = b.read(&mut buf);
            assert!(Pin::new(&mut rx).poll(&mut cx).is_pending());
            assert!(Pin::new(&mut a.write(&[1, 2, 3])).poll(&mut cx).is_ready());
            rx.await.unwrap();
            assert_eq!(&buf[0..3], &[1, 2, 3]);

            let mut rx = a.read(&mut buf);
            assert!(Pin::new(&mut rx).poll(&mut cx).is_pending());
            assert!(Pin::new(&mut b.write(&[1, 2, 3])).poll(&mut cx).is_ready());
            rx.await.unwrap();
            assert_eq!(&buf[0..3], &[1, 2, 3]);
        })
    }

    #[test]
    fn channel_basic() {
        let (p1, p2) = Channel::create().unwrap();

        let mut empty = vec![];
        assert!(p1.write(b"hello", &mut empty).is_ok());

        let mut buf = MessageBuf::new();
        assert!(p2.read(&mut buf).is_ok());
        assert_eq!(buf.bytes(), b"hello");
    }

    #[test]
    fn channel_send_handle() {
        let hello_length: usize = 5;

        // Create a pair of channels and a pair of sockets.
        let (p1, p2) = Channel::create().unwrap();
        let (s1, s2) = Socket::create(SocketOpts::STREAM).unwrap();

        // Send one socket down the channel
        let mut handles_to_send: Vec<Handle> = vec![s1.into_handle()];
        assert!(p1.write(b"", &mut handles_to_send).is_ok());
        // Handle should be removed from vector.
        assert!(handles_to_send.is_empty());

        // Read the handle from the receiving channel.
        let mut buf = MessageBuf::new();
        assert!(p2.read(&mut buf).is_ok());
        assert_eq!(buf.n_handles(), 1);
        // Take the handle from the buffer.
        let received_handle = buf.take_handle(0).unwrap();
        // Should not affect number of handles.
        assert_eq!(buf.n_handles(), 1);
        // Trying to take it again should fail.
        assert!(buf.take_handle(0).is_none());

        // Now to test that we got the right handle, try writing something to it...
        let received_socket = Socket::from(received_handle);
        assert!(received_socket.write(b"hello").is_ok());

        // ... and reading it back from the original VMO.
        let mut read_vec = vec![0; hello_length];
        assert!(s2.read(&mut read_vec).is_ok());
        assert_eq!(read_vec, b"hello");
    }

    #[test]
    fn socket_basic() {
        let (s1, s2) = Socket::create(SocketOpts::STREAM).unwrap();

        // Write two packets and read from other end
        assert_eq!(s1.write(b"hello").unwrap(), 5);
        assert_eq!(s1.write(b"world").unwrap(), 5);

        let mut read_vec = vec![0; 11];
        assert_eq!(s2.read(&mut read_vec).unwrap(), 10);
        assert_eq!(&read_vec[0..10], b"helloworld");

        // Try reading when there is nothing to read.
        assert_eq!(s2.read(&mut read_vec), Err(Status::SHOULD_WAIT));
    }

    #[cfg(not(target_os = "fuchsia"))]
    #[test]
    fn handle_type_is_correct() {
        let (c1, c2) = Channel::create().unwrap();
        let (s1, s2) = Socket::create(SocketOpts::STREAM).unwrap();
        assert_eq!(c1.into_handle().handle_type(), FidlHdlType::Channel);
        assert_eq!(c2.into_handle().handle_type(), FidlHdlType::Channel);
        assert_eq!(s1.into_handle().handle_type(), FidlHdlType::Socket);
        assert_eq!(s2.into_handle().handle_type(), FidlHdlType::Socket);
    }
}

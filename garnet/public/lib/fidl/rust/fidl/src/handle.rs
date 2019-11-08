// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A portable representation of handle-like objects for fidl.
//! Since this code needs implementation of the fidlhdl_* groups of functions, tests for this
//! module exist in th src/connectivity/overnet/lib/hoist/src/fidlhdl.rs module (which provides
//! them).

/// Fuchsia implementation of handles just aliases the zircon library
#[cfg(target_os = "fuchsia")]
pub mod fuchsia_handles {

    use fuchsia_async as fasync;
    use fuchsia_zircon as zx;

    pub use zx::AsHandleRef;
    pub use zx::Handle;
    pub use zx::HandleBased;
    pub use zx::HandleRef;
    pub use zx::MessageBuf;

    pub use zx::Channel;
    pub use zx::DebugLog;
    pub use zx::Event;
    pub use zx::EventPair;
    pub use zx::Fifo;
    pub use zx::Interrupt;
    pub use zx::Job;
    pub use zx::Port;
    pub use zx::Process;
    pub use zx::Resource;
    pub use zx::Socket;
    pub use zx::Thread;
    pub use zx::Timer;
    pub use zx::Vmar;
    pub use zx::Vmo;

    pub use fasync::Channel as AsyncChannel;
    pub use fasync::Socket as AsyncSocket;

    pub use zx::SocketOpts;
}

/// Non-Fuchsia implementation of handles
#[cfg(not(target_os = "fuchsia"))]
pub mod non_fuchsia_handles {

    use fuchsia_zircon_status as zx_status;
    use futures::task::{AtomicWaker, Context};
    use parking_lot::Mutex;
    use std::{borrow::BorrowMut, pin::Pin, sync::Arc, task::Poll};

    /// Invalid handle value
    pub const INVALID_HANDLE: u32 = 0xffff_ffff;

    /// Read result for IO operations
    #[repr(C)]
    pub enum FidlHdlReadResult {
        /// Success!
        Ok,
        /// Nothing read
        Pending,
        /// Peer handle is closed
        PeerClosed,
        /// Not enough buffer space
        BufferTooSmall,
    }

    /// Write result for IO operations
    #[repr(C)]
    pub enum FidlHdlWriteResult {
        /// Success!
        Ok,
        /// Peer handle is closed
        PeerClosed,
    }

    /// Return type for fidlhdl_channel_create
    // Upon success, left=first handle, right=second handle
    // Upon failure, left=INVALID_HANDLE, right=reason
    #[repr(C)]
    pub struct FidlHdlPairCreateResult {
        left: u32,
        right: u32,
    }

    impl FidlHdlPairCreateResult {
        unsafe fn into_handles<T: HandleBased>(&self) -> Result<(T, T), zx_status::Status> {
            match self.left {
                INVALID_HANDLE => Err(zx_status::Status::from_raw(self.right as i32)),
                _ => Ok((
                    T::from_handle(Handle::from_raw(self.left)),
                    T::from_handle(Handle::from_raw(self.right)),
                )),
            }
        }

        /// Create a new (successful) result
        pub fn new(left: u32, right: u32) -> Self {
            assert_ne!(left, INVALID_HANDLE);
            assert_ne!(right, INVALID_HANDLE);
            Self { left, right }
        }

        /// Create a new (failed) result
        pub fn new_err(status: zx_status::Status) -> Self {
            Self { left: INVALID_HANDLE, right: status.into_raw() as u32 }
        }
    }

    /// The type of a handle
    #[repr(C)]
    pub enum FidlHdlType {
        /// An invalid handle
        Invalid,
        /// A channel
        Channel,
        /// A socket
        Socket,
    }

    /// Non-Fuchsia implementation of handles
    extern "C" {
        /// Close the handle: no action if hdl==INVALID_HANDLE
        fn fidlhdl_close(hdl: u32);
        /// Return the type of a handle
        fn fidlhdl_type(hdl: u32) -> FidlHdlType;
        /// Create a channel pair
        fn fidlhdl_channel_create() -> FidlHdlPairCreateResult;
        /// Read from a channel - takes ownership of all handles
        fn fidlhdl_channel_read(
            hdl: u32,
            bytes: *mut u8,
            handles: *mut u32,
            num_bytes: usize,
            num_handles: usize,
            actual_bytes: *mut usize,
            actual_handles: *mut usize,
        ) -> FidlHdlReadResult;
        /// Write to a channel
        fn fidlhdl_channel_write(
            hdl: u32,
            bytes: *const u8,
            handles: *mut Handle,
            num_bytes: usize,
            num_handles: usize,
        ) -> FidlHdlWriteResult;
        /// Write to a socket
        fn fidlhdl_socket_write(hdl: u32, bytes: *const u8, num_bytes: usize)
            -> FidlHdlWriteResult;
        /// Read from a socket
        fn fidlhdl_socket_read(
            hdl: u32,
            bytes: *const u8,
            num_bytes: usize,
            actual_bytes: *mut usize,
        ) -> FidlHdlReadResult;
        /// Signal that a read is required
        fn fidlhdl_need_read(hdl: u32);
        /// Create a socket pair
        fn fidlhdl_socket_create(sock_opts: SocketOpts) -> FidlHdlPairCreateResult;
    }

    #[derive(Debug)]
    struct HdlWaker {
        hdl: u32,
        waker: AtomicWaker,
    }

    impl HdlWaker {
        fn sched(&self, cx: &mut Context<'_>) {
            self.waker.register(cx.waker());
            unsafe {
                fidlhdl_need_read(self.hdl);
            }
        }
    }

    lazy_static::lazy_static! {
        static ref HANDLE_WAKEUPS: Mutex<Vec<Arc<HdlWaker>>> = Mutex::new(Vec::new());
    }

    /// Awaken a handle by index.
    ///
    /// Wakeup flow:
    ///   There are no wakeups issued unless fidlhdl_need_read is called.
    ///   fidlhdl_need_read arms the wakeup, and no wakeups will occur until that call is made.
    pub fn awaken_hdl(hdl: u32) {
        HANDLE_WAKEUPS.lock()[hdl as usize].waker.wake();
    }

    fn get_or_create_arc_waker(hdl: u32) -> Arc<HdlWaker> {
        assert_ne!(hdl, INVALID_HANDLE);
        let mut wakers = HANDLE_WAKEUPS.lock();
        while wakers.len() <= (hdl as usize) {
            let index = wakers.len();
            wakers.push(Arc::new(HdlWaker { hdl: index as u32, waker: AtomicWaker::new() }));
        }
        wakers[hdl as usize].clone()
    }

    /// A borrowed reference to an underlying handle
    pub struct HandleRef(u32);

    /// A trait to get a reference to the underlying handle of an object.
    pub trait AsHandleRef {
        /// Get a reference to the handle.
        fn as_handle_ref(&self) -> HandleRef;
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
    #[repr(C)]
    #[derive(PartialEq, Eq, Debug, Ord, PartialOrd, Hash)]
    pub struct Handle(u32);

    impl Drop for Handle {
        fn drop(&mut self) {
            unsafe {
                fidlhdl_close(self.0);
            }
        }
    }

    impl AsHandleRef for Handle {
        fn as_handle_ref(&self) -> HandleRef {
            HandleRef(self.0)
        }
    }

    impl HandleBased for Handle {}

    impl Handle {
        /// Non-fuchsia only: return the type of a handle
        pub fn handle_type(&self) -> FidlHdlType {
            if self.is_invalid() {
                FidlHdlType::Invalid
            } else {
                unsafe { fidlhdl_type(self.0) }
            }
        }

        /// Return an invalid handle
        pub fn invalid() -> Handle {
            Handle(INVALID_HANDLE)
        }

        /// Return true if this handle is invalid
        pub fn is_invalid(&self) -> bool {
            self.0 == INVALID_HANDLE
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
                    HandleRef(INVALID_HANDLE)
                }
            }
        };
    }

    declare_unsupported_fidl_handle!(DebugLog);
    declare_unsupported_fidl_handle!(Event);
    declare_unsupported_fidl_handle!(EventPair);
    declare_unsupported_fidl_handle!(Vmo);

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
                    HandleRef(self.0)
                }
            }

            impl Drop for $name {
                fn drop(&mut self) {
                    unsafe { fidlhdl_close(self.0) };
                }
            }
        };
    }

    declare_fidl_handle!(Channel);

    impl Channel {
        /// Create a channel, resulting in a pair of `Channel` objects representing both
        /// sides of the channel. Messages written into one maybe read from the opposite.
        pub fn create() -> Result<(Channel, Channel), zx_status::Status> {
            unsafe { fidlhdl_channel_create().into_handles() }
        }

        /// Read a message from a channel.
        pub fn read(&self, buf: &mut MessageBuf) -> Result<(), zx_status::Status> {
            let (bytes, handles) = buf.split_mut();
            self.read_split(bytes, handles)
        }

        /// Read a message from a channel into a separate byte vector and handle vector.
        ///
        /// Note that this method can cause internal reallocations in the `MessageBuf`
        /// if it is lacks capacity to hold the full message. If such reallocations
        /// are not desirable, use `read_raw` instead.
        pub fn read_split(
            &self,
            bytes: &mut Vec<u8>,
            handles: &mut Vec<Handle>,
        ) -> Result<(), zx_status::Status> {
            loop {
                match self.read_raw(bytes, handles) {
                    Ok(result) => return result,
                    Err((num_bytes, num_handles)) => {
                        ensure_capacity(bytes, num_bytes);
                        ensure_capacity(handles, num_handles);
                    }
                }
            }
        }

        /// Read a message from a channel. Wraps the
        /// [zx_channel_read](https://fuchsia.googlesource.com/fuchsia/+/master/zircon/docs/syscalls/channel_read.md)
        /// syscall.
        ///
        /// If the vectors lack the capacity to hold the pending message,
        /// returns an `Err` with the number of bytes and number of handles needed.
        /// Otherwise returns an `Ok` with the result as usual.
        pub fn read_raw(
            &self,
            bytes: &mut Vec<u8>,
            handles: &mut Vec<Handle>,
        ) -> Result<Result<(), zx_status::Status>, (usize, usize)> {
            unsafe {
                bytes.clear();
                handles.clear();
                let mut num_bytes = bytes.capacity();
                let mut num_handles = handles.capacity();
                match fidlhdl_channel_read(
                    self.0,
                    bytes.as_mut_ptr(),
                    handles.as_mut_ptr() as *mut u32,
                    num_bytes,
                    num_handles,
                    &mut num_bytes,
                    &mut num_handles,
                ) {
                    FidlHdlReadResult::Ok => {
                        bytes.set_len(num_bytes as usize);
                        handles.set_len(num_handles as usize);
                        Ok(Ok(()))
                    }
                    FidlHdlReadResult::Pending => Ok(Err(zx_status::Status::SHOULD_WAIT)),
                    FidlHdlReadResult::PeerClosed => Ok(Err(zx_status::Status::PEER_CLOSED)),
                    FidlHdlReadResult::BufferTooSmall => {
                        Err((num_bytes as usize, num_handles as usize))
                    }
                }
            }
        }

        /// Write a message to a channel.
        pub fn write(
            &self,
            bytes: &[u8],
            handles: &mut Vec<Handle>,
        ) -> Result<(), zx_status::Status> {
            match unsafe {
                fidlhdl_channel_write(
                    self.0,
                    bytes.as_ptr(),
                    handles.as_mut_ptr(),
                    bytes.len(),
                    handles.len(),
                )
            } {
                FidlHdlWriteResult::Ok => Ok(()),
                FidlHdlWriteResult::PeerClosed => Err(zx_status::Status::PEER_CLOSED),
            }
        }
    }

    /// An I/O object representing a `Channel`.
    #[derive(Debug)]
    pub struct AsyncChannel {
        channel: Channel,
        waker: Arc<HdlWaker>,
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
            match self.channel.read_split(bytes, handles) {
                Err(zx_status::Status::SHOULD_WAIT) => {
                    self.waker.sched(cx);
                    Poll::Pending
                }
                x => Poll::Ready(x),
            }
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
            Ok(AsyncChannel { waker: get_or_create_arc_waker(channel.0), channel })
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
    #[repr(C)]
    pub enum SocketOpts {
        /// A bytestream style socket
        STREAM,
    }

    declare_fidl_handle!(Socket);

    impl Socket {
        /// Create a pair of sockets
        pub fn create(sock_opts: SocketOpts) -> Result<(Socket, Socket), zx_status::Status> {
            unsafe { fidlhdl_socket_create(sock_opts).into_handles() }
        }

        /// Write the given bytes into the socket.
        /// Return value (on success) is number of bytes actually written.
        pub fn write(&self, bytes: &[u8]) -> Result<usize, zx_status::Status> {
            match unsafe { fidlhdl_socket_write(self.0, bytes.as_ptr(), bytes.len()) } {
                FidlHdlWriteResult::Ok => Ok(bytes.len()),
                FidlHdlWriteResult::PeerClosed => Err(zx_status::Status::PEER_CLOSED),
            }
        }

        /// Read bytes from the socket.
        /// Return value (on success) is number of bytes actually read.
        pub fn read(&self, bytes: &mut [u8]) -> Result<usize, zx_status::Status> {
            let mut actual_len: usize = 0;
            match unsafe {
                fidlhdl_socket_read(self.0, bytes.as_ptr(), bytes.len(), &mut actual_len)
            } {
                FidlHdlReadResult::Ok => Ok(actual_len),
                FidlHdlReadResult::Pending => Err(zx_status::Status::SHOULD_WAIT),
                FidlHdlReadResult::PeerClosed => Err(zx_status::Status::PEER_CLOSED),
                FidlHdlReadResult::BufferTooSmall => unimplemented!(),
            }
        }
    }

    /// An I/O object representing a `Socket`.
    #[derive(Debug)]
    pub struct AsyncSocket {
        socket: Socket,
        waker: Arc<HdlWaker>,
    }

    impl AsyncSocket {
        /// Construct an `AsyncSocket` from an existing `Socket`
        pub fn from_socket(socket: Socket) -> std::io::Result<AsyncSocket> {
            Ok(AsyncSocket { waker: get_or_create_arc_waker(socket.0), socket })
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
            match self.socket.read(bytes) {
                Err(zx_status::Status::SHOULD_WAIT) => {
                    self.waker.sched(cx);
                    Poll::Pending
                }
                Ok(x) => Poll::Ready(Ok(x)),
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
}

#[cfg(target_os = "fuchsia")]
pub use fuchsia_handles::*;

#[cfg(not(target_os = "fuchsia"))]
pub use non_fuchsia_handles::*;

#[cfg(all(test, not(target_os = "fuchsia")))]
#[no_mangle]
pub extern "C" fn fidlhdl_close(_hdl: u32) {}

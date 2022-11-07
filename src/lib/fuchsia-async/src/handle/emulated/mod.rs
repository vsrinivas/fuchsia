// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Emulation of Zircon handles for non-Zircon based operating systems.

pub mod channel;
pub mod socket;

use crate::invoke_for_handle_types;
use bitflags::bitflags;
use fuchsia_zircon_status as zx_status;
use futures::ready;
use futures::task::noop_waker_ref;
use slab::Slab;
#[cfg(debug_assertions)]
use std::cell::Cell;
use std::sync::Mutex;
use std::{
    collections::VecDeque,
    future::Future,
    marker::PhantomData,
    pin::Pin,
    sync::atomic::{AtomicU64, AtomicUsize, Ordering},
    task::{Context, Poll, Waker},
};
use zx_status::Status;

/// Invalid handle value
const INVALID_HANDLE: u32 = 0;

/// The type of an object.
#[derive(Debug, Eq, PartialEq, Ord, PartialOrd, Hash, Copy, Clone)]
pub struct ObjectType(u32);

macro_rules! define_object_type_constant {
    ($x:tt, $docname:expr, $name:ident, $value:expr, $availability:ident) => {
        #[doc = $docname]
        pub const $name: ObjectType = ObjectType($value);
    };
}

impl ObjectType {
    /// No object.
    pub const NONE: ObjectType = ObjectType(0);
    invoke_for_handle_types!(define_object_type_constant);

    /// Converts ObjectType into the underlying zircon type.
    pub fn into_raw(self) -> u32 {
        self.0
    }
}

/// A borrowed reference to an underlying handle
#[derive(Debug, Clone, Copy, Eq, PartialEq, Ord, PartialOrd, Hash)]
pub struct HandleRef<'a>(u32, std::marker::PhantomData<&'a u32>);

impl<'a> HandleRef<'a> {
    /// Returns an invalid handle ref
    pub fn invalid() -> Self {
        Self(INVALID_HANDLE, std::marker::PhantomData)
    }

    /// Signal an object
    pub fn signal(&self, clear_mask: Signals, set_mask: Signals) -> Result<(), Status> {
        if self.is_invalid() {
            return Err(zx_status::Status::BAD_HANDLE);
        }

        clear_mask.validate_user_signals()?;
        set_mask.validate_user_signals()?;

        let rights = with_handle(self.raw_handle(), |mut h, side| h.as_hdl_data().rights(side));

        if !rights.contains(Rights::SIGNAL) {
            Err(Status::ACCESS_DENIED)
        } else {
            with_handle(self.0, |mut h, side| {
                // We just checked for an invalid handle above, so this should never fail.
                h.as_hdl_data()
                    .signal(side, clear_mask, set_mask)
                    .status_for_peer()
                    .expect("Handle became invalid while processing signal call");
            });

            Ok(())
        }
    }
}

#[derive(Debug, Eq, PartialEq)]
#[repr(transparent)]
pub struct Koid(u64);

#[derive(Debug, Eq, PartialEq)]
pub struct HandleBasicInfo {
    pub koid: Koid,
    pub rights: Rights,
    pub object_type: ObjectType,
    pub related_koid: Koid,
    pub reserved: u32,
}

/// A trait to get a reference to the underlying handle of an object.
pub trait AsHandleRef {
    /// Get a reference to the handle.
    fn as_handle_ref<'a>(&'a self) -> HandleRef<'a>;

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

    /// Set and clear userspace-accessible signal bits on an object.
    fn signal_handle(&self, clear_mask: Signals, set_mask: Signals) -> Result<(), Status> {
        self.as_handle_ref().signal(clear_mask, set_mask)
    }

    /// Wraps the
    /// [zx_object_get_info](https://fuchsia.dev/fuchsia-src/reference/syscalls/object_get_info.md)
    /// syscall for the ZX_INFO_HANDLE_BASIC topic.
    fn basic_info(&self) -> Result<HandleBasicInfo, zx_status::Status> {
        if self.is_invalid() {
            return Err(zx_status::Status::BAD_HANDLE);
        }
        let (koids, rights) = with_handle(self.raw_handle(), |mut h, side| {
            let h = h.as_hdl_data();
            (h.koids(side), h.rights(side))
        });
        let (_, _, ty, _) = unpack_handle(self.raw_handle());
        Ok(HandleBasicInfo {
            koid: Koid(koids.0),
            rights,
            object_type: ty.object_type(),
            related_koid: Koid(koids.1),
            reserved: 0,
        })
    }
}

/// A trait implemented by all handles for objects which have a peer.
pub trait Peered: HandleBased {
    /// Set and clear userspace-accessible signal bits on the object's peer. Wraps the
    /// [zx_object_signal_peer](https://fuchsia.dev/fuchsia-src/reference/syscalls/object_signal.md)
    /// syscall.
    fn signal_peer(&self, clear_mask: Signals, set_mask: Signals) -> Result<(), Status> {
        if self.is_invalid() {
            return Err(zx_status::Status::BAD_HANDLE);
        }

        clear_mask.validate_user_signals()?;
        set_mask.validate_user_signals()?;

        let rights = with_handle(self.raw_handle(), |mut h, side| h.as_hdl_data().rights(side));

        if !rights.contains(Rights::SIGNAL_PEER) {
            Err(Status::ACCESS_DENIED)
        } else {
            with_handle(self.raw_handle(), |mut h, side| {
                h.as_hdl_data().signal(side.opposite(), clear_mask, set_mask).status_for_peer()
            })
        }
    }
}

/// An extension of `AsHandleRef` that adds non-Fuchsia-only operations.
pub trait EmulatedHandleRef: AsHandleRef {
    /// Return the type of a handle.
    fn object_type(&self) -> ObjectType {
        if self.is_invalid() {
            ObjectType::NONE
        } else {
            let (_, _, ty, _) = unpack_handle(self.as_handle_ref().0);
            ty.object_type()
        }
    }

    /// Return a reference to the other end of a handle.
    fn related<'a>(&'a self) -> HandleRef<'a> {
        if self.is_invalid() {
            HandleRef(INVALID_HANDLE, std::marker::PhantomData)
        } else {
            HandleRef(self.as_handle_ref().0 ^ 1, std::marker::PhantomData)
        }
    }

    /// Return a "koid" like value.
    fn koid_pair(&self) -> (u64, u64) {
        if self.is_invalid() {
            (0, 0)
        } else {
            with_handle(self.as_handle_ref().0, |mut h, side| h.as_hdl_data().koids(side))
        }
    }

    /// Return true if the handle appears valid (i.e. not `Handle::invalid()`),
    /// but does not exist in the table. This should not normally return true;
    /// when it does, dropping the handle will cause a panic.
    fn is_dangling(&self) -> bool {
        if self.is_invalid() {
            false
        } else {
            let (shard, slot, ty, side) = unpack_handle(self.as_handle_ref().0);
            let present = match ty {
                HdlType::Channel => table_contains(&CHANNELS, shard, slot, side),
                HdlType::StreamSocket => table_contains(&STREAM_SOCKETS, shard, slot, side),
                HdlType::DatagramSocket => table_contains(&DATAGRAM_SOCKETS, shard, slot, side),
                HdlType::EventPair => table_contains(&EVENT_PAIRS, shard, slot, side),
            };
            !present
        }
    }
}

impl<T: AsHandleRef> EmulatedHandleRef for T {}

impl AsHandleRef for HandleRef<'_> {
    fn as_handle_ref<'a>(&'a self) -> HandleRef<'a> {
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
    fn as_handle_ref<'a>(&'a self) -> HandleRef<'a> {
        HandleRef(self.0, std::marker::PhantomData)
    }
}

impl HandleBased for Handle {}

impl Handle {
    /// Return an invalid handle
    #[inline(always)]
    pub const fn invalid() -> Handle {
        Handle(INVALID_HANDLE)
    }

    /// Return true if this handle is invalid
    pub fn is_invalid(&self) -> bool {
        self.0 == INVALID_HANDLE
    }

    /// If a raw handle is obtained from some other source, this method converts
    /// it into a type-safe owned handle.
    ///
    /// # Safety
    ///
    /// `hdl` must be a valid raw handle. The returned `Handle` takes ownership
    /// of the raw handle, so it must not be modified or closed by other means
    /// after calling `from_raw`.
    pub unsafe fn from_raw(hdl: u32) -> Handle {
        Handle(hdl)
    }

    /// Take this handle and return a new handle (leaves this handle invalid)
    pub fn take(&mut self) -> Handle {
        let h = Handle(self.0);
        self.0 = INVALID_HANDLE;
        h
    }

    /// Invalidates this handle and returns its raw handle.
    ///
    /// To avoid leaking resources, the returned raw handle should eventually be
    /// closed. This can be done by converting it back to a [`Handle`] with
    /// [`from_raw`] and dropping it.
    ///
    /// [`from_raw`]: Handle::from_raw
    pub fn raw_take(&mut self) -> u32 {
        let h = self.0;
        self.0 = INVALID_HANDLE;
        h
    }

    /// Create a replacement for a handle, possibly reducing the rights available. This invalidates
    /// the original handle. Wraps the
    /// [zx_handle_replace](https://fuchsia.dev/fuchsia-src/reference/syscalls/handle_replace.md)
    /// syscall.
    pub fn replace(self, target_rights: Rights) -> Result<Handle, zx_status::Status> {
        if target_rights == Rights::SAME_RIGHTS {
            return Ok(self);
        }
        if self.is_invalid() {
            return Err(zx_status::Status::BAD_HANDLE);
        }
        let (shard, slot, ty, side) = unpack_handle(self.raw_handle());
        let result = match ty {
            HdlType::Channel => table_replace(&CHANNELS, shard, slot, side, target_rights),
            HdlType::StreamSocket => {
                table_replace(&STREAM_SOCKETS, shard, slot, side, target_rights)
            }
            HdlType::DatagramSocket => {
                table_replace(&DATAGRAM_SOCKETS, shard, slot, side, target_rights)
            }
            HdlType::EventPair => table_replace(&EVENT_PAIRS, shard, slot, side, target_rights),
        };
        let (new_shard, new_slot) = match result {
            Ok(val) => val,
            Err(zx_status::Status::BAD_HANDLE) => {
                std::mem::forget(self);
                return Err(zx_status::Status::BAD_HANDLE);
            }
            Err(status) => {
                return Err(status);
            }
        };
        std::mem::forget(self);
        unsafe { Ok(Handle::from_raw(pack_handle(new_shard, new_slot, ty, side))) }
    }
}

/// An emulated Zircon Channel
#[derive(PartialEq, Eq, Debug, PartialOrd, Ord, Hash)]
pub struct Channel(u32);

impl From<Handle> for Channel {
    fn from(mut hdl: Handle) -> Channel {
        let out = Channel(hdl.0);
        hdl.0 = INVALID_HANDLE;
        out
    }
}
impl From<Channel> for Handle {
    fn from(mut hdl: Channel) -> Handle {
        let out = unsafe { Handle::from_raw(hdl.0) };
        hdl.0 = INVALID_HANDLE;
        out
    }
}
impl HandleBased for Channel {}
impl AsHandleRef for Channel {
    fn as_handle_ref<'a>(&'a self) -> HandleRef<'a> {
        HandleRef(self.0, std::marker::PhantomData)
    }
}

impl Peered for Channel {}

impl Drop for Channel {
    fn drop(&mut self) {
        hdl_close(self.0);
    }
}

impl Channel {
    /// Create a channel, resulting in a pair of `Channel` objects representing both
    /// sides of the channel. Messages written into one maybe read from the opposite.
    pub fn create() -> Result<(Channel, Channel), zx_status::Status> {
        let rights = Rights::CHANNEL_DEFAULT;
        let (shard, slot) = CHANNELS.new_handle_slot(rights);
        let left = pack_handle(shard, slot, HdlType::Channel, Side::Left);
        let right = pack_handle(shard, slot, HdlType::Channel, Side::Right);

        let mut shard = CHANNELS.shards[shard].lock().unwrap();
        let mut hdl_ref = HdlRef::Channel(&mut shard[slot]);
        hdl_ref
            .as_hdl_data()
            .signal(Side::Left, Signals::NONE, Signals::OBJECT_WRITABLE)
            .expect("Handle wasn't open immediately after creation");
        hdl_ref
            .as_hdl_data()
            .signal(Side::Right, Signals::NONE, Signals::OBJECT_WRITABLE)
            .expect("Handle wasn't open immediately after creation");

        Ok((Channel(left), Channel(right)))
    }

    /// Returns true if the channel is closed (i.e. other side was dropped).
    pub fn is_closed(&self) -> bool {
        assert!(!self.is_invalid());
        let (shard, slot, _, _) = unpack_handle(self.0);
        let tbl = CHANNELS.shards[shard].lock().unwrap();
        !tbl[slot].liveness.is_open()
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
        with_handle(self.0, |h, side| {
            if let HdlRef::Channel(obj) = h {
                if let Some(mut msg) = obj.q.side_mut(side.opposite()).pop_front() {
                    std::mem::swap(bytes, &mut msg.bytes);
                    std::mem::swap(handles, &mut msg.handles);
                    if obj.q.side_mut(side.opposite()).is_empty() {
                        obj.signal(side, Signals::OBJECT_READABLE, Signals::NONE)
                            .status_for_self()?;
                    }
                    Poll::Ready(Ok(()))
                } else if obj.liveness.is_open() {
                    obj.wakers.side_mut(side).pending_readable(cx)
                } else {
                    Poll::Ready(Err(zx_status::Status::PEER_CLOSED))
                }
            } else {
                unreachable!();
            }
        })
    }

    /// Read a message from a channel.
    pub fn read_etc(&self, buf: &mut MessageBufEtc) -> Result<(), zx_status::Status> {
        let (bytes, handles) = buf.split_mut();
        self.read_etc_split(bytes, handles)
    }

    /// Read a message from a channel into a separate byte vector and handle vector.
    pub fn read_etc_split(
        &self,
        bytes: &mut Vec<u8>,
        handles: &mut Vec<HandleInfo>,
    ) -> Result<(), zx_status::Status> {
        match self.poll_read_etc(&mut Context::from_waker(noop_waker_ref()), bytes, handles) {
            Poll::Ready(r) => r,
            Poll::Pending => Err(zx_status::Status::SHOULD_WAIT),
        }
    }

    fn poll_read_etc(
        &self,
        cx: &mut Context<'_>,
        bytes: &mut Vec<u8>,
        handle_infos: &mut Vec<HandleInfo>,
    ) -> Poll<Result<(), zx_status::Status>> {
        let mut handles = Vec::new();
        ready!(self.poll_read(cx, bytes, &mut handles))?;
        handle_infos.clear();
        handle_infos.extend(handles.into_iter().map(|handle| {
            let h_raw = handle.raw_handle();
            let (_, _, ty, _) = unpack_handle(h_raw);
            let rights = with_handle(h_raw, |mut href, side| href.as_hdl_data().rights(side));
            HandleInfo { handle, object_type: ty.object_type(), rights }
        }));
        Poll::Ready(Ok(()))
    }

    /// Write a message to a channel.
    pub fn write(&self, bytes: &[u8], handles: &mut [Handle]) -> Result<(), zx_status::Status> {
        let bytes_vec = bytes.to_vec();
        let mut handles_vec = Vec::with_capacity(handles.len());
        for i in 0..handles.len() {
            handles_vec.push(std::mem::replace(&mut handles[i], Handle::invalid()));
        }
        with_handle(self.0, |h, side| {
            if let HdlRef::Channel(obj) = h {
                if !obj.liveness.is_open() {
                    return Err(zx_status::Status::PEER_CLOSED);
                }
                obj.q
                    .side_mut(side)
                    .push_back(ChannelMessage { bytes: bytes_vec, handles: handles_vec });
                obj.signal(side.opposite(), Signals::NONE, Signals::OBJECT_READABLE)
                    .status_for_peer()?;
                Ok(())
            } else {
                unreachable!();
            }
        })
    }

    /// Write a message to a channel.
    pub fn write_etc<'a>(
        &self,
        bytes: &[u8],
        handles: &mut [HandleDisposition<'a>],
    ) -> Result<(), zx_status::Status> {
        let bytes_vec = bytes.to_vec();
        let mut handles_vec = Vec::with_capacity(handles.len());
        for hd in handles {
            let op: HandleOp<'a> =
                std::mem::replace(&mut hd.handle_op, HandleOp::Move(Handle::invalid()));
            if let HandleOp::Move(handle) = op {
                let (_, _, ty, _) = unpack_handle(handle.raw_handle());
                if ty.object_type() != handle.object_type()
                    && handle.object_type() != ObjectType::NONE
                {
                    return Err(zx_status::Status::INVALID_ARGS);
                }
                handles_vec.push(handle.replace(hd.rights)?);
            } else {
                panic!("unimplemented HandleOp");
            }
        }
        with_handle(self.0, |h, side| {
            if let HdlRef::Channel(obj) = h {
                if !obj.liveness.is_open() {
                    // Move the handles outside this closure before dropping them.  If any are
                    // channels in the same shard as this channel, dropping them will attempt to
                    // re-acquire the lock held by with_handle.
                    return Err(handles_vec);
                }
                obj.q
                    .side_mut(side)
                    .push_back(ChannelMessage { bytes: bytes_vec, handles: handles_vec });
                obj.signal(side.opposite(), Signals::NONE, Signals::OBJECT_READABLE)
                    .expect("Signalling readable should never fail here");
                Ok(())
            } else {
                unreachable!();
            }
        })
        .map_err(|_handles_to_drop| zx_status::Status::PEER_CLOSED)
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

/// Emulation of a Zircon Socket.
#[derive(PartialEq, Eq, Debug, PartialOrd, Ord, Hash)]
pub struct Socket(u32);

impl From<Handle> for Socket {
    fn from(mut hdl: Handle) -> Socket {
        let out = Socket(hdl.0);
        hdl.0 = INVALID_HANDLE;
        out
    }
}
impl From<Socket> for Handle {
    fn from(mut hdl: Socket) -> Handle {
        let out = unsafe { Handle::from_raw(hdl.0) };
        hdl.0 = INVALID_HANDLE;
        out
    }
}
impl HandleBased for Socket {}
impl AsHandleRef for Socket {
    fn as_handle_ref<'a>(&'a self) -> HandleRef<'a> {
        HandleRef(self.0, std::marker::PhantomData)
    }
}

impl Peered for Socket {}

impl Drop for Socket {
    fn drop(&mut self) {
        hdl_close(self.0);
    }
}

impl Socket {
    /// Create a pair of sockets
    pub fn create(sock_opts: SocketOpts) -> Result<(Socket, Socket), zx_status::Status> {
        match sock_opts {
            SocketOpts::STREAM => {
                let rights = Rights::TRANSFER
                    | Rights::WRITE
                    | Rights::READ
                    | Rights::WAIT
                    | Rights::SIGNAL
                    | Rights::SIGNAL_PEER;
                let (shard, slot) = STREAM_SOCKETS.new_handle_slot(rights);
                let left = pack_handle(shard, slot, HdlType::StreamSocket, Side::Left);
                let right = pack_handle(shard, slot, HdlType::StreamSocket, Side::Right);
                let mut shard = STREAM_SOCKETS.shards[shard].lock().unwrap();
                let mut hdl_ref = HdlRef::StreamSocket(&mut shard[slot]);
                hdl_ref
                    .as_hdl_data()
                    .signal(Side::Left, Signals::NONE, Signals::OBJECT_WRITABLE)
                    .expect("Handle wasn't open immediately after creation");
                hdl_ref
                    .as_hdl_data()
                    .signal(Side::Right, Signals::NONE, Signals::OBJECT_WRITABLE)
                    .expect("Handle wasn't open immediately after creation");
                Ok((Socket(left), Socket(right)))
            }
            SocketOpts::DATAGRAM => {
                let rights = Rights::TRANSFER
                    | Rights::WRITE
                    | Rights::READ
                    | Rights::SIGNAL
                    | Rights::SIGNAL_PEER;
                let (shard, slot) = DATAGRAM_SOCKETS.new_handle_slot(rights);
                let left = pack_handle(shard, slot, HdlType::DatagramSocket, Side::Left);
                let right = pack_handle(shard, slot, HdlType::DatagramSocket, Side::Right);
                let mut shard = DATAGRAM_SOCKETS.shards[shard].lock().unwrap();
                let mut hdl_ref = HdlRef::DatagramSocket(&mut shard[slot]);
                hdl_ref
                    .as_hdl_data()
                    .signal(Side::Left, Signals::NONE, Signals::OBJECT_WRITABLE)
                    .expect("Handle wasn't open immediately after creation");
                hdl_ref
                    .as_hdl_data()
                    .signal(Side::Right, Signals::NONE, Signals::OBJECT_WRITABLE)
                    .expect("Handle wasn't open immediately after creation");
                Ok((Socket(left), Socket(right)))
            }
        }
    }

    /// Write the given bytes into the socket.
    /// Return value (on success) is number of bytes actually written.
    pub fn write(&self, bytes: &[u8]) -> Result<usize, zx_status::Status> {
        with_handle(self.0, |h, side| {
            match h {
                HdlRef::StreamSocket(obj) => {
                    if !obj.liveness.is_open() {
                        return Err(zx_status::Status::PEER_CLOSED);
                    }
                    obj.q.side_mut(side).extend(bytes);
                    obj.signal(side.opposite(), Signals::NONE, Signals::OBJECT_READABLE)
                        .status_for_peer()?;
                }
                HdlRef::DatagramSocket(obj) => {
                    if !obj.liveness.is_open() {
                        return Err(zx_status::Status::PEER_CLOSED);
                    }
                    obj.q.side_mut(side).push_back(bytes.to_vec());
                    obj.signal(side.opposite(), Signals::NONE, Signals::OBJECT_READABLE)
                        .status_for_peer()?;
                }
                _ => panic!("Non socket passed to Socket::write"),
            }
            Ok(bytes.len())
        })
    }

    /// Return how many bytes are buffered in the socket
    pub fn outstanding_read_bytes(&self) -> Result<usize, zx_status::Status> {
        let (len, open) = with_handle(self.0, |h, side| match h {
            HdlRef::StreamSocket(obj) => {
                (obj.q.side(side.opposite()).len(), obj.liveness.is_open())
            }
            HdlRef::DatagramSocket(obj) => (
                obj.q.side(side.opposite()).front().map(|frame| frame.len()).unwrap_or(0),
                obj.liveness.is_open(),
            ),
            _ => panic!("Non socket passed to Socket::outstanding_read_bytes"),
        });
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
        with_handle(self.0, |h, side| match h {
            HdlRef::StreamSocket(obj) => {
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
                        return obj.wakers.side_mut(side).pending_readable(ctx);
                    } else {
                        return Poll::Ready(Err(zx_status::Status::PEER_CLOSED));
                    }
                }
                for (i, b) in read.drain(..copy_bytes).enumerate() {
                    bytes[i] = b;
                }
                if read.is_empty() {
                    obj.signal(side, Signals::OBJECT_READABLE, Signals::NONE).status_for_self()?;
                }
                Poll::Ready(Ok(copy_bytes))
            }
            HdlRef::DatagramSocket(obj) => {
                if let Some(frame) = obj.q.side_mut(side.opposite()).pop_front() {
                    let n = std::cmp::min(bytes.len(), frame.len());
                    bytes[..n].clone_from_slice(&frame[..n]);
                    if obj.q.side_mut(side.opposite()).is_empty() {
                        obj.signal(side, Signals::OBJECT_READABLE, Signals::NONE)
                            .status_for_self()?;
                    }
                    Poll::Ready(Ok(n))
                } else if !obj.liveness.is_open() {
                    Poll::Ready(Err(zx_status::Status::PEER_CLOSED))
                } else {
                    obj.wakers.side_mut(side).pending_readable(ctx)
                }
            }
            _ => panic!("Non socket passed to Socket::read"),
        })
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

/// Emulation of Zircon EventPair.
#[derive(PartialEq, Eq, Debug, PartialOrd, Ord, Hash)]
pub struct EventPair(u32);

impl From<Handle> for EventPair {
    fn from(mut hdl: Handle) -> EventPair {
        let out = EventPair(hdl.0);
        hdl.0 = INVALID_HANDLE;
        out
    }
}
impl From<EventPair> for Handle {
    fn from(mut hdl: EventPair) -> Handle {
        let out = unsafe { Handle::from_raw(hdl.0) };
        hdl.0 = INVALID_HANDLE;
        out
    }
}
impl HandleBased for EventPair {}
impl AsHandleRef for EventPair {
    fn as_handle_ref<'a>(&'a self) -> HandleRef<'a> {
        HandleRef(self.0, std::marker::PhantomData)
    }
}

impl Peered for EventPair {}

impl Drop for EventPair {
    fn drop(&mut self) {
        hdl_close(self.0);
    }
}

impl EventPair {
    /// Create an event pair.
    pub fn create() -> Result<(EventPair, EventPair), Status> {
        let rights = Rights::EVENTPAIR_DEFAULT;
        let (shard, slot) = EVENT_PAIRS.new_handle_slot(rights);
        let left = pack_handle(shard, slot, HdlType::EventPair, Side::Left);
        let right = pack_handle(shard, slot, HdlType::EventPair, Side::Right);
        Ok((EventPair(left), EventPair(right)))
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

/// A buffer for _receiving_ messages from a channel.
///
/// This differs from `MessageBuf` in that it holds `HandleInfo` with
/// extended handle information.
///
/// A `MessageBufEtc` is essentially a byte buffer and a vector of handle
/// infos, but move semantics for "taking" handles requires special handling.
///
/// Note that for sending messages to a channel, the caller manages the buffers,
/// using a plain byte slice and `Vec<HandleDisposition>`.
#[derive(Debug, Default)]
pub struct MessageBufEtc {
    bytes: Vec<u8>,
    handle_infos: Vec<HandleInfo>,
}

impl MessageBufEtc {
    /// Create a new, empty, message buffer.
    pub fn new() -> Self {
        Default::default()
    }

    /// Create a new non-empty message buffer.
    pub fn new_with(v: Vec<u8>, h: Vec<HandleInfo>) -> Self {
        Self { bytes: v, handle_infos: h }
    }

    /// Splits apart the message buf into a vector of bytes and a vector of handle infos.
    pub fn split_mut(&mut self) -> (&mut Vec<u8>, &mut Vec<HandleInfo>) {
        (&mut self.bytes, &mut self.handle_infos)
    }

    /// Splits apart the message buf into a vector of bytes and a vector of handle infos.
    pub fn split(self) -> (Vec<u8>, Vec<HandleInfo>) {
        (self.bytes, self.handle_infos)
    }

    /// Ensure that the buffer has the capacity to hold at least `n_bytes` bytes.
    pub fn ensure_capacity_bytes(&mut self, n_bytes: usize) {
        ensure_capacity(&mut self.bytes, n_bytes);
    }

    /// Ensure that the buffer has the capacity to hold at least `n_handles` handle infos.
    pub fn ensure_capacity_handle_infos(&mut self, n_handle_infos: usize) {
        ensure_capacity(&mut self.handle_infos, n_handle_infos);
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
    pub fn n_handle_infos(&self) -> usize {
        self.handle_infos.len()
    }

    /// Take the handle at the specified index from the message buffer. If the
    /// method is called again with the same index, it will return `None`, as
    /// will happen if the index exceeds the number of handles available.
    pub fn take_handle_info(&mut self, index: usize) -> Option<HandleInfo> {
        self.handle_infos.get_mut(index).and_then(|handle_info| {
            if handle_info.handle.is_invalid() {
                None
            } else {
                Some(std::mem::replace(
                    handle_info,
                    HandleInfo {
                        handle: Handle::invalid(),
                        object_type: ObjectType::NONE,
                        rights: Rights::NONE,
                    },
                ))
            }
        })
    }

    /// Clear the bytes and handles contained in the buf. This will drop any
    /// contained handles, resulting in their resources being freed.
    pub fn clear(&mut self) {
        self.bytes.clear();
        self.handle_infos.clear();
    }
}

fn ensure_capacity<T>(vec: &mut Vec<T>, size: usize) {
    let len = vec.len();
    if size > len {
        vec.reserve(size - len);
    }
}

pub mod on_signals {
    use super::*;

    /// Wait for some signals to be raised.
    #[must_use = "futures do nothing unless polled"]
    pub struct OnSignals<'a> {
        h: u32,
        koid: u64,
        signals: Signals,
        _lifetime: PhantomData<&'a ()>,
    }

    impl Unpin for OnSignals<'_> {}

    impl<'a> OnSignals<'a> {
        /// Construct a new OnSignals
        pub fn new<T: AsHandleRef>(handle: &'a T, signals: Signals) -> Self {
            Self::from_ref(handle.as_handle_ref(), signals)
        }

        /// Creates a new `OnSignals` using a HandleRef instead of an AsHandleRef.
        ///
        /// Passing a HandleRef to OnSignals::new is likely to lead to borrow check errors, since
        /// the resulting OnSignals is tied to the lifetime of the HandleRef itself and not the
        /// handle it refers to. Use this instead when you need to pass a HandleRef.
        pub fn from_ref(handle: HandleRef<'a>, signals: Signals) -> Self {
            let h = handle.0;
            with_handle(h, |mut hdl, side| Self {
                h,
                koid: hdl.as_hdl_data().koids(side).0,
                signals,
                _lifetime: PhantomData,
            })
        }

        /// This function allows the `OnSignals` object to live for the `'static` lifetime.
        ///
        /// It is functionally a no-op, but callers of this method should note that
        /// `OnSignals` will not fire if the handle that was used to create it is dropped or
        /// transferred to another process.
        pub fn extend_lifetime(self) -> OnSignals<'static> {
            OnSignals { h: self.h, koid: self.koid, signals: self.signals, _lifetime: PhantomData }
        }
    }

    impl Future for OnSignals<'_> {
        type Output = Result<Signals, Status>;
        fn poll(self: Pin<&mut Self>, ctx: &mut Context<'_>) -> Poll<Self::Output> {
            with_handle(self.h, |mut h, side| {
                let h = h.as_hdl_data();
                if self.koid != h.koids(side).0 || !h.liveness().is_side_open(side) {
                    if self.signals.contains(Signals::HANDLE_CLOSED) {
                        Poll::Ready(Signals::HANDLE_CLOSED)
                    } else {
                        Poll::Pending
                    }
                } else {
                    h.poll_signals(ctx, side, self.signals)
                }
            })
            .map(Ok)
        }
    }
}

bitflags! {
    /// Signals that can be waited upon.
    ///
    /// See [signals](https://fuchsia.dev/fuchsia-src/concepts/kernel/signals) for more information.
    #[repr(transparent)]
    pub struct Signals : u32 {
        /// No signals
        const NONE = 0x00000000;
        /// All object signals
        const OBJECT_ALL = 0x00ffffff;
        /// All user signals
        const USER_ALL = 0xff000000;

        /// Object signal 0
        const OBJECT_0 = 1 << 0;
        /// Object signal 1
        const OBJECT_1 = 1 << 1;
        /// Object signal 2
        const OBJECT_2 = 1 << 2;
        /// Object signal 3
        const OBJECT_3 = 1 << 3;
        /// Object signal 4
        const OBJECT_4 = 1 << 4;
        /// Object signal 5
        const OBJECT_5 = 1 << 5;
        /// Object signal 6
        const OBJECT_6 = 1 << 6;
        /// Object signal 7
        const OBJECT_7 = 1 << 7;
        /// Object signal 8
        const OBJECT_8 = 1 << 8;
        /// Object signal 9
        const OBJECT_9 = 1 << 9;
        /// Object signal 10
        const OBJECT_10 = 1 << 10;
        /// Object signal 11
        const OBJECT_11 = 1 << 11;
        /// Object signal 12
        const OBJECT_12 = 1 << 12;
        /// Object signal 13
        const OBJECT_13 = 1 << 13;
        /// Object signal 14
        const OBJECT_14 = 1 << 14;
        /// Object signal 15
        const OBJECT_15 = 1 << 15;
        /// Object signal 16
        const OBJECT_16 = 1 << 16;
        /// Object signal 17
        const OBJECT_17 = 1 << 17;
        /// Object signal 18
        const OBJECT_18 = 1 << 18;
        /// Object signal 19
        const OBJECT_19 = 1 << 19;
        /// Object signal 20
        const OBJECT_20 = 1 << 20;
        /// Object signal 21
        const OBJECT_21 = 1 << 21;
        /// Object signal 22
        const OBJECT_22 = 1 << 22;
        /// Handle closed
        const HANDLE_CLOSED = 1 << 23;
        /// User signal 0
        const USER_0 = 1 << 24;
        /// User signal 1
        const USER_1 = 1 << 25;
        /// User signal 2
        const USER_2 = 1 << 26;
        /// User signal 3
        const USER_3 = 1 << 27;
        /// User signal 4
        const USER_4 = 1 << 28;
        /// User signal 5
        const USER_5 = 1 << 29;
        /// User signal 6
        const USER_6 = 1 << 30;
        /// User signal 7
        const USER_7 = 1 << 31;

        /// All user signals
        const USER_SIGNALS = Self::USER_0.bits() |
        Self::USER_1.bits() |
        Self::USER_2.bits() |
        Self::USER_3.bits() |
        Self::USER_4.bits() |
        Self::USER_5.bits() |
        Self::USER_6.bits() |
        Self::USER_7.bits();

        /// Object is readable
        const OBJECT_READABLE = Self::OBJECT_0.bits();
        /// Object is writable
        const OBJECT_WRITABLE = Self::OBJECT_1.bits();
        /// Object peer closed
        const OBJECT_PEER_CLOSED = Self::OBJECT_2.bits();

        /// Channel peer closed
        const CHANNEL_PEER_CLOSED = Self::OBJECT_PEER_CLOSED.bits();
    }
}

impl Signals {
    /// Returns `Status::INVALID_ARGS` if this signal set contains non-user signals.
    fn validate_user_signals(&self) -> Result<(), Status> {
        if Signals::USER_SIGNALS.contains(*self) {
            Ok(())
        } else {
            Err(Status::INVALID_ARGS)
        }
    }
}

bitflags! {
    /// Rights associated with a handle.
    ///
    /// See [rights](https://fuchsia.dev/fuchsia-src/concepts/kernel/rights) for more information.
    #[repr(C)]
    pub struct Rights: u32 {
        /// No rights.
        const NONE           = 0;
        /// Duplicate right.
        const DUPLICATE      = 1 << 0;
        /// Transfer right.
        const TRANSFER       = 1 << 1;
        /// Read right.
        const READ           = 1 << 2;
        /// Write right.
        const WRITE          = 1 << 3;
        /// Execute right.
        const EXECUTE = 1 << 4;
        /// Map right.
        const MAP = 1 << 5;
        /// Get Property right.
        const GET_PROPERTY = 1 << 6;
        /// Set Property right.
        const SET_PROPERTY = 1 << 7;
        /// Enumerate right.
        const ENUMERATE = 1 << 8;
        /// Destroy right.
        const DESTROY = 1 << 9;
        /// Set Policy right.
        const SET_POLICY = 1 << 10;
        /// Get Policy right.
        const GET_POLICY = 1 << 11;
        /// Signal right.
        const SIGNAL = 1 << 12;
        /// Signal Peer right.
        const SIGNAL_PEER = 1 << 13;
        /// Wait right.
        const WAIT = 1 << 14;
        /// Inspect right.
        const INSPECT = 1 << 15;
        /// Manage Job right.
        const MANAGE_JOB = 1 << 16;
        /// Manage Process right.
        const MANAGE_PROCESS = 1 << 17;
        /// Manage Thread right.
        const MANAGE_THREAD = 1 << 18;
        /// Apply Profile right.
        const APPLY_PROFILE = 1 << 19;
        /// Manage Socket right.
        const MANAGE_SOCKET = 1 << 20;
        /// Same rights.
        const SAME_RIGHTS = 1 << 31;
        /// A basic set of rights for most things.
        const BASIC_RIGHTS = Rights::TRANSFER.bits() |
                             Rights::DUPLICATE.bits() |
                             Rights::WAIT.bits() |
                             Rights::INSPECT.bits();
        /// IO related rights
        const IO = Rights::WRITE.bits() |
                   Rights::READ.bits();
        /// Rights of a new channel.
        const CHANNEL_DEFAULT = (Rights::BASIC_RIGHTS.bits() & !Rights::DUPLICATE.bits()) |
                                Rights::IO.bits() |
                                Rights::SIGNAL.bits() |
                                Rights::SIGNAL_PEER.bits();
        /// Rights of a new channel.
        const EVENTPAIR_DEFAULT =
                                Rights::TRANSFER.bits() |
                                Rights::DUPLICATE.bits() |
                                Rights::IO.bits() |
                                Rights::SIGNAL.bits() |
                                Rights::SIGNAL_PEER.bits();
    }
}

impl Rights {
    /// Same as from_bits() but a const fn.
    #[inline]
    pub const fn from_bits_const(bits: u32) -> Option<Rights> {
        if (bits & !Rights::all().bits()) == 0 {
            return Some(Rights { bits });
        } else {
            None
        }
    }
}

/// Handle operation.
#[derive(Debug, Eq, PartialEq, Ord, PartialOrd, Hash)]
pub enum HandleOp<'a> {
    /// Move the handle.
    Move(Handle),
    /// Duplicate the handle.
    Duplicate(HandleRef<'a>),
}

/// Operation to perform on handles during write.
/// Based on zx_handle_disposition_t, but does not match the same layout.
#[derive(Debug, Eq, PartialEq, Ord, PartialOrd, Hash)]
pub struct HandleDisposition<'a> {
    /// Handle operation.
    pub handle_op: HandleOp<'a>,
    /// Object type to check, or NONE to avoid the check.
    pub object_type: ObjectType,
    /// Rights to send, or SAME_RIGHTS.
    /// Rights are checked against existing handle rights to ensure there is no
    /// increase in rights.
    pub rights: Rights,
    /// Result of attempting to write this handle disposition.
    pub result: Status,
}

/// HandleInfo represents a handle with additional metadata.
#[derive(Debug, Eq, PartialEq, Ord, PartialOrd, Hash)]
pub struct HandleInfo {
    /// The handle.
    pub handle: Handle,
    /// The type of object referenced by the handle.
    pub object_type: ObjectType,
    /// The rights of the handle.
    pub rights: Rights,
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
enum HdlType {
    Channel,
    StreamSocket,
    DatagramSocket,
    EventPair,
}

impl HdlType {
    fn object_type(&self) -> ObjectType {
        match self {
            HdlType::Channel => ObjectType::CHANNEL,
            HdlType::StreamSocket => ObjectType::SOCKET,
            HdlType::DatagramSocket => ObjectType::SOCKET,
            HdlType::EventPair => ObjectType::EVENTPAIR,
        }
    }
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

    fn is_side_open(self, side: Side) -> bool {
        match (self, side) {
            (Liveness::Open, _) => true,
            (Liveness::Left, Side::Left) => true,
            (Liveness::Right, Side::Right) => true,
            (Liveness::Left, Side::Right) => false,
            (Liveness::Right, Side::Left) => false,
        }
    }
}

#[derive(Default)]
struct WakerSlot(Vec<Waker>);

impl WakerSlot {
    fn wake(&mut self) {
        self.0.drain(..).for_each(|w| w.wake());
    }

    fn arm(&mut self, ctx: &mut Context<'_>) {
        self.0.push(ctx.waker().clone())
    }
}

#[derive(Default)]
struct Wakers {
    signals: [WakerSlot; 32],
}

impl Wakers {
    fn for_signals_in(&mut self, signals: Signals, mut f: impl FnMut(&mut WakerSlot)) {
        for i in 0..32 {
            if signals.bits() & (1 << i) != 0 {
                f(&mut self.signals[i])
            }
        }
    }

    fn wake(&mut self, signals: Signals) {
        self.for_signals_in(signals, |w| w.wake())
    }

    fn arm(&mut self, signals: Signals, ctx: &mut Context<'_>) {
        self.for_signals_in(signals, |w| w.arm(ctx))
    }

    fn pending<R>(&mut self, signals: Signals, ctx: &mut Context<'_>) -> Poll<R> {
        self.arm(signals, ctx);
        Poll::Pending
    }

    fn pending_readable<R>(&mut self, ctx: &mut Context<'_>) -> Poll<R> {
        self.pending(
            Signals::OBJECT_READABLE | Signals::HANDLE_CLOSED | Signals::OBJECT_PEER_CLOSED,
            ctx,
        )
    }
}

#[derive(Debug)]
enum SignalError {
    HandleInvalid,
}

trait SignalErrorToStatus<T> {
    fn status_for_self(self) -> Result<T, Status>;
    fn status_for_peer(self) -> Result<T, Status>;
}

impl<T> SignalErrorToStatus<T> for Result<T, SignalError> {
    fn status_for_self(self) -> Result<T, Status> {
        self.map_err(|e| {
            let SignalError::HandleInvalid = e;
            Status::BAD_HANDLE
        })
    }

    fn status_for_peer(self) -> Result<T, Status> {
        self.map_err(|e| {
            let SignalError::HandleInvalid = e;
            Status::PEER_CLOSED
        })
    }
}

trait HdlData {
    fn signal(
        &mut self,
        side: Side,
        clear_mask: Signals,
        set_mask: Signals,
    ) -> Result<(), SignalError>;
    fn poll_signals(
        &mut self,
        ctx: &mut Context<'_>,
        side: Side,
        signals: Signals,
    ) -> Poll<Signals>;
    fn koids(&self, side: Side) -> (u64, u64);
    fn liveness(&self) -> Liveness;
    fn rights(&self, side: Side) -> Rights;
}

struct Hdl<Q> {
    q: Sided<Q>,
    wakers: Sided<Wakers>,
    liveness: Liveness,
    koid_left: u64,
    rights: Sided<Rights>,
    signals: Sided<Signals>,
}

impl<Q> HdlData for Hdl<Q> {
    fn signal(
        &mut self,
        side: Side,
        clear_mask: Signals,
        set_mask: Signals,
    ) -> Result<(), SignalError> {
        if !self.liveness.is_side_open(side) {
            return Err(SignalError::HandleInvalid);
        }
        let signals = self.signals.side_mut(side);
        signals.remove(clear_mask);
        signals.insert(set_mask);
        self.wakers.side_mut(side).wake(*signals);
        Ok(())
    }

    fn liveness(&self) -> Liveness {
        self.liveness
    }

    fn poll_signals(
        &mut self,
        ctx: &mut Context<'_>,
        side: Side,
        signals: Signals,
    ) -> Poll<Signals> {
        let intersection = *self.signals.side(side) & signals;
        if !intersection.is_empty() {
            Poll::Ready(intersection)
        } else {
            self.wakers.side_mut(side).pending(signals, ctx)
        }
    }

    fn koids(&self, side: Side) -> (u64, u64) {
        match side {
            Side::Left => (self.koid_left, self.koid_left + 1),
            Side::Right => (self.koid_left + 1, self.koid_left),
        }
    }

    fn rights(&self, side: Side) -> Rights {
        *self.rights.side(side)
    }
}

enum HdlRef<'a> {
    Channel(&'a mut Hdl<VecDeque<ChannelMessage>>),
    StreamSocket(&'a mut Hdl<VecDeque<u8>>),
    DatagramSocket(&'a mut Hdl<VecDeque<Vec<u8>>>),
    EventPair(&'a mut Hdl<()>),
}

impl<'a> HdlRef<'a> {
    fn as_hdl_data<'b>(&'b mut self) -> &'b mut dyn HdlData {
        match self {
            HdlRef::Channel(hdl) => *hdl,
            HdlRef::StreamSocket(hdl) => *hdl,
            HdlRef::DatagramSocket(hdl) => *hdl,
            HdlRef::EventPair(hdl) => *hdl,
        }
    }
}

#[cfg(debug_assertions)]
std::thread_local! {
    static IN_WITH_HANDLE: Cell<bool> = Cell::new(false);
}

fn with_handle<R>(handle: u32, f: impl FnOnce(HdlRef<'_>, Side) -> R) -> R {
    #[cfg(debug_assertions)]
    IN_WITH_HANDLE.with(|iwh| assert_eq!(iwh.replace(true), false));
    let (shard, slot, ty, side) = unpack_handle(handle);
    let r = match ty {
        HdlType::Channel => {
            f(HdlRef::Channel(&mut CHANNELS.shards[shard].lock().unwrap()[slot]), side)
        }
        HdlType::StreamSocket => {
            f(HdlRef::StreamSocket(&mut STREAM_SOCKETS.shards[shard].lock().unwrap()[slot]), side)
        }
        HdlType::DatagramSocket => f(
            HdlRef::DatagramSocket(&mut DATAGRAM_SOCKETS.shards[shard].lock().unwrap()[slot]),
            side,
        ),
        HdlType::EventPair => {
            f(HdlRef::EventPair(&mut EVENT_PAIRS.shards[shard].lock().unwrap()[slot]), side)
        }
    };
    #[cfg(debug_assertions)]
    IN_WITH_HANDLE.with(|iwh| assert_eq!(iwh.replace(false), true));
    r
}

const SHARD_COUNT: usize = 16;

struct HandleTable<T> {
    shards: [Mutex<Slab<Hdl<T>>>; SHARD_COUNT],
    next_shard: AtomicUsize,
    _t: std::marker::PhantomData<T>,
}

lazy_static::lazy_static! {
    static ref CHANNELS: HandleTable<VecDeque<ChannelMessage>> = Default::default();
    static ref STREAM_SOCKETS: HandleTable<VecDeque<u8>> = Default::default();
    static ref DATAGRAM_SOCKETS: HandleTable<VecDeque<Vec<u8>>> = Default::default();
    static ref EVENT_PAIRS: HandleTable<()> = Default::default();
}

static NEXT_KOID: AtomicU64 = AtomicU64::new(1);

impl<T> Default for HandleTable<T> {
    fn default() -> Self {
        Self {
            shards: Default::default(),
            next_shard: Default::default(),
            _t: std::marker::PhantomData,
        }
    }
}

impl<T: Default> HandleTable<T> {
    fn new_handle_slot(&self, rights: Rights) -> (usize, usize) {
        self.insert_hdl(Hdl {
            q: Default::default(),
            wakers: Default::default(),
            liveness: Liveness::Open,
            koid_left: NEXT_KOID.fetch_add(2, Ordering::Relaxed),
            rights: Sided { left: rights, right: rights },
            signals: Sided { left: Signals::empty(), right: Signals::empty() },
        })
    }

    fn insert_hdl(&self, hdl: Hdl<T>) -> (usize, usize) {
        let shard = self.next_shard.fetch_add(1, Ordering::Relaxed) & 15;
        let mut h = self.shards[shard].lock().unwrap();
        (shard, h.insert(hdl))
    }
}

fn pack_handle(shard: usize, slot: usize, ty: HdlType, side: Side) -> u32 {
    assert!(shard <= SHARD_COUNT);
    let side_bit = match side {
        Side::Left => 0,
        Side::Right => 1,
    };
    let ty_bits = match ty {
        HdlType::Channel => 0,
        HdlType::StreamSocket => 1,
        HdlType::DatagramSocket => 2,
        HdlType::EventPair => 3,
    };
    let shard_bits = shard as u32;
    let slot_bits = slot as u32;
    ((slot_bits << 7) | (shard_bits << 3) | (ty_bits << 1) | side_bit) + 1
}

fn unpack_handle(handle: u32) -> (usize, usize, HdlType, Side) {
    let handle = handle - 1;
    let side = if handle & 1 == 0 { Side::Left } else { Side::Right };
    let ty = match (handle >> 1) & 0x3 {
        0 => HdlType::Channel,
        1 => HdlType::StreamSocket,
        2 => HdlType::DatagramSocket,
        3 => HdlType::EventPair,
        x => panic!("Bad handle type {}", x),
    };
    let shard = ((handle >> 3) & 15) as usize;
    let slot = (handle >> 7) as usize;
    (shard, slot, ty, side)
}

fn table_contains<T>(tbl: &HandleTable<T>, shard: usize, slot: usize, side: Side) -> bool {
    match tbl.shards[shard].lock().unwrap().get(slot) {
        None => false,
        Some(h) => match (h.liveness, side) {
            (Liveness::Open, _) => true,
            (Liveness::Left, Side::Left) => true,
            (Liveness::Right, Side::Right) => true,
            _ => false,
        },
    }
}

fn table_replace<T>(
    tbl: &HandleTable<T>,
    shard: usize,
    slot: usize,
    side: Side,
    target_rights: Rights,
) -> Result<(usize, usize), zx_status::Status> {
    let mut tbl_shard = tbl.shards[shard].lock().unwrap();
    match tbl_shard.get_mut(slot) {
        None => Err(zx_status::Status::BAD_HANDLE),
        Some(h) => {
            let rights = h.rights.side_mut(side);
            if !rights.contains(target_rights) {
                return Err(zx_status::Status::INVALID_ARGS);
            }
            *rights = target_rights;
            // zx_handle_replace results in a new handle id. However, because emulated handles
            // embed the (shard, slot) location in the handle id, doing the same here would
            // break the other end of the handle pair.
            Ok((shard, slot))
        }
    }
}

fn close_in_table<T>(tbl: &HandleTable<T>, shard: usize, slot: usize, side: Side) {
    let mut tbl = tbl.shards[shard].lock().unwrap();
    let h = &mut tbl[slot];
    let removed = match h.liveness.close(side) {
        None => {
            h.wakers.side_mut(side).wake(Signals::HANDLE_CLOSED);
            Some(tbl.remove(slot))
        }
        Some(liveness) => {
            h.liveness = liveness;
            h.wakers.side_mut(side).wake(Signals::HANDLE_CLOSED);
            h.signal(side.opposite(), Signals::empty(), Signals::OBJECT_PEER_CLOSED)
                .expect("Close reported other side was open but we could not signal it.");
            None
        }
    };
    drop(tbl);
    drop(removed);
}

/// Close the handle: no action if hdl==INVALID_HANDLE
fn hdl_close(hdl: u32) {
    if hdl == INVALID_HANDLE {
        return;
    }
    let (shard, slot, ty, side) = unpack_handle(hdl);
    match ty {
        HdlType::Channel => close_in_table(&CHANNELS, shard, slot, side),
        HdlType::StreamSocket => close_in_table(&STREAM_SOCKETS, shard, slot, side),
        HdlType::DatagramSocket => close_in_table(&DATAGRAM_SOCKETS, shard, slot, side),
        HdlType::EventPair => close_in_table(&EVENT_PAIRS, shard, slot, side),
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use fuchsia_zircon_status as zx_status;
    use futures::FutureExt;
    use std::mem::ManuallyDrop;
    use zx_status::Status;

    /// Returns a "handle" which mimics a closed handle.
    ///
    /// This handle is not a real handle, and will cause panics most places it is used. It should
    /// be impossible to get this kind of handle safely, but some tests want to assert what happens
    /// if you try to use a closed handle.
    ///
    /// The result is wrapped in a ManuallyDrop because you should avoid dropping the resulting
    /// handle because that too will trigger a panic.
    #[deny(unsafe_op_in_unsafe_fn)]
    unsafe fn mimic_closed_handle() -> std::mem::ManuallyDrop<Handle> {
        // This corresponds to the last id we would allocate in its shard; this test won't generate that many handles at once.
        let raw_unallocated_handle = u32::MAX;
        let unallocated_handle = unsafe { Handle::from_raw(raw_unallocated_handle) };
        std::mem::ManuallyDrop::new(unallocated_handle)
    }

    #[test]
    fn channel_create_not_closed() {
        let (a, b) = Channel::create().unwrap();
        assert_eq!(a.is_closed(), false);
        assert_eq!(b.is_closed(), false);
    }

    #[test]
    fn channel_closed_after_drop() {
        let (a, _b) = Channel::create().unwrap();
        let a_copy = Channel::from(unsafe { Handle::from_raw(a.0) });
        // Prevent trying to close a twice at the end of the test.
        let a_copy = ManuallyDrop::new(a_copy);
        drop(a);
        assert_eq!(a_copy.is_closed(), true);
    }

    #[test]
    fn channel_drop_left_closes_right() {
        let (a, b) = Channel::create().unwrap();
        drop(a);
        assert_eq!(b.is_closed(), true);
    }

    #[test]
    fn channel_drop_right_closes_left() {
        let (a, b) = Channel::create().unwrap();
        drop(b);
        assert_eq!(a.is_closed(), true);
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
    fn channel_write_etc_read_etc() {
        let (a, b) = Channel::create().unwrap();
        let (c, d) = Channel::create().unwrap();
        let mut incoming = MessageBufEtc::new();

        assert_eq!(b.read_etc(&mut incoming).err().unwrap(), Status::SHOULD_WAIT);
        d.write(&[4, 5, 6], &mut vec![]).unwrap();
        let mut hds = vec![
            HandleDisposition {
                handle_op: HandleOp::Move(c.into()),
                object_type: ObjectType::CHANNEL,
                rights: Rights::SAME_RIGHTS,
                result: Status::OK,
            },
            HandleDisposition {
                handle_op: HandleOp::Move(d.into()),
                object_type: ObjectType::CHANNEL,
                rights: Rights::TRANSFER | Rights::READ,
                result: Status::OK,
            },
        ];
        a.write_etc(&[1, 2, 3], &mut hds).unwrap();

        b.read_etc(&mut incoming).unwrap();
        assert_eq!(incoming.bytes(), &[1, 2, 3]);
        assert_eq!(incoming.n_handle_infos(), 2);

        let mut c_handle_info = incoming.take_handle_info(0).unwrap();
        assert_eq!(c_handle_info.object_type, ObjectType::CHANNEL);
        assert_eq!(c_handle_info.rights, Rights::CHANNEL_DEFAULT);
        let mut d_handle_info = incoming.take_handle_info(1).unwrap();
        assert_eq!(d_handle_info.object_type, ObjectType::CHANNEL);
        assert_eq!(d_handle_info.rights, Rights::TRANSFER | Rights::READ);
        let c: Channel = std::mem::replace(&mut c_handle_info.handle, Handle::invalid()).into();
        let d: Channel = std::mem::replace(&mut d_handle_info.handle, Handle::invalid()).into();
        c.read_etc(&mut incoming).unwrap();
        drop(d);
        assert_eq!(incoming.bytes(), &[4, 5, 6]);
        assert_eq!(incoming.n_handle_infos(), 0);
    }

    #[test]
    fn mixed_channel_write_read_etc() {
        let (a, b) = Channel::create().unwrap();
        let (c, _) = Channel::create().unwrap();
        a.write(&[1, 2, 3], &mut [c.into()]).unwrap();
        let mut buf = MessageBufEtc::new();
        b.read_etc(&mut buf).unwrap();
        assert_eq!(buf.bytes(), &[1, 2, 3]);
        assert_eq!(buf.n_handle_infos(), 1);
        let hi = &buf.handle_infos[0];
        assert_eq!(hi.object_type, ObjectType::CHANNEL);
        assert_eq!(hi.rights, Rights::CHANNEL_DEFAULT);
        assert_ne!(hi.handle, Handle::invalid());
    }

    #[test]
    fn mixed_channel_write_etc_read() {
        let (a, b) = Channel::create().unwrap();
        let (c, _) = Channel::create().unwrap();
        let hd = HandleDisposition {
            handle_op: HandleOp::Move(c.into()),
            object_type: ObjectType::NONE,
            rights: Rights::SAME_RIGHTS,
            result: Status::OK,
        };
        a.write_etc(&[1, 2, 3], &mut [hd]).unwrap();
        let mut buf = MessageBuf::new();
        b.read(&mut buf).unwrap();
        assert_eq!(buf.bytes(), &[1, 2, 3]);
        assert_eq!(buf.n_handles(), 1);
        assert_ne!(buf.handles[0], Handle::invalid());
    }

    #[test]
    fn channel_write_etc_closes_handles_in_same_shard_on_peer_closed() {
        // Create 2 channel pairs, ensuring both are allocated in the same shard.
        let (a, b) = Channel::create().unwrap();
        let (a_shard, _, _, _) = unpack_handle(a.0);

        let (c, _d) = loop {
            let (c, d) = Channel::create().unwrap();
            let (c_shard, _, _, _) = unpack_handle(c.0);

            if a_shard == c_shard {
                break (c, d);
            }
        };

        let hd = HandleDisposition {
            handle_op: HandleOp::Move(c.into()),
            object_type: ObjectType::NONE,
            rights: Rights::SAME_RIGHTS,
            result: Status::OK,
        };

        // Ensure that write_etc does not deadlock while trying to close c, which is in the same
        // shard and is guarded by the same mutex as a.
        drop(b);
        assert_eq!(a.write_etc(&[1, 2, 3], &mut [hd]), Err(Status::PEER_CLOSED));
    }

    #[test]
    fn socket_write_read() {
        let (a, b) = Socket::create(SocketOpts::STREAM).unwrap();
        a.write(&[1, 2, 3]).unwrap();
        let mut buf = [0u8; 128];
        assert_eq!(b.read(&mut buf).unwrap(), 3);
        assert_eq!(&buf[0..3], &[1, 2, 3]);
    }

    /// Ensure streaming sockets are waitable by default
    #[test]
    fn socket_wait() {
        let (a, b) = Socket::create(SocketOpts::STREAM).unwrap();
        with_handle(a.0, |h, _| match h {
            HdlRef::StreamSocket(obj) => {
                assert_eq!((obj.rights.left & Rights::WAIT), Rights::WAIT);
                assert_eq!((obj.rights.right & Rights::WAIT), Rights::WAIT);
            }
            _ => unreachable!(),
        });
        with_handle(b.0, |h, _| match h {
            HdlRef::StreamSocket(obj) => {
                assert_eq!((obj.rights.left & Rights::WAIT), Rights::WAIT);
                assert_eq!((obj.rights.right & Rights::WAIT), Rights::WAIT);
            }
            _ => unreachable!(),
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
        // The handle vector should only contain invalid handles.
        for handle in handles_to_send {
            assert!(handle.is_invalid());
        }

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
    fn object_type_is_correct() {
        let (c1, c2) = Channel::create().unwrap();
        let (s1, s2) = Socket::create(SocketOpts::STREAM).unwrap();
        assert_eq!(c1.into_handle().object_type(), ObjectType::CHANNEL);
        assert_eq!(c2.into_handle().object_type(), ObjectType::CHANNEL);
        assert_eq!(s1.into_handle().object_type(), ObjectType::SOCKET);
        assert_eq!(s2.into_handle().object_type(), ObjectType::SOCKET);
    }

    #[cfg(not(target_os = "fuchsia"))]
    #[test]
    fn invalid_handle_is_not_dangling() {
        let h = Handle::invalid();
        assert!(!h.is_dangling());
    }

    #[cfg(not(target_os = "fuchsia"))]
    #[test]
    fn live_valid_handle_is_not_dangling() {
        let (c1, c2) = Channel::create().unwrap();
        assert!(!c1.is_dangling());
        assert!(!c2.is_dangling());
    }

    #[cfg(not(target_os = "fuchsia"))]
    #[test]
    fn closed_handle_is_dangling() {
        let closed_handle = unsafe { mimic_closed_handle() };

        assert!(closed_handle.is_dangling());
    }

    #[test]
    fn handle_basic_info_success() {
        let (c1, c2) = Channel::create().unwrap();
        let c1_info = c1.basic_info().unwrap();
        let c2_info = c2.basic_info().unwrap();

        assert_ne!(c1_info.koid, Koid(0));
        assert_ne!(c1_info.related_koid, Koid(0));
        assert_ne!(c1_info.koid, c1_info.related_koid);
        assert_eq!(c1_info.related_koid, c2_info.koid);
        assert_eq!(c1_info.koid, c2_info.related_koid);

        assert_eq!(c1_info.rights, Rights::CHANNEL_DEFAULT);
        assert_eq!(c1_info.object_type, ObjectType::CHANNEL);
        assert_eq!(c1_info.reserved, 0);
    }

    #[test]
    fn handle_basic_info_invalid() {
        assert_eq!(Handle::invalid().basic_info().unwrap_err(), zx_status::Status::BAD_HANDLE);

        // Note non-zero but invalid handles can't be tested because of a
        // panic when the handle isn't found in with_handle().
    }

    #[test]
    fn handle_replace_success() {
        let (c1, c2) = Channel::create().unwrap();
        let c1_basic_info = c1.basic_info().unwrap();
        let c2_basic_info = c2.basic_info().unwrap();
        assert_eq!(c1_basic_info.rights, Rights::CHANNEL_DEFAULT);

        let new_handle = c1.into_handle().replace(Rights::TRANSFER | Rights::WRITE).unwrap();

        let new_c1_basic_info = new_handle.basic_info().unwrap();
        assert_eq!(new_c1_basic_info.koid, c1_basic_info.koid);
        assert_eq!(new_c1_basic_info.related_koid, c1_basic_info.related_koid);
        assert_eq!(new_c1_basic_info.object_type, ObjectType::CHANNEL);
        assert_eq!(new_c1_basic_info.rights, Rights::TRANSFER | Rights::WRITE);

        let new_c2_basic_info = c2.basic_info().unwrap();
        assert_eq!(new_c2_basic_info.koid, c2_basic_info.koid);
        assert_eq!(new_c2_basic_info.related_koid, c2_basic_info.related_koid);
        assert_eq!(new_c2_basic_info.object_type, c2_basic_info.object_type);
        assert_eq!(new_c2_basic_info.rights, c2_basic_info.rights);
    }

    #[test]
    fn handle_replace_invalid() {
        assert_eq!(
            Handle::invalid().replace(Rights::TRANSFER).unwrap_err(),
            zx_status::Status::BAD_HANDLE
        );

        let closed_handle = unsafe { mimic_closed_handle() };
        assert_eq!(
            ManuallyDrop::into_inner(closed_handle).replace(Rights::TRANSFER).unwrap_err(),
            zx_status::Status::BAD_HANDLE
        );
    }

    #[test]
    fn handle_replace_increasing_rights() {
        let (c1, _) = Channel::create().unwrap();
        let orig_basic_info = c1.basic_info().unwrap();
        assert_eq!(orig_basic_info.rights, Rights::CHANNEL_DEFAULT);
        assert_eq!(
            c1.into_handle().replace(Rights::DUPLICATE).unwrap_err(),
            zx_status::Status::INVALID_ARGS
        );
    }

    #[test]
    fn handle_replace_same_rights() {
        let (c1, _) = Channel::create().unwrap();
        let orig_basic_info = c1.basic_info().unwrap();
        assert_eq!(orig_basic_info.rights, Rights::CHANNEL_DEFAULT);
        let orig_raw = c1.raw_handle();

        let new_handle = c1.into_handle().replace(Rights::SAME_RIGHTS).unwrap();
        assert_eq!(new_handle.raw_handle(), orig_raw);

        let new_basic_info = new_handle.basic_info().unwrap();
        assert_eq!(new_basic_info.rights, Rights::CHANNEL_DEFAULT);
    }

    #[test]
    fn await_user_signal() {
        let (c1, _) = EventPair::create().unwrap();
        let mut on_sig = on_signals::OnSignals::new(&c1, Signals::USER_0);
        let (waker, count) = futures_test::task::new_count_waker();
        let mut ctx = Context::from_waker(&waker);
        assert_eq!(on_sig.poll_unpin(&mut ctx), Poll::Pending);
        assert_eq!(count, 0);
        c1.signal_handle(Signals::empty(), Signals::USER_1).unwrap();
        assert_eq!(count, 0);
        c1.signal_handle(Signals::empty(), Signals::USER_0).unwrap();
        assert_eq!(count, 1);
        assert_eq!(on_sig.poll_unpin(&mut ctx), Poll::Ready(Ok(Signals::USER_0)));
        c1.signal_handle(Signals::USER_0, Signals::empty()).unwrap();
        let mut on_sig = on_signals::OnSignals::new(&c1, Signals::USER_0);
        assert_eq!(on_sig.poll_unpin(&mut ctx), Poll::Pending);
        assert_eq!(count, 1);
        c1.signal_handle(Signals::empty(), Signals::USER_0).unwrap();
        assert_eq!(count, 2);
        assert_eq!(on_sig.poll_unpin(&mut ctx), Poll::Ready(Ok(Signals::USER_0)));
    }

    #[test]
    fn await_user_signal_peer() {
        let (c1, c2) = EventPair::create().unwrap();
        let mut on_sig = on_signals::OnSignals::new(&c1, Signals::USER_0);
        let (waker, count) = futures_test::task::new_count_waker();
        let mut ctx = Context::from_waker(&waker);
        assert_eq!(on_sig.poll_unpin(&mut ctx), Poll::Pending);
        assert_eq!(count, 0);
        c2.signal_peer(Signals::empty(), Signals::USER_1).unwrap();
        assert_eq!(count, 0);
        c2.signal_peer(Signals::empty(), Signals::USER_0).unwrap();
        assert_eq!(count, 1);
        assert_eq!(on_sig.poll_unpin(&mut ctx), Poll::Ready(Ok(Signals::USER_0)));
        c2.signal_peer(Signals::USER_0, Signals::empty()).unwrap();
        let mut on_sig = on_signals::OnSignals::new(&c1, Signals::USER_0);
        assert_eq!(on_sig.poll_unpin(&mut ctx), Poll::Pending);
        assert_eq!(count, 1);
        c2.signal_peer(Signals::empty(), Signals::USER_0).unwrap();
        assert_eq!(count, 2);
        assert_eq!(on_sig.poll_unpin(&mut ctx), Poll::Ready(Ok(Signals::USER_0)));
    }

    #[test]
    fn await_close_signal() {
        let (c1, c2) = EventPair::create().unwrap();
        let mut on_sig = on_signals::OnSignals::new(&c1, Signals::OBJECT_PEER_CLOSED);
        let (waker, count) = futures_test::task::new_count_waker();
        let mut ctx = Context::from_waker(&waker);
        assert_eq!(on_sig.poll_unpin(&mut ctx), Poll::Pending);
        assert_eq!(count, 0);
        c1.signal_handle(Signals::empty(), Signals::USER_1).unwrap();
        assert_eq!(count, 0);
        c1.signal_handle(Signals::empty(), Signals::USER_0).unwrap();
        assert_eq!(count, 0);
        std::mem::drop(c2);
        assert_eq!(count, 1);
        assert_eq!(on_sig.poll_unpin(&mut ctx), Poll::Ready(Ok(Signals::OBJECT_PEER_CLOSED)));
    }

    #[test]
    fn user_signal_no_rights() {
        let (c1, _) = EventPair::create().unwrap();
        let c1 = c1.into_handle().replace(Rights::EVENTPAIR_DEFAULT & !Rights::SIGNAL).unwrap();
        assert_eq!(c1.signal_handle(Signals::empty(), Signals::USER_1), Err(Status::ACCESS_DENIED));
    }

    #[test]
    fn user_signal_peer_no_rights() {
        let (c1, _) = EventPair::create().unwrap();
        let c1 =
            c1.into_handle().replace(Rights::EVENTPAIR_DEFAULT & !Rights::SIGNAL_PEER).unwrap();
        let c1 = EventPair::from(c1);
        assert_eq!(c1.signal_peer(Signals::empty(), Signals::USER_1), Err(Status::ACCESS_DENIED));
    }

    #[test]
    fn kernel_signal_denied() {
        let (c1, _) = EventPair::create().unwrap();
        assert_eq!(
            c1.signal_handle(Signals::empty(), Signals::OBJECT_WRITABLE),
            Err(Status::INVALID_ARGS)
        );
    }

    #[test]
    fn kernel_signal_peer_denied() {
        let (c1, _) = EventPair::create().unwrap();
        assert_eq!(
            c1.signal_peer(Signals::empty(), Signals::OBJECT_WRITABLE),
            Err(Status::INVALID_ARGS)
        );
    }

    #[test]
    fn handles_always_writable() {
        let mut ctx = futures_test::task::noop_context();
        let (s1, s2) = Socket::create(SocketOpts::STREAM).unwrap();
        let mut on_sig = on_signals::OnSignals::new(&s1, Signals::OBJECT_WRITABLE);
        assert_eq!(on_sig.poll_unpin(&mut ctx), Poll::Ready(Ok(Signals::OBJECT_WRITABLE)));
        let mut on_sig = on_signals::OnSignals::new(&s2, Signals::OBJECT_WRITABLE);
        assert_eq!(on_sig.poll_unpin(&mut ctx), Poll::Ready(Ok(Signals::OBJECT_WRITABLE)));

        let (s1, s2) = Socket::create(SocketOpts::DATAGRAM).unwrap();
        let mut on_sig = on_signals::OnSignals::new(&s1, Signals::OBJECT_WRITABLE);
        assert_eq!(on_sig.poll_unpin(&mut ctx), Poll::Ready(Ok(Signals::OBJECT_WRITABLE)));
        let mut on_sig = on_signals::OnSignals::new(&s2, Signals::OBJECT_WRITABLE);
        assert_eq!(on_sig.poll_unpin(&mut ctx), Poll::Ready(Ok(Signals::OBJECT_WRITABLE)));

        let (c1, c2) = Channel::create().unwrap();
        let mut on_sig = on_signals::OnSignals::new(&c1, Signals::OBJECT_WRITABLE);
        assert_eq!(on_sig.poll_unpin(&mut ctx), Poll::Ready(Ok(Signals::OBJECT_WRITABLE)));
        let mut on_sig = on_signals::OnSignals::new(&c2, Signals::OBJECT_WRITABLE);
        assert_eq!(on_sig.poll_unpin(&mut ctx), Poll::Ready(Ok(Signals::OBJECT_WRITABLE)));
    }

    #[test]
    fn read_signal() {
        let (c1, c2) = Channel::create().unwrap();
        let mut on_sig = on_signals::OnSignals::new(&c1, Signals::OBJECT_READABLE);
        let (waker, count) = futures_test::task::new_count_waker();
        let mut ctx = Context::from_waker(&waker);
        assert_eq!(on_sig.poll_unpin(&mut ctx), Poll::Pending);
        assert_eq!(count, 0);
        assert_eq!(c2.write(b"abc", &mut []), Ok(()));
        assert_eq!(count, 1);
        assert_eq!(on_sig.poll_unpin(&mut ctx), Poll::Ready(Ok(Signals::OBJECT_READABLE)));
    }
}
